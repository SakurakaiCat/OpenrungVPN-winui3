#pragma once
#include "pch.h"
#include "../Models/Dto.h"

namespace Services
{
    /// Shared app state: the latest StateSnapshot from the SSE stream. Views
    /// subscribe with a key and get notified on the UI thread; they pull
    /// Current() and re-render. Replaces the C# AppStateViewModel.
    class AppState
    {
    public:
        static AppState& Instance();

        /// Any thread (typically the SSE worker). Notifies listeners on the UI thread.
        void ApplyState(StateSnapshot s);

        /// Current snapshot; safe to call from any thread.
        StateSnapshot Current() const;

        /// Once per second from the shell's DispatcherTimer (UI thread): notifies
        /// so the elapsed-time display advances while connected.
        void Tick();

        /// True from process start until the first EnsureRunning probe/spawn
        /// settles; HomePage shows the boot hint while set. Any thread.
        void SetCoreBooting(bool booting);
        bool CoreBooting() const;

        using Listener = std::function<void()>;
        /// Listener invoked on the UI thread after every state change / tick.
        void AddListener(void const* key, Listener fn);
        void RemoveListener(void const* key);

    private:
        mutable std::mutex m_mutex;
        StateSnapshot m_state;
        bool m_coreBooting = true; // until the first EnsureRunning settles
        std::unordered_map<void const*, Listener> m_listeners;

        void Notify();
    };
}
