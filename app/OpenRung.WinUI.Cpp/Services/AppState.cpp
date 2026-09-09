#include "pch.h"
#include "../Models/Dto.h"
#include "AppState.h"
#include "AppLog.h"

namespace Services
{
    AppState& AppState::Instance()
    {
        static AppState instance;
        return instance;
    }

    void AppState::ApplyState(StateSnapshot s)
    {
        {
            std::lock_guard lock(m_mutex);
            m_state = std::move(s);
        }
        Notify();
    }

    StateSnapshot AppState::Current() const
    {
        std::lock_guard lock(m_mutex);
        return m_state;
    }

    void AppState::Tick()
    {
        Notify();
    }

    void AppState::SetCoreBooting(bool booting)
    {
        {
            std::lock_guard lock(m_mutex);
            if (m_coreBooting == booting)
                return;
            m_coreBooting = booting;
        }
        Notify();
    }

    bool AppState::CoreBooting() const
    {
        std::lock_guard lock(m_mutex);
        return m_coreBooting;
    }

    void AppState::Notify()
    {
        // Snapshot listeners under the lock; callbacks can unsubscribe.
        std::vector<Listener> snapshot;
        {
            std::lock_guard lock(m_mutex);
            snapshot.reserve(m_listeners.size());
            for (auto const& [_, fn] : m_listeners)
                snapshot.push_back(fn);
        }
        // Tick is already on the UI thread and posting again would be harmless,
        // but ApplyState arrives from the SSE thread, so always hop.
        Ui::Post([snapshot = std::move(snapshot)] {
            for (auto const& fn : snapshot)
                fn();
        });
    }

    void AppState::AddListener(void const* key, Listener fn)
    {
        std::lock_guard lock(m_mutex);
        m_listeners[key] = std::move(fn);
    }

    void AppState::RemoveListener(void const* key)
    {
        std::lock_guard lock(m_mutex);
        m_listeners.erase(key);
    }
}
