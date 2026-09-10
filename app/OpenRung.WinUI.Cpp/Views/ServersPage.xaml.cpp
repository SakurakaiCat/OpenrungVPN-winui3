#include "pch.h"
#include "ServersPage.xaml.h"
#if __has_include("ServersPage.g.cpp")
#include "ServersPage.g.cpp"
#endif

#include "../Models/Dto.h"
#include "../Services/Localization.h"
#include "../Services/RelayDirectory.h"
#include "../Services/CoreSupervisor.h"
#include "StateUi.h"
#include "../Models/RelayRow.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Services;

namespace winrt::OpenRung::WinUI::implementation
{
    ServersPage::ServersPage()
    {
        InitializeComponent();

        // Lifetime-guarded: in-flight store notifications must not touch a
        // page that navigation has already destroyed.
        Services::RelayStore::Instance().AddListener(&m_storeKey, [this, weak = m_lifetime.Weak()] {
            if (!StateUi::Lifetime::Live(weak)) return;
            OnStoreChanged();
        });
        OnStoreChanged();
        ApplyStrings();
    }

    ServersPage::~ServersPage()
    {
        m_lifetime.End();
        Services::RelayStore::Instance().RemoveListener(&m_storeKey);
    }

    void ServersPage::OnNavigatedTo(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e)
    {
        __super::OnNavigatedTo(e);
        if (Services::RelayStore::Instance().Relays().empty())
            Reload();
    }

