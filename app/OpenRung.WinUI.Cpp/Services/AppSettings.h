#pragma once
#include "pch.h"

namespace Services
{
    /// UI-side preferences, persisted to %LOCALAPPDATA%\OpenRung\app-settings.json
    /// (next to the core's own client state). The core stays unaware of these.
    struct AppSettings
    {
        /// When true, the app clears any pre-existing OS system proxy (e.g. left
        /// by another proxy client) on startup and whenever the user enables the
        /// option, instead of letting proxy mode take it over and restore it later.
        bool autoClearProxy = false;

        /// Capture mode the app keeps the core in. Defaults to TUN (full-device
        /// capture); switching to 代理模式 in Settings persists "proxy" here and
        /// silences the startup elevation prompt.
        std::wstring preferredMode = L"tun";

        /// UI language choice: "system" (default), "zh" or "en".
        std::wstring language = L"system";

        static AppSettings Load();
        void Save() const;

        /// %LOCALAPPDATA%\OpenRung (created lazily by Save / crash log).
        static std::wstring LocalDataDir();
        static std::wstring SettingsPath();
        /// core-endpoint.json discovery file written by the core.
        static std::wstring EndpointFilePath();
    };
}
