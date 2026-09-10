#pragma once

#include "App.g.h"
#include "App.xaml.g.h"
#include "winrt/Microsoft.UI.Xaml.h"

namespace winrt::OpenRung::WinUI::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
        void OnUnhandledException(winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e);

        static void ShutdownAsync();

        /// The main window (may be null very early / during teardown).
        static winrt::Microsoft::UI::Xaml::Window Window();

        /// Startup prompt: TUN (the default mode) needs an elevated core.
        /// Offers the UAC restart; refusal just logs and stays in proxy mode.
        static void PromptTunElevation(std::wstring const& why);

    private:
        winrt::Microsoft::UI::Xaml::Window m_window{ nullptr };
        static inline winrt::Microsoft::UI::Xaml::Window s_window{ nullptr };
    };
}

namespace winrt::OpenRung::WinUI::factory_implementation
{
    struct App : AppT<App, implementation::App>
    {
    };
}
