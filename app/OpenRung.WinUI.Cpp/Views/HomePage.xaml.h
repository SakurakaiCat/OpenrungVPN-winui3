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
        void OnStoreChanged();
        void RenderState();
        void OfferElevatedRestart(std::wstring const& why);
        void ShowError(std::wstring const& message);

        int m_storeKey = 0;
        int m_stateKey = 0;
        bool m_suppressSelection = false;
        StateUi::Lifetime m_lifetime;
    };
}

namespace winrt::OpenRung::WinUI::factory_implementation
{
    struct HomePage : HomePageT<HomePage, implementation::HomePage>
    {
    };
}
