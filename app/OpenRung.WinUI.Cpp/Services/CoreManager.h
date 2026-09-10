#pragma once
#include "pch.h"
#include "CoreApiClient.h"

namespace Services
{
    namespace detail
    {
        /// Minimal unique Win32 handle (WIL is not a dependency of this project).
        struct UniqueHandle
        {
            HANDLE h = nullptr;
            UniqueHandle() = default;
            explicit UniqueHandle(HANDLE v) : h(v) {}
            ~UniqueHandle() { reset(); }
            UniqueHandle(UniqueHandle const&) = delete;
            UniqueHandle& operator=(UniqueHandle const&) = delete;
            UniqueHandle(UniqueHandle&& o) noexcept : h(o.h) { o.h = nullptr; }
            UniqueHandle& operator=(UniqueHandle&& o) noexcept { reset(o.h); o.h = nullptr; return *this; }
            HANDLE get() const { return h; }
            explicit operator bool() const { return h != nullptr; }
            void reset(HANDLE v = nullptr)
            {
                if (h) ::CloseHandle(h);
                h = v;
            }
        };
    }

    /// Owns the openrung-core.exe child process: launch (or elevated relaunch),
    /// endpoint-file discovery, heartbeat, and graceful shutdown. Blocking
    /// calls; invoke from worker threads only.
    class CoreManager
    {
    public:
        CoreManager();
        ~CoreManager(); // stops heartbeat + process quietly

        CoreManager(CoreManager const&) = delete;
        CoreManager& operator=(CoreManager const&) = delete;

        /// Ensures the core is running. Adopts a healthy core already on the
        /// endpoint file (e.g. an earlier elevated respawn), otherwise spawns.
        CoreApiClient& EnsureRunning(bool elevated);

        /// <exe dir>\core\openrung-core.exe, or the repo dist\ fallback.
        static std::wstring CoreExePath();

        /// Politely ask the core to exit (contract /api/shutdown), then wait.
        void Stop();

        /// Stop (when running) then start a fresh core; blocking. The new
        /// instance re-reads the endpoint file and may adopt an unrelated
        /// live core instead of spawning.
        void Restart();

        /// Restart the core running elevated (for TUN mode).
        void RestartElevated();

        bool IsCoreRunning() const;

        std::optional<int> EndpointPid() const;
        std::optional<unsigned short> EndpointPort() const;
        std::optional<std::wstring> EndpointToken() const;

        /// Raised when the core dies unexpectedly. Fired from a worker thread.
        std::function<void()> CoreExited;

    private:
        struct Endpoint
        {
            unsigned short port = 0;
            std::wstring token;
            int pid = 0;
            bool ours = false; // true when we spawned this instance
        };

        static constexpr auto HeartbeatInterval = std::chrono::seconds(5);
        static constexpr auto StartupTimeout = std::chrono::seconds(15);

        mutable std::mutex m_gate;
        /// Serializes the whole probe/adopt/spawn sequence: EnsureRunning
        /// runs on several threads (app startup, relay load, connect) and a
        /// second caller must never spawn a rival core while the first is
        /// between Spawn and endpoint adoption.
        std::mutex m_ensureLock;
        detail::UniqueHandle m_process;
        detail::UniqueHandle m_stopEvent;
        detail::UniqueHandle m_exitEvent;
        std::optional<Endpoint> m_endpoint;
        std::unique_ptr<CoreApiClient> m_api;
        std::thread m_heartbeatThread;
        std::thread m_waitThread;
        std::atomic<bool> m_stopping{ false };
        std::atomic<bool> m_exitedSignaled{ false };

        std::optional<Endpoint> TryAdopt(bool requireElevated);
        void Spawn(bool elevated);
        Endpoint WaitForEndpoint();
        static std::optional<Endpoint> ReadEndpointFile();
        static bool ProcessAlive(int pid);
        void StartHeartbeatLocked();
        void HeartbeatLoop();
        static void DrainPipe(HANDLE readEnd);
        void RaiseCoreExited();
    };
}
