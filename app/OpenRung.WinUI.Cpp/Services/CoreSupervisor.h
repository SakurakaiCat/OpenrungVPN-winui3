#pragma once
#include "pch.h"
#include "CoreManager.h"
#include "CoreEventsClient.h"
#include "../Models/Dto.h"

namespace Services
{
    /// UI-facing facade over the core: holds the latest StateSnapshot, runs the
    /// SSE stream with reconnect/backoff, and exposes log entries for the pages.
    /// All callbacks fire on a worker thread — the consumer marshals to the UI
    /// thread via its DispatcherQueue (AppState::ApplyState does this).
    class CoreSupervisor
    {
    public:
        /// App-wide singleton (one supervisor owns the one core).
        static CoreSupervisor& Instance();

        CoreSupervisor();
        ~CoreSupervisor(); // stops the stream and the core

        CoreSupervisor(CoreSupervisor const&) = delete;
        CoreSupervisor& operator=(CoreSupervisor const&) = delete;

        CoreManager& Core() { return m_core; }

        /// Latest state from the SSE stream (thread-safe copy).
        std::optional<StateSnapshot> LatestState() const;

        std::function<void(StateSnapshot const&)> StateChanged;
        std::function<void(LogLine const&)> LogReceived;
        /// Raised when the stream drops and will be retried.
        std::function<void()> ConnectionLost;

        /// Start (or adopt) the core, seed state, and begin the event stream.
        /// Idempotent. Blocking: call from a worker thread.
        void Start();

        /// Restart the core elevated (UAC prompt) and re-establish the stream.
        /// Blocking: call from a worker thread.
        void RestartElevated();

        /// Stop the event stream and the core. Idempotent.
        void Stop();

    private:
        CoreManager m_core;
        mutable std::mutex m_gate;
        std::thread m_streamThread;
        std::atomic<bool> m_stopStream{ false };
        std::atomic<bool> m_disposed{ false };
        std::atomic<bool> m_streamRunning{ false };

        mutable std::mutex m_stateGate;
        std::optional<StateSnapshot> m_state;

        void RunEventLoop();
        void DispatchEvent(EventItem const& item);
        void RaiseState(StateSnapshot const& state);
    };
}
