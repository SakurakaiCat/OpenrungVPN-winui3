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

        int m_storeKey = 0;
        int m_stateKey = 0;
        bool m_suppressSelection = false;
        // Click-to-busy bridge: set when the user initiates connect/disconnect
        // and cleared once the core's state stream reports a busy/terminal
        // status (or after a timeout, if the core never took over).
        std::optional<std::chrono::steady_clock::time_point> m_pendingSince;
        StateUi::Lifetime m_lifetime;
    };
}

namespace winrt::OpenRung::WinUI::factory_implementation
{
    struct HomePage : HomePageT<HomePage, implementation::HomePage>
    {
    };
}
