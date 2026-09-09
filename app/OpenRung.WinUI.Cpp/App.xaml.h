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

    private:
        winrt::Microsoft::UI::Xaml::Window m_window{ nullptr };
    };
}

namespace winrt::OpenRung::WinUI::factory_implementation
{
    struct App : AppT<App, implementation::App>
    {
    };
}
