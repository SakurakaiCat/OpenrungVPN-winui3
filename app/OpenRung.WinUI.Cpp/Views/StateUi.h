#pragma once
#include "pch.h"
#include "../Models/Dto.h"

namespace StateUi
{
    inline bool IsConnected(Services::StateSnapshot const& s) { return s.status == L"connected"; }

    inline bool IsBusy(Services::StateSnapshot const& s)
    {
        return s.status == L"preparing" || s.status == L"connecting" || s.status == L"disconnecting";
    }

    inline wchar_t const* StatusGlyph(std::wstring const& status)
    {
        if (status == L"connected") return L"\uEA18";    // check mark
        if (status == L"failed") return L"\uEA39";       // error glyph
        if (status == L"preparing" || status == L"connecting" || status == L"disconnecting")
            return L"\uE15E";                            // sync/hourglass
        return L"\uE81E";                                // hollow circle
    }

    /// WARP-style switch track: orange while connected, gray otherwise.
    inline winrt::Microsoft::UI::Xaml::Media::SolidColorBrush ToggleBrush(bool connected)
    {
        winrt::Windows::UI::Color color = connected
            ? winrt::Windows::UI::Color{ 255, 0xFA, 0x5A, 0x28 }
            : winrt::Windows::UI::Color{ 255, 0x6B, 0x72, 0x80 };
        return winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(color);
    }

    /// Knob position inside the 220-wide track: left when off, right when on.
    inline winrt::Microsoft::UI::Xaml::Thickness KnobMargin(bool connected)
    {
        return connected
            ? winrt::Microsoft::UI::Xaml::Thickness{ 120, 0, 0, 0 }
            : winrt::Microsoft::UI::Xaml::Thickness{ 12, 0, 0, 0 };
    }

    /// "HH:MM:SS" since the connection started.
    inline std::wstring FormatElapsed(std::chrono::system_clock::time_point startedAt)
    {
        auto span = std::chrono::system_clock::now() - startedAt;
        auto h = std::chrono::duration_cast<std::chrono::hours>(span).count();
        auto m = std::chrono::duration_cast<std::chrono::minutes>(span).count() % 60;
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(span).count() % 60;
        wchar_t buf[16];
        _snwprintf_s(buf, _TRUNCATE, L"%02lld:%02d:%02d",
            static_cast<long long>(h), static_cast<int>(m), static_cast<int>(sec));
        return buf;
    }

    /// System proxy display: "" renders as 无.
    inline std::wstring ProxyDisplay(std::wstring const& systemProxy)
    {
        return systemProxy.empty() ? L"无" : systemProxy;
    }

    /// Shared page-lifetime token: worker callbacks capture a weak ref and
    /// become no-ops once the page is gone. Guards every detached-thread
    /// continuation that touches `this`.
    struct Lifetime
    {
        std::shared_ptr<std::atomic<bool>> token = std::make_shared<std::atomic<bool>>(true);

        void End() { token->store(false); }
        /// Weak copy to capture in worker lambdas.
        std::weak_ptr<std::atomic<bool>> Weak() const { return token; }
        static bool Live(std::weak_ptr<std::atomic<bool>> const& weak)
        {
            auto t = weak.lock();
            return t && t->load();
        }
    };
}
