#pragma once
#include "pch.h"

namespace Services
{
    /// This build's version; MUST match the published release tag format
    /// (v-prefixed semantic triple, optional -suffix).
    inline constexpr wchar_t AppVersion[] = L"v0.1.1";

    /// The GitHub release the update check looks at.
    inline constexpr wchar_t UpdateRepo[] = L"SakurakaiCat/OpenrungVPN-winui3";

    struct UpdateInfo
    {
        std::wstring tag;     // release tag, e.g. "v0.1.1"
        std::wstring pageUrl; // release HTML page on github.com
    };

    /// Queries the GitHub API for the latest release of UpdateRepo.
    /// Returns nullopt when nothing newer than AppVersion is published.
    /// Throws std::runtime_error on transport or HTTP failure.
    std::optional<UpdateInfo> CheckForUpdate();
}
