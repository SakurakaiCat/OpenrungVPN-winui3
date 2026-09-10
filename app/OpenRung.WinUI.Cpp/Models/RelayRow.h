#pragma once
#include "pch.h"
#include "RelayRow.g.h"
#include "../Models/Dto.h"
#include "../Services/Localization.h"

namespace winrt::OpenRung::WinUI::implementation
{
    /// One row in the relay list (home card + servers page share the shape).
    /// Plain data holder; x:Bind OneTime reads these at realize time.
    struct RelayRow : RelayRowT<RelayRow>
    {
        RelayRow() = default;

        winrt::hstring Id() const { return m_id; }
        void Id(winrt::hstring const& value) { m_id = value; }

        winrt::hstring Label() const { return m_label; }
        void Label(winrt::hstring const& value) { m_label = value; }

        winrt::hstring DisplayTitle() const { return m_displayTitle; }
        void DisplayTitle(winrt::hstring const& value) { m_displayTitle = value; }

        winrt::hstring NodeClass() const { return m_nodeClass; }
        void NodeClass(winrt::hstring const& value) { m_nodeClass = value; }

        winrt::hstring LatencyText() const { return m_latencyText; }
        void LatencyText(winrt::hstring const& value) { m_latencyText = value; }

        winrt::hstring TestText() const { return m_testText; }
        void TestText(winrt::hstring const& value) { m_testText = value; }

        winrt::Microsoft::UI::Xaml::Media::ImageSource FlagImage() const { return m_flagImage; }
        void FlagImage(winrt::Microsoft::UI::Xaml::Media::ImageSource const& value) { m_flagImage = value; }

    private:
        winrt::hstring m_id;
        winrt::hstring m_label;
        winrt::hstring m_displayTitle;
        winrt::hstring m_nodeClass;
        winrt::hstring m_latencyText;
        winrt::hstring m_testText;
        winrt::Microsoft::UI::Xaml::Media::ImageSource m_flagImage{ nullptr };
    };
}

namespace winrt::OpenRung::WinUI::factory_implementation
{
    struct RelayRow : RelayRowT<RelayRow, implementation::RelayRow>
    {
    };
}

namespace StateUi
{
    /// Build one bindable row from a parsed relay. DisplayTitle falls back to
    /// the raw label then the id, mirroring the C# RelayItem.DisplayTitle.
    inline winrt::OpenRung::WinUI::RelayRow MakeRelayRow(Services::RelayInfo const& relay)
    {
        winrt::OpenRung::WinUI::RelayRow row = winrt::make<winrt::OpenRung::WinUI::implementation::RelayRow>();
        row.Id(winrt::hstring(relay.id));
        row.Label(winrt::hstring(relay.label));
        row.DisplayTitle(winrt::hstring(relay.label.empty() ? relay.id : relay.label));
        row.NodeClass(winrt::hstring(relay.nodeClass));
        row.LatencyText(winrt::hstring(
            relay.latencyMs ? std::to_wstring(*relay.latencyMs) + L" ms"
                            : Services::I18n::Tr(L"row.notTested")));
        // Client-side measurements: TCPing (handshake) and real delay
        // (through-tunnel generate_204). -1 marks a failed rung.
        std::wstring test;
        if (relay.tcpingMs)
            test += (*relay.tcpingMs >= 0
                ? L"TCPing " + std::to_wstring(*relay.tcpingMs) + L" ms"
                : Services::I18n::Tr(L"row.tcpingFailed"));
        if (relay.realMs)
        {
            if (!test.empty()) test += L" · ";
            test += (*relay.realMs >= 0
                ? Services::I18n::Tr(L"row.realDelay") + L" " + std::to_wstring(*relay.realMs) + L" ms"
                : Services::I18n::Tr(L"row.realDelayFailed"));
        }
        row.TestText(winrt::hstring(test));
        // Bundled flag images from the public-domain flagcdn set; Windows has
        // no flag-glyph emoji, so the list binds real images.
        if (relay.countryCode.size() == 2)
        {
            try
            {
                auto image = winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage(
                    winrt::Windows::Foundation::Uri(
                        L"ms-appx:///Assets/Flags/" + relay.countryCode + L".png"));
                row.FlagImage(image);
            }
            catch (...)
            {
            }
        }
        return row;
    }
}
