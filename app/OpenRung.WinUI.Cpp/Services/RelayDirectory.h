#pragma once
#include "pch.h"
#include "../Models/Dto.h"

namespace Services
{
    /// Shared relay-directory state for the home server card and the servers
    /// page. Mutated by RelayDirectory::Load on a worker thread; the store
    /// notifies listeners on the UI thread, and pages rebuild their ListView
    /// rows from the snapshot there.
    class RelayStore
    {
    public:
        static RelayStore& Instance();

        std::vector<RelayInfo> Relays() const;            // mutex-protected copy
        std::wstring SelectedId() const;
        void SetSelectedId(std::wstring id);
        std::wstring SummaryText() const;
        bool Loading() const;
        std::wstring Error() const;

        long Revision() const; // increments on every mutation
        using Listener = std::function<void()>;
        void AddListener(void const* key, Listener fn);
        void RemoveListener(void const* key);

        // Mutation helpers; invoked from RelayDirectory / pages.
        void SetLoading(bool v);
        void SetError(std::wstring e);
        void SetRelays(std::vector<RelayInfo> relays, std::wstring summaryText);

    private:
        mutable std::mutex m_mutex;
        std::vector<RelayInfo> m_relays;
        std::wstring m_selectedId;
        std::wstring m_summary;
        std::wstring m_error;
        bool m_loading = false;
        long m_revision = 0;
        std::unordered_map<void const*, Listener> m_listeners;

        void Notify(); // posts to UI thread
    };

    /// Shared relay-directory loading: fetches the ranked directory from the
    /// core, assigns 国家+地区+编号 display titles, and fills the store.
    /// Blocking; throws on failure — the caller owns the error surface.
    namespace RelayDirectory
    {
        void Load();  // fetch + assign titles + publish (throws)
        void SelectLowestLatency(); // pick lowest-latency probed relay
        /// Title helpers exposed for the home page card.
        std::wstring CountryName(RelayInfo const& relay);
        std::wstring DisplayTitleOf(RelayInfo const& relay); // computed lazily if absent
    }
}
