#pragma once
#include "pch.h"

namespace Services
{
    /// Pre-UI startup diagnostics. Writes timestamped lines both to the log
    /// file %LOCALAPPDATA%\OpenRung\logs\startup.log and to the parent
    /// process console (so `OpenRung.WinUI.exe` run from PowerShell shows the
    /// whole startup trace inline). Never throws; usable before the UI
    /// dispatcher exists (unlike AppLog, which needs it to reach the logs
    /// page). Every call is synchronous and flushed immediately: if the
    /// process fail-fasts (0xC000027B), the last written line marks the exact
    /// stage that died.
    class StartupLog
    {
    public:
        /// Attach the parent console (best-effort) and open the log file.
        /// Safe to call more than once; later calls are no-ops.
        static void Init();

        /// One timestamped line to console + file.
        static void Write(std::wstring const& line);
        static void Write(std::string const& line);

        /// Format an exception for logging: "0x%08X <message>".
        static std::wstring Describe(winrt::hresult_error const& ex);
    };
}
