#include "pch.h"
#include "HomePage.xaml.h"
#if __has_include("HomePage.g.cpp")
#include "HomePage.g.cpp"
#endif

#include "../Models/Dto.h"
#include "../Services/AppLog.h"
#include "../Services/StartupLog.h"
#include "../Services/AppState.h"
#include "../Services/CoreApiClient.h"
#include "../Services/CoreSupervisor.h"
#include "../Services/Localization.h"
#include "../Services/RelayDirectory.h"
#include "StateUi.h"
#include "../Models/RelayRow.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Services;
using namespace ::StateUi;

namespace winrt::OpenRung::WinUI::implementation
{
    HomePage::HomePage()
    {
        Services::StartupLog::Write("HomePage enter");
        InitializeComponent();
        Services::StartupLog::Write("HomePage InitializeComponent done");

        // Listener copies live in in-flight store notifications; the lifetime
        // token makes them no-ops once this page is gone.
        Services::RelayStore::Instance().AddListener(&m_storeKey, [this, weak = m_lifetime.Weak()] {
            if (!StateUi::Lifetime::Live(weak)) return;
            OnStoreChanged();
        });
        Services::AppState::Instance().AddListener(&m_stateKey, [this, weak = m_lifetime.Weak()] {
            if (!StateUi::Lifetime::Live(weak)) return;
            RenderState();
        });
        Services::StartupLog::Write("HomePage listeners done");

        ApplyStrings();

        Services::StartupLog::Write("HomePage OnStoreChanged begin");
        OnStoreChanged();
        Services::StartupLog::Write("HomePage OnStoreChanged done");
        Services::StartupLog::Write("HomePage RenderState begin");
        RenderState();
        Services::StartupLog::Write("HomePage done");
    }

    HomePage::~HomePage()
    {
        m_lifetime.End();
        Services::RelayStore::Instance().RemoveListener(&m_storeKey);
        Services::AppState::Instance().RemoveListener(&m_stateKey);
    }

    void HomePage::ApplyStrings()
    {
        NodeHeader().Text(I18n::Tr(L"home.nodeHeader"));
        ToolTipService::SetToolTip(AutoSelectButton(), box_value(winrt::hstring(I18n::Tr(L"home.autoSelectTip"))));
        ToolTipService::SetToolTip(RefreshCardButton(), box_value(winrt::hstring(I18n::Tr(L"home.refreshTip"))));
        BootingText().Text(I18n::Tr(L"home.booting"));
        ErrorBar().Title(winrt::hstring(I18n::Tr(L"home.errorTitle")));
    }

    void HomePage::OnNavigatedTo(winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& e)
    {
        __super::OnNavigatedTo(e);
        if (Services::RelayStore::Instance().Relays().empty())
            LoadRelays();
    }

    // Fire-and-forget load: the home card surfaces failures inline
    // (RelayStore::Error) instead of a dialog, so no XamlRoot is needed.
    void HomePage::LoadRelays()
    {
        Services::RelayDirectory::UpdateFromRemote();
    }

    void HomePage::Refresh_Click(Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        LoadRelays();
    }

    void HomePage::AutoSelect_Click(Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        Services::RelayDirectory::SelectLowestLatency();
    }

    void HomePage::RelayList_SelectionChanged(Windows::Foundation::IInspectable const&,
        SelectionChangedEventArgs const&)
    {
        if (m_suppressSelection)
            return;
        if (auto row = RelayList().SelectedItem().try_as<winrt::OpenRung::WinUI::RelayRow>())
            Services::RelayStore::Instance().SetSelectedId(std::wstring(row.Id()));
    }

    void HomePage::OnStoreChanged()
    {
        auto& store = Services::RelayStore::Instance();

        auto loading = store.Loading();
        Services::StartupLog::Write("OnStoreChanged a");
        LoadingBar().IsIndeterminate(loading);
        LoadingBar().Visibility(loading ? Visibility::Visible : Visibility::Collapsed);
        Services::StartupLog::Write("OnStoreChanged b");

        auto error = store.Error();
        ErrorText().Text(error);
        ErrorText().Visibility(error.empty() ? Visibility::Collapsed : Visibility::Visible);
        Services::StartupLog::Write("OnStoreChanged c");

        SummaryText().Text(store.SummaryText());
        Services::StartupLog::Write("OnStoreChanged d");

        // Rebuild rows; preserve the selected id.
        auto relays = store.Relays();
        auto selected = store.SelectedId();
        // XAML items controls can only enumerate IObservableVector<IInspectable>
        // (the bindable form); a typed IObservableVector<RelayRow> fails with
        // E_INVALIDARG when assigned to ItemsSource.
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
        Services::StartupLog::Write("OnStoreChanged e");
        RelayList().ItemsSource(rows);
        Services::StartupLog::Write("OnStoreChanged f");
        if (selectedIndex >= 0)
            RelayList().SelectedIndex(selectedIndex);
        m_suppressSelection = false;
        Services::StartupLog::Write("OnStoreChanged g");
    }

