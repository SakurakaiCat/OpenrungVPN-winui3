#include "pch.h"
#include "CoreManager.h"
#include "AppLog.h"
#include "AppSettings.h"
#include "../Models/Dto.h"
#include <shellapi.h>

using namespace winrt::Windows::Data::Json;
using namespace Services;
using Services::detail::UniqueHandle;

namespace Services
{
    namespace
    {
        std::wstring ExeDir()
        {
            wchar_t buf[MAX_PATH];
            ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
            std::wstring path = buf;
            auto slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? L"." : path.substr(0, slash);
        }

        /// NO_PROXY for the core's environment: the core is the proxy itself;
        /// routing its outbound through the OS proxy would loop straight into
        /// its own loopback inbound. Set on the current process before spawn;
        /// CreateProcessW(nullptr env) lets the child inherit it.
        void SetNoProxyEnv()
        {
            ::SetEnvironmentVariableW(L"NO_PROXY", L"*");
            ::SetEnvironmentVariableW(L"no_proxy", L"*");
        }
    }

    CoreManager::CoreManager()
    {
        m_stopEvent.reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        m_exitEvent.reset(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!m_stopEvent || !m_exitEvent)
            throw std::runtime_error("CreateEventW failed");
    }

    CoreManager::~CoreManager()
    {
        try
        {
            m_stopping = true;
            if (m_stopEvent) ::SetEvent(m_stopEvent.get());
            if (m_heartbeatThread.joinable()) m_heartbeatThread.join();
            if (m_waitThread.joinable()) m_waitThread.join();
        }
        catch (...)
        {
        }
    }

    std::wstring CoreManager::CoreExePath()
    {
        auto dir = ExeDir();
        auto sibling = dir + L"\\core\\openrung-core.exe";
        if (::GetFileAttributesW(sibling.c_str()) != INVALID_FILE_ATTRIBUTES)
            return sibling;
        // Dev convenience: <repo>\dist\core\openrung-core.exe from a source checkout.
        auto fallback = dir + L"\\..\\..\\..\\..\\..\\dist\\core\\openrung-core.exe";
        if (::GetFileAttributesW(fallback.c_str()) != INVALID_FILE_ATTRIBUTES)
            return fallback;
        throw std::runtime_error(
            "openrung-core.exe not found (looked in: " + WideToUtf8(sibling) +
            "); 移动或安装时请保留程序目录中的 core 子目录");
    }

    CoreApiClient& CoreManager::EnsureRunning(bool elevated)
    {
        // Serialize the whole probe/adopt/spawn sequence: callers race (app
        // startup thread vs. first relay load), and a second caller seeing a
        // live process but no m_api yet (spawn in flight) used to spawn a
        // rival core, which the contract's single-instance check then killed
        // (exit code 2) — surfacing as "core startup failed" while the first
        // core ran fine and was adopted seconds later.
        std::lock_guard ensure(m_ensureLock);
        {
            std::lock_guard lock(m_gate);
            if (m_stopping)
                throw std::runtime_error("core is shutting down");
            bool haveLiveProc = m_process &&
                ::WaitForSingleObject(m_process.get(), 0) == WAIT_TIMEOUT;
            if (!haveLiveProc)
            {
                auto adopted = TryAdopt(elevated);
                if (adopted)
                {
                    m_endpoint = std::move(adopted);
                    m_api = std::make_unique<CoreApiClient>(m_endpoint->port, m_endpoint->token);
                    m_api->ExpectedPid = m_endpoint->pid;
                    AppLog::Write(L"core already running, adopted: 127.0.0.1:" +
                        std::to_wstring(m_endpoint->port) + L" (PID " +
                        std::to_wstring(m_endpoint->pid) + L")");
                    StartHeartbeatLocked();
                    return *m_api;
                }
            }
            else if (m_api)
            {
                return *m_api;
            }
            else
            {
                // Live process without m_api: a previous spawn failed between
                // CreateProcess and endpoint adoption. Kill the orphan so the
                // adopt/spawn below starts clean instead of fighting the
                // contract's single-instance check (exit code 2).
                AppLog::Write(L"killing orphaned core process from a failed spawn");
                ::TerminateProcess(m_process.get(), 1);
                ::WaitForSingleObject(m_process.get(), 2000);
                m_process.reset();
            }
        }

        m_stopping = false;
        if (m_stopEvent) ::ResetEvent(m_stopEvent.get());

        {
            std::lock_guard lock(m_gate);
            Spawn(elevated);
        }

        auto ep = WaitForEndpoint();
        std::lock_guard lock(m_gate);
        m_endpoint = ep;
        m_api = std::make_unique<CoreApiClient>(ep.port, ep.token);
        m_api->ExpectedPid = ep.pid;
        AppLog::Write(std::wstring(elevated ? L"core restarted elevated: 127.0.0.1:"
                                            : L"core started: 127.0.0.1:") +
            std::to_wstring(ep.port) + L" (PID " + std::to_wstring(ep.pid) + L")");
        StartHeartbeatLocked();
        return *m_api;
    }

