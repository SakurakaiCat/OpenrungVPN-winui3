#include "pch.h"
#include "../Models/Dto.h"
#include "RelayDirectory.h"
#include "CoreSupervisor.h"
#include "AppLog.h"
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
