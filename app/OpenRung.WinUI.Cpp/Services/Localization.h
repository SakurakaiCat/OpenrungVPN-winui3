#pragma once
#include "pch.h"

namespace Services
{
    /// Code-side UI string table with runtime language switching (Chinese is
    /// the source language; English is the second locale). Chosen over
    /// .resw/x:Uid because every page in this app already sets its text from
    /// code-behind and runtime switching then needs no PRI toolchain dance.
    ///
    /// Keys are dotted English identifiers; see Localization.cpp for the
    /// table. "{0}".."{2}" placeholders in values are filled by Tr(key, ...).
    namespace I18n
    {
        /// Resolves the persisted preference ("system" | "zh" | "en") into
        /// the active language. Called once from App::OnLaunched.
        void Init();

        /// Active language: L"zh" or L"en".
        std::wstring const& Language();

        /// Persisted user choice: L"system", L"zh" or L"en".
        std::wstring const& Preference();

        /// Persists the choice, re-resolves, then fires LanguageChanged.
        void SetPreference(std::wstring const& choice);

        /// Translate (falls back: active language -> Chinese -> key itself).
        std::wstring Tr(std::wstring_view key);

        /// Tr + "{n}" placeholder substitution.
        std::wstring Tr(std::wstring_view key,
            std::wstring const& a0,
            std::wstring const& a1 = {},
            std::wstring const& a2 = {});

        /// Raised (on the calling thread) after the active language changed;
        /// MainWindow re-applies its own strings and re-navigates the frame.
        inline std::function<void()> LanguageChanged;
    }
}
