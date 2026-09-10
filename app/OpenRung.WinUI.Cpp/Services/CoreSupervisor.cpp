#include "pch.h"
#include "CoreSupervisor.h"
#include "AppLog.h"
#include "AppSettings.h"
#include "Localization.h"
#include "RelayDirectory.h"
#include "../Models/Dto.h"

using namespace winrt::Windows::Data::Json;
using namespace Services;

namespace Services
{
    namespace
    {
        constexpr auto kMaxBackoff = std::chrono::seconds(10);

        std::wstring FormatSeconds(std::chrono::milliseconds ms)
        {
            // 0.# format: one decimal place, drop the trailing zero.
            auto tenths = static_cast<long long>(ms.count() / 100);
            if (tenths % 10 == 0) return std::to_wstring(tenths / 10) + L"0";
            return std::to_wstring(tenths / 10) + L"." + std::to_wstring(tenths % 10);
        }
    }

    CoreSupervisor& CoreSupervisor::Instance()
    {
        static CoreSupervisor instance;
        return instance;
    }

    CoreSupervisor::CoreSupervisor() = default;

    CoreSupervisor::~CoreSupervisor()
    {
        Stop();
    }

    std::optional<StateSnapshot> CoreSupervisor::LatestState() const
    {
        std::lock_guard lock(m_stateGate);
        return m_state;
    }

    void CoreSupervisor::Start()
    {
        // Idempotent: a healthy stream already running means we're done.
        if (m_streamRunning) return;

        m_disposed = false;
        m_stopStream = false;
        auto& api = m_core.EnsureRunning(false);
        // Seed state before the stream so the UI renders in one pass.
        try
        {
            RaiseState(api.GetState());
        }
        catch (...)
        {
            // stream reconnect loop will pick it up
        }
        m_streamRunning = true;
        m_streamThread = std::thread([this] { RunEventLoop(); });
    }

    void CoreSupervisor::RestartElevated()
    {
        AppLog::Write(L"restarting core elevated for TUN mode (UAC prompt expected)");
        // Stop the core first: its exit closes the SSE socket, which unblocks
        // the stream thread's read (a live-socket read would otherwise hold
        // the join until the 90s receive timeout).
        m_stopStream = true;
        m_core.RestartElevated();
        if (m_streamThread.joinable())
            m_streamThread.join();
        m_streamRunning = false;

        m_disposed = false;
        m_stopStream = false;
        m_streamRunning = true;
        m_streamThread = std::thread([this] { RunEventLoop(); });
        AppLog::Write(L"elevated core ready; event stream restarted");
    }

    void CoreSupervisor::Stop()
    {
        // A going-away core cannot serve the ladder's next dial; cancel first.
        RelayDirectory::CancelFailover(I18n::Tr(L"failover.reasonCoreStop"));
        m_disposed = true;
        m_stopStream = true;
        // Core death drops the SSE socket, unblocking the stream thread's read.
        m_core.Stop();
        if (m_streamThread.joinable())
            m_streamThread.join();
        m_streamRunning = false;
    }

    CoreSupervisor::CoreStatus CoreSupervisor::Describe() const
    {
        CoreStatus status;
        if (m_core.IsCoreRunning())
        {
            status.phase = CorePhase::Running;
            if (auto pid = m_core.EndpointPid())
                status.pid = *pid;
        }
        return status;
    }

    CoreSupervisor::ModeSync CoreSupervisor::SyncPreferredMode()
    {
        auto preferred = AppSettings::Load().preferredMode;
        if (preferred.empty())
            preferred = L"tun";

        auto& api = m_core.EnsureRunning(false);
        auto state = api.GetState();

        if (state.mode != preferred)
        {
            try
            {
                api.SetMode(preferred);
                AppLog::Write(L"mode synced to preference: " + preferred);
            }
            catch (ElevationRequiredException const& ex)
            {
                return {false, true, ex.WideMessage()};
            }
            catch (std::exception const& ex)
            {
                return {false, false, Utf8ToWide(ex.what())};
            }
        }

        // A persisted mode=tun on a non-elevated core accepts the mode but
        // fails at connect (428); require elevation up front instead.
        // Plain text, not the dlg.elevBody template: the caller feeds this
        // back into I18n::Tr as {0}, and a template-in-template used to hang
        // the UI thread in the replacement loop.
        if (preferred == L"tun" && !state.elevated)
            return {false, true, I18n::Tr(L"dlg.elevOffer")};
        return {};
    }

    void CoreSupervisor::ApplyPreferredModeElevated()
    {
        auto preferred = AppSettings::Load().preferredMode;
        RestartElevated();
        m_core.EnsureRunning(false).SetMode(preferred);
        AppLog::Write(L"core restarted elevated; mode set to " + preferred);
    }

    void CoreSupervisor::RunEventLoop()
    {
        auto delay = std::chrono::milliseconds(250);
        while (!m_stopStream && !m_disposed)
        {
            auto port = m_core.EndpointPort();
            auto token = m_core.EndpointToken();
            if (!port || !token)
            {
                // Core not (re)started yet; wait a beat and retry.
                ::Sleep(100);
                continue;
            }
            CoreEventsClient events(*port, *token);
            try
            {
                events.Run([this](EventItem const& item) { DispatchEvent(item); }, m_stopStream);
            }
            catch (...)
            {
                // transport error: fall through to reconnect
            }
            if (m_stopStream || m_disposed)
                break;
            AppLog::Write(L"event stream dropped; reconnecting in " + FormatSeconds(delay) + L"s");
            if (ConnectionLost) ConnectionLost();
            // Interruptible delay.
            auto const steps = 50;
            auto const stepMs = delay / steps;
            for (int i = 0; i < steps && !m_stopStream && !m_disposed; ++i)
                ::Sleep(std::max<DWORD>(1, stepMs.count()));
            delay = std::min<decltype(delay)>(delay * 2, std::chrono::duration_cast<std::chrono::milliseconds>(kMaxBackoff));
        }
    }

    void CoreSupervisor::DispatchEvent(EventItem const& item)
    {
        if (item.name == L"state")
        {
            auto json = Utf8ToWide(item.data);
            JsonObject obj;
            if (JsonObject::TryParse(json, obj))
                RaiseState(ParseState(obj));
        }
        else if (item.name == L"log")
        {
            auto json = Utf8ToWide(item.data);
            JsonObject obj;
            if (JsonObject::TryParse(json, obj))
            {
                LogLine line;
                auto timeVal = obj.TryLookup(L"time");
                auto lineVal = obj.TryLookup(L"line");
                if (timeVal) line.time = std::wstring{ timeVal.GetString() };
                if (lineVal) line.line = std::wstring{ lineVal.GetString() };
                if (LogReceived) LogReceived(line);
            }
        }
    }

    void CoreSupervisor::RaiseState(StateSnapshot const& state)
    {
        // One line per transition so the logs page shows the full lifecycle a
        // connect passes through (connecting -> connected / failed -> ...).
        std::optional<std::wstring> prev;
        {
            std::lock_guard lock(m_stateGate);
            if (m_state)
                prev = m_state->status;
            m_state = state;
        }
        if (prev != state.status)
        {
            std::wstring extra =
                state.lastError && !state.lastError->empty() ? L" (error: " + *state.lastError + L")" : L"";
            AppLog::Write(L"core state: " + (prev ? *prev : L"start") + L" -> " + state.status + extra);
        }
        if (StateChanged) StateChanged(state);
        RelayDirectory::OnStateForFailover(state);
    }
}