    void ServersPage::Refresh_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        Reload();
    }

    void ServersPage::AutoSelect_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        Services::RelayDirectory::SelectLowestLatency();
    }

    void ServersPage::ApplyStrings()
    {
        UpdateRemoteButton().Content(box_value(winrt::hstring(I18n::Tr(L"servers.updateRemote"))));
        ToolTipService::SetToolTip(UpdateRemoteButton(), box_value(winrt::hstring(I18n::Tr(L"home.refreshTip"))));
        AutoSelectToolbarButton().Content(box_value(winrt::hstring(I18n::Tr(L"servers.autoSelect"))));
        TcpingButton().Content(box_value(winrt::hstring(I18n::Tr(L"servers.tcping"))));
        RealDelayButton().Content(box_value(winrt::hstring(I18n::Tr(L"servers.realDelay"))));
        TestHint().Text(I18n::Tr(L"servers.testHint"));
        ErrorBar().Title(winrt::hstring(I18n::Tr(L"servers.errorTitle")));
    }

    void ServersPage::Tcping_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        auto& store = Services::RelayStore::Instance();
        if (store.Testing())
            return; // busy gating: no racing double-runs
        std::vector<std::wstring> ids;
        for (auto const& relay : store.Relays())
            ids.push_back(relay.id);
        if (ids.empty())
            return;

        store.SetTesting(true);
        store.SetTestStatus(I18n::Tr(L"servers.tcpingRunning"));
        // Worker thread touches only the singleton store and the API client —
        // safe if the page is navigated away mid-test.
        std::thread([ids] {
            auto& store = Services::RelayStore::Instance();
            try
            {
                auto& api = Services::CoreSupervisor::Instance().Core().EnsureRunning(false);
                auto results = api.Tcping(ids);
                for (auto const& result : results)
                    store.ApplyLatency(result.relayId, result.avgMs ? *result.avgMs : -1L,
                        std::nullopt);
                int ok = 0;
                for (auto const& result : results)
                    if (result.avgMs) ++ok;
                store.SetTestStatus(I18n::Tr(L"servers.tcpingDone",
                    std::to_wstring(ok), std::to_wstring(results.size())));
            }
            catch (std::exception const& ex)
            {
                store.SetTestStatus(I18n::Tr(L"servers.tcpingFailed",
                    Services::Utf8ToWide(ex.what())));
            }
            store.SetTesting(false);
        }).detach();
    }

    void ServersPage::RealDelay_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        auto& store = Services::RelayStore::Instance();
        if (store.Testing())
            return;
        auto relays = store.Relays();
        if (relays.empty())
            return;

        try
        {
            auto& api = Services::CoreSupervisor::Instance().Core().EnsureRunning(false);
            auto state = api.GetState();
            if (state.status != L"disconnected")
            {
                store.SetTestStatus(I18n::Tr(L"servers.realDelayNeedDisconnect", state.status));
                return;
            }
            if (state.mode == L"tun")
            {
                store.SetTestStatus(I18n::Tr(L"servers.realDelayTunBlocked"));
                return;
            }
        }
        catch (std::exception const&)
        {
            // The per-relay loop below surfaces core boot failures.
        }

        store.SetTesting(true);
        store.SetTestStatus(I18n::Tr(L"servers.realDelayRunning"));
        std::thread([relays] {
            auto& store = Services::RelayStore::Instance();
            int total = static_cast<int>(relays.size());
            int done = 0;
            int failures = 0;
            std::wstring firstError;
            try
            {
                auto& api = Services::CoreSupervisor::Instance().Core().EnsureRunning(false);
                for (auto const& relay : relays)
                {
                    if (!store.Testing())
                        return; // page is gone or a refresh reset the run
                    store.SetTestStatus(I18n::Tr(L"servers.realDelayProgress",
                        std::to_wstring(done + 1), std::to_wstring(total),
                        Services::RelayDirectory::DisplayTitleOf(relay)));
                    auto result = api.RealDelay(relay.id);
                    if (result.ms)
                    {
                        store.ApplyLatency(result.relayId, std::nullopt, result.ms);
                    }
                    else
                    {
                        if (firstError.empty())
                            firstError = result.error;
                        store.ApplyLatency(relay.id, std::nullopt, -1L);
                        ++failures;
                    }
                    ++done;
                }
            }
            catch (std::exception const& ex)
            {
                if (firstError.empty())
                    firstError = Services::Utf8ToWide(ex.what());
                ++failures;
            }
            std::wstring status = I18n::Tr(L"servers.realDelayDone",
                std::to_wstring(total - failures), std::to_wstring(total));
            if (!firstError.empty())
                status += I18n::Tr(L"servers.firstFailure", firstError);
            store.SetTestStatus(status);
            store.SetTesting(false);
        }).detach();
    }

    void ServersPage::RelayList_SelectionChanged(Windows::Foundation::IInspectable const&,
        SelectionChangedEventArgs const&)
    {
        if (m_suppressSelection)
            return;
        if (auto row = RelayList().SelectedItem().try_as<winrt::OpenRung::WinUI::RelayRow>())
            Services::RelayStore::Instance().SetSelectedId(std::wstring(row.Id()));
    }

    void ServersPage::Reload()
    {
        Services::RelayDirectory::UpdateFromRemote();
    }

    void ServersPage::OnStoreChanged()
    {
        auto& store = Services::RelayStore::Instance();

        auto loading = store.Loading();
        LoadingBar().IsIndeterminate(loading);
        LoadingBar().Visibility(loading ? Visibility::Visible : Visibility::Collapsed);

        auto error = store.Error();
        ErrorBar().Message(winrt::hstring(error));
        ErrorBar().IsOpen(!error.empty());

        SummaryText().Text(store.SummaryText());

        auto testStatus = store.TestStatus();
        TestStatusText().Text(winrt::hstring(testStatus));
        TestStatusText().Visibility(testStatus.empty() ? Visibility::Collapsed : Visibility::Visible);
        bool testing = store.Testing();
        TcpingButton().IsEnabled(!testing);
        RealDelayButton().IsEnabled(!testing);

        auto relays = store.Relays();
        auto selected = store.SelectedId();
        // Same bindable-vector requirement as HomePage.OnStoreChanged.
        auto rows = winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>();
        int selectedIndex = -1;
        for (size_t i = 0; i < relays.size(); ++i)
        {
            auto row = StateUi::MakeRelayRow(relays[i]);
            if (!selected.empty() && std::wstring(row.Id()) == selected)
                selectedIndex = static_cast<int>(i);
            rows.Append(std::move(row));
        }

        m_suppressSelection = true;
        RelayList().ItemsSource(rows);
        if (selectedIndex >= 0)
            RelayList().SelectedIndex(selectedIndex);
        m_suppressSelection = false;
    }
}
