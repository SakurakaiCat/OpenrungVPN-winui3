#include "pch.h"
#include "../Models/Dto.h"
#include "RelayDirectory.h"
#include "CoreSupervisor.h"
#include "AppLog.h"
#include "AppState.h"
#include "AppSettings.h"
#include "Localization.h"
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
        // Mainland China relays are volunteer-provided with very poor
        // availability; hidden by default per the settings toggle.
        bool IsMainlandChinaRelay(RelayInfo const& relay)
        {
            if (relay.countryCode.size() == 2)
            {
                auto cc = relay.countryCode;
                std::transform(cc.begin(), cc.end(), cc.begin(), ::towupper);
                if (cc == L"CN")
                    return true;
            }
            if (relay.country.find(L"中国") != std::wstring::npos)
                return true;
            auto lower = relay.country;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
            return lower.find(L"china") != std::wstring::npos;
        }

        std::wstring TitleCase(std::wstring city)
        {
            // Broker cities are English ("Los Angeles", "helsinki"); normalize
            // to Title Case for display.
            bool wordStart = true;
            for (auto& ch : city)
            {
                if (wordStart && ch >= L'a' && ch <= L'z')
                    ch = ch - L'a' + L'A';
                wordStart = (ch == L' ' || ch == L'-');
            }
            return city;
        }

        std::wstring CityLocalize(std::wstring const& city)
        {
            // The broker sends latin city names; the static tables map them
            // to zh (and back) for the two languages.
            if (city.empty()) return {};
            auto lower = city;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
            if (I18n::Language() == L"en")
            {
                if (auto* en = NameTables::LookupCityEnFromZh(city))
                    return en; // broker variant that is already zh
                return TitleCase(city);
            }
            if (auto* zh = NameTables::LookupCityLower(lower))
                return zh;
            return city;
        }

        std::wstring CountryLocalize(RelayInfo const& relay)
        {
            if (relay.countryCode.size() == 2)
            {
                auto upper = relay.countryCode;
                std::transform(upper.begin(), upper.end(), upper.begin(), ::towupper);
                if (I18n::Language() == L"en")
                {
                    if (auto* en = NameTables::LookupCountryEn(upper))
                        return en;
                }
                else if (auto* zh = NameTables::LookupCountry(upper))
                    return zh;
            }
            if (I18n::Language() == L"en")
                if (auto* en = NameTables::LookupCountryEnFromZh(relay.country))
                    return en;
            return relay.country.empty() ? I18n::Tr(L"relay.unnamed") : relay.country;
        }

        /// Rebuilds display labels ("国家城市N" / "Country City N") in the
        /// active language. Called on every load and on language changes.
        void BuildLabels(std::vector<RelayInfo>& relays)
        {
            bool english = I18n::Language() == L"en";
            std::unordered_map<std::wstring, int> counters;
            for (auto& relay : relays)
            {
                auto country = CountryLocalize(relay);
                auto city = CityLocalize(relay.city);
                // "新加坡新加坡1" reads duplicated; skip a city matching its country.
                if (city == country)
                    city.clear();
                auto key = relay.countryCode + L"|" + relay.city;
                int n = ++counters[key];
                // zh glues the parts (日本东京1); en separates them.
                relay.label = english
                    ? (city.empty() ? country : country + L" " + city) + L" " + std::to_wstring(n)
                    : (city.empty() ? country : country + city) + std::to_wstring(n);
            }
        }
    }

    std::wstring RelayDirectory::CountryName(RelayInfo const& relay)
    {
        return CountryLocalize(relay);
    }

    void RelayDirectory::Retitle()
    {
        // Re-applies the display labels of the cached relay list in the
        // active language (no network round-trip).
        auto& store = RelayStore::Instance();
        auto relays = store.Relays();
        if (relays.empty())
            return;
        BuildLabels(relays);
        auto total = relays.size();
        long ranked = 0;
        for (auto const& r : relays)
            if (r.latencyMs) ++ranked;
        store.SetRelays(std::move(relays), I18n::Tr(L"relay.summary",
            std::to_wstring(total), std::to_wstring(ranked)));
    }

    void RelayDirectory::Load()
    {
        auto& api = CoreSupervisor::Instance().Core().EnsureRunning(false);
        std::vector<RelayInfo> relays;
        auto serverTime = api.GetRelays(relays);

        // Default-off for mainland China relays (volunteer nodes with very
        // poor availability); the settings page carries the security warning.
        size_t hidden = 0;
        if (AppSettings::Load().hideCnRelays)
        {
            auto keep = std::stable_partition(relays.begin(), relays.end(),
                [](RelayInfo const& r) { return !IsMainlandChinaRelay(r); });
            hidden = static_cast<size_t>(std::distance(keep, relays.end()));
            relays.erase(keep, relays.end());
        }
        BuildLabels(relays);

        long ranked = 0;
        for (auto const& r : relays)
            if (r.latencyMs) ++ranked;
        auto summary = I18n::Tr(L"relay.summary",
            std::to_wstring(relays.size()), std::to_wstring(ranked));

        auto& store = RelayStore::Instance();
        store.SetRelays(std::move(relays), summary);
        if (hidden > 0)
            AppLog::Write(I18n::Tr(L"log.cnRelaysHidden", std::to_wstring(hidden)));
        // A selection pointing at a relay that is no longer listed (hidden or
        // gone upstream) must not silently drive the connect button. The
        // smart-routing pseudo-node never enters the store list, so keep it.
        auto selected = store.SelectedId();
        bool selectedPresent = selected == kSmartRelayId;
        for (auto const& r : store.Relays())
            if (r.id == selected) { selectedPresent = true; break; }
        if (selected.empty() || !selectedPresent)
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
                AppLog::Write(I18n::Tr(L"log.relayUpdateDone",
                    std::to_wstring(store.Relays().size())));
            }
            catch (std::exception const& ex)
            {
                store.SetError(Services::Utf8ToWide(ex.what()));
                AppLog::Write(I18n::Tr(L"log.relayUpdateFailed") + L" " + Utf8ToWide(ex.what()));
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

    bool RelayDirectory::SwitchToSelected()
    {
        auto const& state = AppState::Instance().Current();
        // Only a live session switches; idle selection is a pure store update
        // (the connect button uses it later), and a busy state means a
        // connect is already under way — queuing another one would flash
        // spurious "failed" states from the cancelled ladder.
        if (state.status != L"connected")
            return false;
        auto selected = RelayStore::Instance().SelectedId();
        // Smart routing while live: connect-while-connected IS the switch —
        // the engine tears the current session down and re-dials through
        // its own ranked auto-select ladder.
        if (selected == kSmartRelayId)
        {
            ConnectSmart();
            return true;
        }
        if (selected.empty() || !state.connection || state.connection->relayId == selected)
            return false;

        std::thread([selected] {
            try
            {
                auto& api = CoreSupervisor::Instance().Core().EnsureRunning(false);
                AppLog::Write(L"switching relay: " + selected);
                // Connect-while-connected IS the switch: the engine serializes
                // with the live session (connectMu), tears it down fully, and
                // dials the new relay.
                api.Connect(L"", selected, L"");
            }
            catch (std::exception const& ex)
            {
                AppLog::Write(L"relay switch failed: " + Utf8ToWide(ex.what()));
            }
        }).detach();
        return true;
    }

    std::wstring RelayDirectory::DisplayTitleOf(RelayInfo const& relay)
    {
        return relay.label.empty() ? relay.id : relay.label;
    }

    // ---- Smart routing -------------------------------------------------------
    //
    // The "smart routing" pseudo-node (kSmartRelayId) pinned to the top of
    // both relay lists. Connecting with it selected dispatches ONE
    // auto-select connect (empty relay/country target) — the same one-click
    // flow the official mobile client uses for its auto relay, where the
    // core owns the entire ladder:
    //
    //   - fetch the broker directory, filter usable relays;
    //   - probe the candidate head's TCP connect latency in parallel
    //     (RelayRankMaxProbes) and reorder by latency BUCKET, so broker
    //     order — and with it the broker's load balancing — still decides
    //     inside a bucket (ranker.go; no herding onto the nearest relay);
    //   - dial candidates in turn, each rung with direct -> hub punch ->
    //     signed WSS/CDN fallback (runLadder);
    //   - once connected, supervise: a dropped session re-ladders with the
    //     failed relay demoted last (monitor.go).
    //
    // Progress arrives as engine log lines and state events on the SSE
    // stream, exactly like any other connect. The client keeps no probe
    // pass, no per-attempt polling, no disconnect churn, and no client-side
    // ordering at all — every one of those was a slower, less capable
    // reimplementation of what the engine already does.

    namespace
    {
        /// Guards only the in-flight dispatch POST: /api/connect returns 202
        /// as soon as the engine has taken over, after which the outcome
        /// belongs to the SSE state stream, and any later user action
        /// (connect to a specific relay, disconnect) simply replaces the
        /// engine's session — connectMu serializes teardown-then-install.
        std::mutex g_smartDispatchMutex;
        bool g_smartDispatching = false;

        // Only dial-stage (reachability) failures are worth retrying on a
        // different relay. Elevation refusals, tun_conflict and bad requests
        // cannot be fixed by another relay, and an empty error means an
        // unknown cause.
        bool RetryableConnectError(std::wstring const& error)
        {
            if (error.empty())
                return false;
            static const wchar_t* const needles[] = {
                L"All relay connection attempts failed", L"not reachable",
                L"i/o timeout",     L"connection refused",
                L"network unreachable", L"no route to host",
                L"handshake timeout",   L"no such host",
                L"connection reset",
            };
            for (auto const* needle : needles)
                if (error.find(needle) != std::wstring::npos)
                    return true;
            return false;
        }
    }

    /// Dispatches the core's auto-select connect on a worker thread (POST
    /// /api/connect with empty targets; EnsureRunning may spawn the core,
    /// which is not a UI-thread job). onFinished fires once the POST has
    /// returned: nullptr when the ladder was accepted (its outcome arrives
    /// via state events), the exception on a synchronous refusal (428
    /// elevation, 409 tun_conflict, dead core). Without a callback (relay
    /// switch path) the refusal is surfaced inline instead.
    bool RelayDirectory::ConnectSmart(std::function<void(std::exception_ptr)> onFinished)
    {
        {
            std::lock_guard lock(g_smartDispatchMutex);
            if (g_smartDispatching)
                return false; // previous dispatch still in flight
            g_smartDispatching = true;
        }
        std::thread([onFinished = std::move(onFinished)] {
            auto done = [&] {
                std::lock_guard lock(g_smartDispatchMutex);
                g_smartDispatching = false;
            };
            try
            {
                // Empty broker/relay/country: the core's auto-select target,
                // which runs its own ranked ladder (see the block comment).
                CoreSupervisor::Instance().Core().EnsureRunning(false)
                    .Connect(L"", L"", L"");
                done();
                if (onFinished)
                    onFinished(nullptr); // accepted; outcome via state events
            }
            catch (...)
            {
                done();
                if (onFinished)
                {
                    onFinished(std::current_exception());
                }
                else
                {
                    // No callback (relay switch path): inline error surface.
                    try { throw; }
                    catch (std::exception const& ex)
                    {
                        AppLog::Write(Utf8ToWide(ex.what()));
                        RelayStore::Instance().SetError(Utf8ToWide(ex.what()));
                    }
                }
            }
        }).detach();
        return true;
    }

    bool RelayDirectory::IsRetryableConnectError(std::wstring const& error)
    {
        return RetryableConnectError(error);
    }
}