    std::optional<CoreManager::Endpoint> CoreManager::TryAdopt(bool requireElevated)
    {
        auto ep = ReadEndpointFile();
        if (!ep) return std::nullopt;
        try
        {
            CoreApiClient probe(ep->port, ep->token);
            auto version = probe.GetVersion();
            if (requireElevated && !version.elevated)
            {
                // Refuse the adoption: the caller asked for the elevated
                // restart, and re-adopting the unprivileged core would loop
                // the 428 dialog forever without ever spawning elevated.
                AppLog::Write(L"running core (PID " + std::to_wstring(ep->pid) +
                    L") is not elevated; respawning elevated instead");
                return std::nullopt;
            }
            if (ProcessAlive(ep->pid))
                return ep;
        }
        catch (...)
        {
            // stale endpoint file; fall through to spawn
        }
        return std::nullopt;
    }

    void CoreManager::Spawn(bool elevated)
    {
        auto exePath = CoreExePath();
        auto workingDir = exePath.substr(0, exePath.find_last_of(L"\\/"));
        if (workingDir.empty()) workingDir = L".";

        if (elevated)
        {
            SHELLEXECUTEINFO sei{};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.lpVerb = L"runas";
            sei.lpFile = exePath.c_str();
            sei.lpParameters = L"serve --heartbeat-timeout 45s";
            sei.lpDirectory = workingDir.c_str();
            sei.nShow = SW_HIDE;
            if (!::ShellExecuteExW(&sei))
            {
                DWORD err = ::GetLastError();
                if (err == ERROR_CANCELLED)
                    throw std::runtime_error("elevation cancelled by user");
                throw std::runtime_error("failed to launch elevated core (Win32 error " +
                    std::to_string(err) + ")");
            }
            m_process.reset(sei.hProcess);
            return;
        }

        // Redirect the core's stdout/stderr into pipes and drain them so the
        // pipe buffers never fill and block the child. The engine's own lines
        // reach the user via the SSE /api/events stream, not this pipe.
        HANDLE childOutRead = nullptr, childOutWrite = nullptr;
        HANDLE childErrRead = nullptr, childErrWrite = nullptr;
        if (!::CreatePipe(&childOutRead, &childOutWrite, nullptr, 0) ||
            !::CreatePipe(&childErrRead, &childErrWrite, nullptr, 0) ||
            !::SetHandleInformation(childOutWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) ||
            !::SetHandleInformation(childErrWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
        {
            if (childOutRead) ::CloseHandle(childOutRead);
            if (childOutWrite) ::CloseHandle(childOutWrite);
            if (childErrRead) ::CloseHandle(childErrRead);
            if (childErrWrite) ::CloseHandle(childErrWrite);
            throw std::runtime_error("CreatePipe failed for core stdio");
        }

        SetNoProxyEnv();
        std::wstring args = L"serve --heartbeat-timeout 45s";
        std::wstring cmd = L"\"" + exePath + L"\" " + args;

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.hStdOutput = childOutWrite;
        si.hStdError = childErrWrite;
        si.dwFlags = STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi{};
        if (!::CreateProcessW(exePath.c_str(), const_cast<wchar_t*>(cmd.c_str()),
                nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                nullptr, workingDir.c_str(), &si, &pi))
        {
            DWORD err = ::GetLastError();
            ::CloseHandle(pi.hProcess);
            ::CloseHandle(pi.hThread);
            ::CloseHandle(childOutRead);
            ::CloseHandle(childOutWrite);
            ::CloseHandle(childErrRead);
            ::CloseHandle(childErrWrite);
            throw std::runtime_error("CreateProcessW failed for core (Win32 error " +
                std::to_string(err) + ")");
        }
        ::CloseHandle(childOutWrite);
        ::CloseHandle(childErrWrite);
        ::CloseHandle(pi.hThread);
        m_process.reset(pi.hProcess);

        std::thread([this](HANDLE r) { DrainPipe(r); }, childOutRead).detach();
        std::thread([this](HANDLE r) { DrainPipe(r); }, childErrRead).detach();
    }

    CoreManager::Endpoint CoreManager::WaitForEndpoint()
    {
        // The core writes the file within milliseconds; allow time for slow AV
        // scans on the freshly written exe.
        auto deadline = std::chrono::steady_clock::now() + StartupTimeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (m_stopping) throw std::runtime_error("core startup cancelled");

            {
                std::lock_guard lock(m_gate);
                if (m_process && ::WaitForSingleObject(m_process.get(), 0) == WAIT_OBJECT_0)
                {
                    DWORD code = 0;
                    ::GetExitCodeProcess(m_process.get(), &code);
                    throw std::runtime_error("core exited during startup (code " +
                        std::to_string(code) + ")");
                }
            }

            if (auto ep = ReadEndpointFile())
            {
                try
                {
                    CoreApiClient probe(ep->port, ep->token);
                    probe.GetVersion();
                    return *ep;
                }
                catch (...)
                {
                    // file written before the listener accepts; retry
                }
            }
            ::Sleep(150);
        }
        throw std::runtime_error("core did not publish its endpoint file in time");
    }

    std::optional<CoreManager::Endpoint> CoreManager::ReadEndpointFile()
    {
        try
        {
            auto path = AppSettings::EndpointFilePath();
            HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return std::nullopt;
            LARGE_INTEGER size{};
            if (!::GetFileSizeEx(file, &size) || size.QuadPart > 4096)
            {
                ::CloseHandle(file);
                return std::nullopt;
            }
            std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
            DWORD read = 0;
            ::ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
            ::CloseHandle(file);
            bytes.resize(read);

            auto wide = Utf8ToWide(bytes);
            JsonObject obj;
            if (!JsonObject::TryParse(wide, obj)) return std::nullopt;

            auto portVal = obj.TryLookup(L"port");
            auto tokenVal = obj.TryLookup(L"token");
            auto pidVal = obj.TryLookup(L"pid");
            if (!portVal || !tokenVal || !pidVal) return std::nullopt;
            if (portVal.ValueType() != JsonValueType::Number ||
                tokenVal.ValueType() != JsonValueType::String ||
                pidVal.ValueType() != JsonValueType::Number)
                return std::nullopt;
            int port = static_cast<int>(portVal.GetNumber());
            auto token = tokenVal.GetString();
            int pid = static_cast<int>(pidVal.GetNumber());
            if (port == 0 || token.empty()) return std::nullopt;

            Endpoint ep;
            ep.port = static_cast<unsigned short>(port);
            ep.token = std::wstring(token);
            ep.pid = pid;
            return ep;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool CoreManager::ProcessAlive(int pid)
    {
        HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
            static_cast<DWORD>(pid));
        if (!process) return false;
        DWORD code = 0;
        bool alive = ::GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
        ::CloseHandle(process);
        return alive;
    }

    void CoreManager::DrainPipe(HANDLE readEnd)
    {
        try
        {
            char buf[4096];
            DWORD n = 0;
            while (::ReadFile(readEnd, buf, sizeof(buf), &n, nullptr) && n > 0)
            {
                // drain only
            }
        }
        catch (...)
        {
        }
        ::CloseHandle(readEnd);
    }

    void CoreManager::StartHeartbeatLocked()
    {
        if (m_heartbeatThread.joinable())
        {
            ::SetEvent(m_stopEvent.get());
            m_heartbeatThread.join();
        }
        if (m_waitThread.joinable())
            m_waitThread.join();
        if (m_exitEvent) ::ResetEvent(m_exitEvent.get());
        m_exitedSignaled = false;

        m_heartbeatThread = std::thread([this] { HeartbeatLoop(); });

        // Monitor the process handle for an unexpected death. The parent's
        // handle to a (possibly elevated) child is valid, so a single wait
        // covers both spawn paths.
        m_waitThread = std::thread([this] {
            HANDLE proc = nullptr;
            {
                std::lock_guard lock(m_gate);
                if (m_process) proc = m_process.get();
            }
            if (!proc) return;

            HANDLE waitSet[2] = { proc, m_exitEvent.get() };
            auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(120);
            for (;;)
            {
                if (m_stopping) return;
                auto result = ::WaitForMultipleObjects(2, waitSet, FALSE, 500);
                if (result == WAIT_OBJECT_0) // process handle signalled
                    break;
                if (result == WAIT_OBJECT_0 + 1) // heartbeat already raised it
                    break;
                if (std::chrono::steady_clock::now() > deadline)
                    return;
            }
            RaiseCoreExited();
        });
    }

    void CoreManager::HeartbeatLoop()
    {
        auto lastBeat = std::chrono::steady_clock::now();
        for (;;)
        {
            DWORD wait = ::WaitForSingleObject(m_stopEvent.get(), 500);
            if (wait == WAIT_OBJECT_0) break;

            if (std::chrono::steady_clock::now() - lastBeat < HeartbeatInterval)
                continue;

            bool beatDue = false;
            {
                std::lock_guard lock(m_gate);
                if (m_stopping) break;
                if (m_api) beatDue = true;
            }
            if (!beatDue) continue;

            bool gotResponse = true;
            {
                std::lock_guard lock(m_gate);
                if (!m_api) { break; }
                try
                {
                    m_api->Heartbeat();
                }
                catch (CoreApiException const& ex)
                {
                    // The core ANSWERED — an HTTP status error is not a dead
                    // core. Keep the watchdog alive and retry next tick.
                    AppLog::Write(L"heartbeat HTTP error: " + ex.WideMessage());
                }
                catch (Http::TransportError const& ex)
                {
                    gotResponse = false;
                    AppLog::Write(L"heartbeat transport error: " + Utf8ToWide(ex.what()));
                }
                catch (std::exception const& ex)
                {
                    gotResponse = false;
                    AppLog::Write(L"heartbeat failed: " + Utf8ToWide(ex.what()));
                }
            }

            if (!gotResponse)
            {
                {
                    std::lock_guard lock(m_gate);
                    if (m_stopping) break;
                }
                RaiseCoreExited();
                break;
            }
        }
    }

    void CoreManager::RaiseCoreExited()
    {
        if (m_stopping) return;
        if (!m_exitedSignaled.exchange(true))
        {
            AppLog::Write(L"core exited unexpectedly");
            if (m_exitEvent) ::SetEvent(m_exitEvent.get());
            if (CoreExited) CoreExited();
        }
    }

    void CoreManager::Stop()
    {
        // Serialize against EnsureRunning: a spawn/adopt in flight must
        // finish and then be shut down here. Without this, quitting while
        // the startup path was still spawning left a freshly spawned core
        // running after the app exited (m_stopping was even reset to false
        // mid-teardown).
        std::lock_guard ensure(m_ensureLock);
        m_stopping = true;
        if (m_stopEvent) ::SetEvent(m_stopEvent.get());

        if (m_api)
        {
            try
            {
                m_api->Shutdown();
            }
            catch (...)
            {
                // core already gone
            }
        }

        HANDLE proc = nullptr;
        {
            std::lock_guard lock(m_gate);
            if (m_process) proc = m_process.get();
        }
        if (proc && ::WaitForSingleObject(proc, 6000) == WAIT_TIMEOUT)
        {
            ::TerminateProcess(proc, 1);
            ::WaitForSingleObject(proc, 2000);
        }

        // Adopted cores have no process handle; the graceful shutdown above
        // usually works, but if it does not, the still-running unprivileged
        // core would be re-adopted by the next EnsureRunning and the TUN
        // elevation restart would silently never happen. Wait on the PID,
        // then hard-kill.
        int adoptedPid = 0;
        {
            std::lock_guard lock(m_gate);
            if (!m_process && m_endpoint) adoptedPid = m_endpoint->pid;
        }
        if (adoptedPid)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            while (ProcessAlive(adoptedPid) && std::chrono::steady_clock::now() < deadline)
                ::Sleep(100);
            if (ProcessAlive(adoptedPid))
            {
                if (HANDLE hard = ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE,
                        static_cast<DWORD>(adoptedPid)))
                {
                    AppLog::Write(L"core shutdown timed out; terminating PID " +
                        std::to_wstring(adoptedPid));
                    ::TerminateProcess(hard, 1);
                    ::WaitForSingleObject(hard, 2000);
                    ::CloseHandle(hard);
                }
            }
        }

        if (m_heartbeatThread.joinable()) m_heartbeatThread.join();
        if (m_waitThread.joinable()) m_waitThread.join();

        {
            std::lock_guard lock(m_gate);
            m_api.reset();
            m_endpoint.reset();
            m_process.reset();
        }
        m_exitedSignaled = false;
        AppLog::Write(L"core stopped");
    }

