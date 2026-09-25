#pragma once
#include "pch.h"
#include "HomePage.g.h"
#include "StateUi.h"

namespace winrt::OpenRung::WinUI::implementation
{
    /// Home: the connect toggle plus the relay directory card. Port of the
    /// C# HomePage; state renders from Services::AppState / RelayStore.
    struct HomePage : HomePageT<HomePage>
    {
        HomePage();
        ~HomePage();

        void OnNavigatedTo(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);

        void ConnectButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Refresh_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void AutoSelect_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void RelayList_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);

    private:
        void LoadRelays();
        void ApplyStrings();
        void OnStoreChanged();
        void RenderState();
        void OfferElevatedRestart(std::wstring const& why);
        void ShowError(std::wstring const& message);
        /// Smart routing: dispatch the core's auto-select connect via
        /// RelayDirectory::ConnectSmart, surfacing synchronous refusals
        /// (elevation dialog, error bar).
        void BeginSmartConnect();
        /// A regular node failed to connect: offer smart routing / re-pick.
        /// Returns false when the dialog can't be shown (no XamlRoot yet).
        bool OfferSmartFallback(std::wstring const& failedTitle);

        int m_storeKey = 0;
        int m_stateKey = 0;
        bool m_suppressSelection = false;
        // Click-to-busy bridge: set when the user initiates connect/disconnect
        // and cleared once the core's state stream reports a busy/terminal
        // status (or after a timeout, if the core never takes over).
        std::optional<std::chrono::steady_clock::time_point> m_pendingSince;
        // The direction the user's click is heading (true = connect), so the
        // optimistic render can already show the toggle's target position
        // with the spinner spinning inside the knob.
        std::optional<bool> m_pendingConnect;
        // Dedupe for the connect-failed surfaces (smart-exhausted error bar /
        // re-pick dialog): relayId|lastError, cleared when a new attempt
        // starts (busy) or connects.
        std::wstring m_promptedFailureKey;
        StateUi::Lifetime m_lifetime;
    };
}

namespace winrt::OpenRung::WinUI::factory_implementation
{
    struct HomePage : HomePageT<HomePage, implementation::HomePage>
    {
    };
}
