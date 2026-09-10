#include "pch.h"
#include "../Models/Dto.h"
#include "RelayDirectory.h"
#include "CoreSupervisor.h"
#include "AppLog.h"
#include "AppState.h"
#include "../Models/NameTables.h"

#include <cwctype>

namespace Services
{
    RelayStore& RelayStore::Instance()
    {
        static RelayStore instance;
        return instance;
    }

    std::vector<RelayInfo> RelayStore::Relays() const
    {
        std::lock_guard lock(m_mutex);
        return m_relays;
    }

    std::wstring RelayStore::SelectedId() const
    {
        std::lock_guard lock(m_mutex);
        return m_selectedId;
    }

    void RelayStore::SetSelectedId(std::wstring id)
    {
        {
            std::lock_guard lock(m_mutex);
            m_selectedId = std::move(id);
            ++m_revision;
        }
        Notify();
    }

    std::wstring RelayStore::SummaryText() const
    {
        std::lock_guard lock(m_mutex);
        return m_summary;
    }

    bool RelayStore::Loading() const
    {
        std::lock_guard lock(m_mutex);
        return m_loading;
    }

    std::wstring RelayStore::Error() const
    {
        std::lock_guard lock(m_mutex);
        return m_error;
    }

    long RelayStore::Revision() const
    {
        std::lock_guard lock(m_mutex);
        return m_revision;
    }

    void RelayStore::AddListener(void const* key, Listener fn)
    {
        std::lock_guard lock(m_mutex);
        m_listeners[key] = std::move(fn);
    }

    void RelayStore::RemoveListener(void const* key)
    {
        std::lock_guard lock(m_mutex);
        m_listeners.erase(key);
    }

    void RelayStore::SetLoading(bool v)
    {
        {
            std::lock_guard lock(m_mutex);
            m_loading = v;
            ++m_revision;
        }
        Notify();
    }

    void RelayStore::SetError(std::wstring e)
    {
        {
            std::lock_guard lock(m_mutex);
            m_error = std::move(e);
            ++m_revision;
        }
        Notify();
    }

    void RelayStore::SetRelays(std::vector<RelayInfo> relays, std::wstring summaryText)
    {
        {
            std::lock_guard lock(m_mutex);
            m_relays = std::move(relays);
            m_summary = std::move(summaryText);
            ++m_revision;
        }
        Notify();
    }

    void RelayStore::SetTesting(bool v)
    {
        {
            std::lock_guard lock(m_mutex);
            m_testing = v;
            ++m_revision;
        }
        Notify();
    }

    bool RelayStore::Testing() const
    {
        std::lock_guard lock(m_mutex);
        return m_testing;
    }

    void RelayStore::SetTestStatus(std::wstring s)
    {
        {
            std::lock_guard lock(m_mutex);
            m_testStatus = std::move(s);
            ++m_revision;
        }
        Notify();
    }

    std::wstring RelayStore::TestStatus() const
    {
        std::lock_guard lock(m_mutex);
        return m_testStatus;
    }

    void RelayStore::ApplyLatency(std::wstring const& id, std::optional<long> tcpingMs,
        std::optional<long> realMs)
    {
        {
            std::lock_guard lock(m_mutex);
            for (auto& relay : m_relays)
            {
                if (relay.id != id)
                    continue;
                if (tcpingMs)
                    relay.tcpingMs = tcpingMs;
                if (realMs)
                    relay.realMs = realMs;
                ++m_revision;
                break;
            }
        }
        Notify();
    }

    void RelayStore::Notify()
    {
        std::vector<Listener> snapshot;
        {
            std::lock_guard lock(m_mutex);
            snapshot.reserve(m_listeners.size());
            for (auto const& [_, fn] : m_listeners)
                snapshot.push_back(fn);
        }
        Ui::Post([snapshot = std::move(snapshot)] {
            for (auto const& fn : snapshot)
                fn();
        });
    }

    namespace
    {
        std::wstring CityLocalize(std::wstring const& city)
        {
            // City names arrive in zh already from the broker; the static table
            // covers the few English leftovers. Keys are lowercase.
            if (city.empty()) return {};
            auto lower = city;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
            if (auto* zh = NameTables::LookupCityLower(lower))
                return zh;
            return city;
        }
    }

