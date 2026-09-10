#pragma once
#include "pch.h"
#include "SettingsPage.g.h"
#include "StateUi.h"
#include "../Services/UpdateCheck.h"

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
        void CheckUpdate_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void LanguageCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void CoreStart_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void CoreStop_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void CoreRestart_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        /// Last update-check outcome, so a language switch can re-render the
        /// status line from the table instead of stale Chinese literals.
        enum class UpdateStatus
        {
            None,
            Checking,
            Failed,
            UpToDate,
            Found,
        };

        void OnLoaded();
        void ApplyStrings();
        void RenderSystemProxy();
        void RenderCoreManagerStatus();
        void PersistAutoClear(bool value);
        void ClearNow(std::wstring const& reason);
        void SetComboIndex(int index);
        void ShowDialog(std::wstring const& title, std::wstring const& content,
            std::wstring const& primary, std::wstring const& close);
        void ShowUpdateDialog(Services::UpdateInfo const& update);
        void OfferElevation(std::wstring const& why);
        bool ComboModeIsTun();

        bool m_suppressSelection = false;
        bool m_suppressAutoClear = false;
        bool m_suppressLanguage = false;
        bool m_proxyBusy = false;
        bool m_updateBusy = false;
        bool m_coreBusy = false;
        UpdateStatus m_lastUpdateStatus = UpdateStatus::None;
        std::wstring m_lastUpdateTag;
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