    void HomePage::RenderState()
    {
        auto state = Services::AppState::Instance().Current();
        auto busy = StateUi::IsBusy(state);

        // The click precedes the core's state transition: keep the spinner up
        // from the click until the stream confirms the transition (or 10s).
        if (busy)
            m_pendingSince.reset();
        else if (m_pendingSince &&
            std::chrono::steady_clock::now() - *m_pendingSince < std::chrono::seconds(10))
            busy = true;
        else
            m_pendingSince.reset();

        auto connected = StateUi::IsConnected(state);

        Services::StartupLog::Write("RenderState 1");
        ConnectButton().IsEnabled(!busy);
        ToggleTrack().Background(StateUi::ToggleBrush(connected));
        Knob().Margin(StateUi::KnobMargin(connected));
        BusyRing().IsActive(busy);
        BusyRing().Visibility(busy ? Visibility::Visible : Visibility::Collapsed);
        Services::StartupLog::Write("RenderState 2");

        auto label = state.relayLabel.value_or(L"");
        RelayLabel().Text(label.empty() ? I18n::Tr(L"home.notConnected") : label);

        StatusLine().Text(state.status + L" · " + I18n::Tr(
            state.mode == L"tun" ? L"mode.tun" : L"mode.proxy"));

        Services::StartupLog::Write("RenderState 3");
        // Core boot hint: the startup thread clears the flag once the first
        // EnsureRunning probe/spawn has settled.
        CoreBootBar().Visibility(Services::AppState::Instance().CoreBooting()
            ? Visibility::Visible
            : Visibility::Collapsed);

        if (connected && state.connection && state.connection->startedAt)
        {
            ElapsedText().Text(StateUi::FormatElapsed(*state.connection->startedAt));
            ElapsedText().Visibility(Visibility::Visible);
        }
        else
        {
            ElapsedText().Visibility(Visibility::Collapsed);
        }
        Services::StartupLog::Write("RenderState 4");
    }

    void HomePage::ConnectButton_Click(Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        // One connect at a time: a burst of clicks queued five API calls within
        // a second (all of which then raced the state machine's transitions).
        auto state = Services::AppState::Instance().Current();
        if (StateUi::IsBusy(state) || m_pendingSince)
            return;

        auto connected = StateUi::IsConnected(state);
        auto selectedId = Services::RelayStore::Instance().SelectedId();

        if (connected)
        {
            Services::AppLog::Write(L"user clicked disconnect");
        }
        else
        {
            Services::AppLog::Write(selectedId.empty()
                ? L"user clicked connect: no node selected, auto-selecting"
                : L"user clicked connect: " + selectedId);
        }

        // Optimistic busy: the spinner starts now, not when the SSE stream
        // delivers the core's "connecting" state.
        m_pendingSince = std::chrono::steady_clock::now();
        RenderState();

        std::thread([this, weak = m_lifetime.Weak(), connected, selectedId] {
            try
            {
                auto& api = Services::CoreSupervisor::Instance().Core().EnsureRunning(false);
                if (connected)
                    api.Disconnect();
                else
                    api.Connect(L"", selectedId, L"");
            }
            catch (Services::ElevationRequiredException const& ex)
            {
                Services::Ui::Post([this, weak, msg = ex.WideMessage()] {
                    if (!StateUi::Lifetime::Live(weak))
                        return;
                    m_pendingSince.reset();
                    RenderState();
                    OfferElevatedRestart(msg);
                });
            }
            catch (Services::CoreApiException const& ex)
            {
                Services::Ui::Post([this, weak, msg = ex.WideMessage()] {
                    if (!StateUi::Lifetime::Live(weak))
                        return;
                    m_pendingSince.reset();
                    RenderState();
                    ShowError(msg);
                });
            }
            catch (std::exception const&)
            {
                // Connection-level failure (core dead / restarting): refused
                // sockets surface here, not as CoreApiException.
                Services::Ui::Post([this, weak] {
                    if (!StateUi::Lifetime::Live(weak))
                        return;
                    m_pendingSince.reset();
                    RenderState();
                    ShowError(I18n::Tr(L"dlg.connectFailed"));
                });
            }
        }).detach();
    }

    void HomePage::ShowError(std::wstring const& message)
    {
        ErrorBar().Message(winrt::hstring(message));
        ErrorBar().IsOpen(true);
    }

    // 428 path from the contract: offer to relaunch the core as Administrator,
    // then retry the same connect after the restart.
    void HomePage::OfferElevatedRestart(std::wstring const& why)
    {
        ContentDialog dialog;
        dialog.Title(box_value(winrt::hstring(I18n::Tr(L"dlg.elevTitle"))));
        dialog.Content(box_value(winrt::hstring(I18n::Tr(L"dlg.elevBody", why))));
        dialog.PrimaryButtonText(winrt::hstring(I18n::Tr(L"dlg.elevRestartCore")));
        dialog.CloseButtonText(winrt::hstring(I18n::Tr(L"dlg.cancel")));
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.XamlRoot(XamlRoot());

        auto operation = dialog.ShowAsync();
        operation.Completed([this, weak = m_lifetime.Weak()](
                                Windows::Foundation::IAsyncOperation<ContentDialogResult> const& async,
                                Windows::Foundation::AsyncStatus status) {
            if (status != Windows::Foundation::AsyncStatus::Completed)
                return;
            if (!StateUi::Lifetime::Live(weak))
                return;
            if (async.get() != ContentDialogResult::Primary)
                return;

            auto selected = Services::RelayStore::Instance().SelectedId();
            std::thread([this, weak, selected] {
                std::wstring failure;
                try
                {
                    Services::CoreSupervisor::Instance().RestartElevated();
                    Services::CoreSupervisor::Instance().Core().EnsureRunning(false)
                        .Connect(L"", selected, L"");
                }
                catch (std::exception const& ex)
                {
                    failure = Services::Utf8ToWide(ex.what());
                }
                Services::Ui::Post([this, weak, failure] {
                    if (!StateUi::Lifetime::Live(weak))
                        return;
                    if (!failure.empty())
                        ShowError(failure);
                });
            }).detach();
        });
    }
}