    std::wstring RelayDirectory::CountryName(RelayInfo const& relay)
    {
        if (relay.countryCode.size() == 2)
        {
            auto upper = relay.countryCode;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::towupper);
            if (auto* zh = NameTables::LookupCountry(upper))
                return zh;
        }
        return relay.country.empty() ? L"节点" : relay.country;
    }

    namespace
    {
        void AssignDisplayTitles(std::vector<RelayInfo>& relays)
        {
            std::unordered_map<std::wstring, int> counters;
            for (auto& relay : relays)
            {
                auto country = RelayDirectory::CountryName(relay);
                auto city = CityLocalize(relay.city);
                // "新加坡新加坡1" reads duplicated; skip a city matching its country.
                if (city == country)
                    city.clear();
                auto key = relay.countryCode + L"|" + relay.city;
                int n = ++counters[key];
                relay.label = city.empty()
                    ? country + std::to_wstring(n)
                    : country + city + std::to_wstring(n);
            }
        }
    }

    void RelayDirectory::Load()
    {
        auto& api = CoreSupervisor::Instance().Core().EnsureRunning(false);
        std::vector<RelayInfo> relays;
        auto serverTime = api.GetRelays(relays);
        AssignDisplayTitles(relays);

        long ranked = 0;
        for (auto const& r : relays)
            if (r.latencyMs) ++ranked;
        auto summary = std::to_wstring(relays.size()) + L" 个节点，已测速 " + std::to_wstring(ranked) + L" 个";

        auto& store = RelayStore::Instance();
        store.SetRelays(std::move(relays), summary);
        if (store.SelectedId().empty())
            SelectLowestLatency();
    }

    bool RelayDirectory::UpdateFromRemote()
    {
        auto& store = RelayStore::Instance();
        // Busy gating: callers are all on the UI thread (click handlers),
        // so check-then-set cannot race here. The auto-refresh thread also
        // honors Loading() before calling Load().
        if (store.Loading())
            return false;
        store.SetLoading(true);
        store.SetError(L"");
        std::thread([&store] {
            try
            {
                Load();
                AppLog::Write(L"从远端更新节点完成：" +
                    std::to_wstring(store.Relays().size()) + L" 个节点");
            }
            catch (std::exception const& ex)
            {
                store.SetError(Services::Utf8ToWide(ex.what()));
                AppLog::Write(L"从远端更新节点失败：" + Utf8ToWide(ex.what()));
            }
            store.SetLoading(false);
        }).detach();
        return true;
    }

    void RelayDirectory::StartAutoRefresh()
    {
        static std::once_flag once;
        std::call_once(once, [] {
            std::thread([] {
                // The core's /api/relays?ranked=1 re-probes upstream latency
                // (availability) on every call, so each refresh is a live
                // availability pass, not a cached copy.
                for (;;)
                {
                    std::this_thread::sleep_for(std::chrono::seconds(60));
                    auto& store = RelayStore::Instance();
                    if (store.Loading())
                        continue;
                    if (AppState::Instance().CoreBooting())
                        continue; // the startup path will Load() when ready
                    try
                    {
                        auto before = static_cast<long>(store.Relays().size());
                        Load();
                        auto after = static_cast<long>(RelayStore::Instance().Relays().size());
                        if (before != after)
                            AppLog::Write(L"relay directory updated: " +
                                std::to_wstring(after) + L" 个节点");
                    }
                    catch (std::exception const& ex)
                    {
                        // Keep the last good list; note the failure in the log.
                        AppLog::Write(L"relay auto-refresh failed: " + Utf8ToWide(ex.what()));
                    }
                }
            }).detach();
        });
    }

    void RelayDirectory::SelectLowestLatency()
    {
        auto relays = RelayStore::Instance().Relays();
        std::optional<RelayInfo> best;
        for (auto const& r : relays)
        {
            if (!r.latencyMs) continue;
            if (!best || *r.latencyMs < *best->latencyMs)
                best = r;
        }
        if (best)
            RelayStore::Instance().SetSelectedId(best->id);
    }

    std::wstring RelayDirectory::DisplayTitleOf(RelayInfo const& relay)
    {
        return relay.label.empty() ? relay.id : relay.label;
    }
}
