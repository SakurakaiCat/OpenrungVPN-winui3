#pragma once
#include "pch.h"
#include "LogsPage.g.h"
#include "StateUi.h"

namespace winrt::OpenRung::WinUI::implementation
{
    /// Live log tail backed by Services::LogStore. Port of the C# LogsPage.
    struct LogsPage : LogsPageT<LogsPage>
    {
        LogsPage();
        ~LogsPage();

        void Clear_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void Copy_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void AutoScrollToggle_Click(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        void ScrollToBottom();
        void SeedLogs();
        void ApplyStrings();

        winrt::event_token m_vectorToken{};
        StateUi::Lifetime m_lifetime;
    };
}

namespace winrt::OpenRung::WinUI::factory_implementation
{
    struct LogsPage : LogsPageT<LogsPage, implementation::LogsPage>
    {
    };
}
