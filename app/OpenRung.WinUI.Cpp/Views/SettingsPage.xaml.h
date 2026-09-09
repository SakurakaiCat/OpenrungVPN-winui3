#pragma once
#include "pch.h"
#include "SettingsPage.g.h"
#include "StateUi.h"

namespace winrt::OpenRung::WinUI::implementation
{
    /// Preferences: capture mode, system-proxy auto-clear, core info. Port of
    /// the C# SettingsPage (both view model and code-behind).
    struct SettingsPage : SettingsPageT<SettingsPage>
    {
        SettingsPage();
        ~SettingsPage();

        void ModeCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void AutoClearProxy_Checked(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void AutoClearProxy_Unchecked(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ClearProxy_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        void OnLoaded();
        void RenderSystemProxy();
        void PersistAutoClear(bool value);
        void ClearNow(std::wstring const& reason);
        void SetComboIndex(int index);
        void ShowDialog(std::wstring const& title, std::wstring const& content,
            std::wstring const& primary, std::wstring const& close);
        void OfferElevation(std::wstring const& why);
        bool ComboModeIsTun();

        bool m_suppressSelection = false;
        bool m_suppressAutoClear = false;
        bool m_proxyBusy = false;
        int m_stateKey = 0;
        winrt::event_token m_loadedToken{};
        StateUi::Lifetime m_lifetime;
    };
}

namespace winrt::OpenRung::WinUI::factory_implementation
{
    struct SettingsPage : SettingsPageT<SettingsPage, implementation::SettingsPage>
    {
    };
}
