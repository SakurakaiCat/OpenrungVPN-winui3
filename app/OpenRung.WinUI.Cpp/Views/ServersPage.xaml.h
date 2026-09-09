#pragma once
#include "pch.h"
#include "ServersPage.g.h"
#include "StateUi.h"

namespace winrt::OpenRung::WinUI::implementation
{
    /// Ranked relay directory with refresh + auto-select. Port of the C#
    /// ServersPage; renders from Services::RelayStore.
    struct ServersPage : ServersPageT<ServersPage>
    {
        ServersPage();
        ~ServersPage();

        void OnNavigatedTo(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e);

        void Refresh_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void AutoSelect_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Tcping_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void RealDelay_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void RelayList_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);

    private:
        void Reload();
        void OnStoreChanged();

        int m_storeKey = 0;
        bool m_suppressSelection = false;
        StateUi::Lifetime m_lifetime;
    };
}

namespace winrt::OpenRung::WinUI::factory_implementation
{
    struct ServersPage : ServersPageT<ServersPage, implementation::ServersPage>
    {
    };
}