    void CoreManager::Restart()
    {
        Stop();
        // Stop() leaves m_stopping set so no stray EnsureRunning can spawn a
        // core behind a teardown; an explicit restart is the one caller that
        // must start again, so clear it here. Without this, EnsureRunning's
        // "core is shutting down" guard threw forever and every later connect
        // failed until the app was restarted.
        m_stopping = false;
        EnsureRunning(false);
    }

    void CoreManager::RestartElevated()
    {
        Stop();
        m_stopping = false;
        EnsureRunning(true);
    }

    bool CoreManager::IsCoreRunning() const
    {
        std::lock_guard lock(m_gate);
        if (m_process)
            return ::WaitForSingleObject(m_process.get(), 0) == WAIT_TIMEOUT;
        // Adopted core: no process handle; fall back to PID liveness.
        return m_endpoint.has_value() && ProcessAlive(m_endpoint->pid);
    }

    std::optional<int> CoreManager::EndpointPid() const
    {
        std::lock_guard lock(m_gate);
        return m_endpoint ? std::optional<int>(m_endpoint->pid) : std::nullopt;
    }

    std::optional<unsigned short> CoreManager::EndpointPort() const
    {
        std::lock_guard lock(m_gate);
        return m_endpoint ? std::optional<unsigned short>(m_endpoint->port) : std::nullopt;
    }

    std::optional<std::wstring> CoreManager::EndpointToken() const
    {
        std::lock_guard lock(m_gate);
        return m_endpoint ? std::optional<std::wstring>(m_endpoint->token) : std::nullopt;
    }
}
