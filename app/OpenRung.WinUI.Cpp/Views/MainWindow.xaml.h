#pragma once
#include "pch.h"
#include "MainWindow.g.h"
#include "Services/TrayIcon.h"
#include "StateUi.h"

namespace winrt::OpenRung::WinUI::implementation
{
    /// Shell: hosts the four pages in a frame, renders the shared status bar
    /// from Services::AppState, and owns the tray icon. Closing the window
    /// hides it to the tray (the engine keeps running); the tray menu's 退出
    /// item shuts the core down and exits. Port of the C# MainWindow.
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void NavView_SelectionChanged(winrt::Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);

    private:
        void SetIcon();
        void SetupTray();
        void ShowFromTray();
        void HideToTray();
        void UpdateStatusBar();
        void QuitAsync();

        void OnWindowClosing(winrt::Microsoft::UI::Windowing::AppWindow const& sender,
            winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args);
        void OnClosed(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::WindowEventArgs const& args);

        Services::TrayIcon m_tray;
        bool m_quitting = false;
        StateUi::Lifetime m_lifetime;
        winrt::Microsoft::UI::Xaml::DispatcherTimer m_ticker{ nullptr };
        winrt::event_token m_closingToken{};
        int m_stateKey = 0;
    };
}

namespace winrt::OpenRung::WinUI::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
