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
        {
            Services::RelayStore::Instance().SetSelectedId(std::wstring(row.Id()));
            // Live session: selecting a different relay switches to it now.
            Services::RelayDirectory::SwitchToSelected();
        }
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
        // The smart-routing pseudo-node is pinned to the top of the list.
        rows.Append(StateUi::MakeSmartRelayRow());
        int selectedIndex = selected == Services::RelayDirectory::kSmartRelayId ? 0 : -1;
        for (size_t i = 0; i < relays.size(); ++i)
        {
            auto row = StateUi::MakeRelayRow(relays[i]);
            if (!selected.empty() && std::wstring(row.Id()) == selected)
                selectedIndex = static_cast<int>(i + 1);
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
        {
            m_pendingSince.reset();
            m_pendingConnect.reset();
        }

        auto connected = StateUi::IsConnected(state);
        auto label = state.relayLabel.value_or(L"");
        // While the user's click is in flight the toggle already shows its
        // target state — knob on the destination side, orange track for a
        // connect — with the spinner spinning inside the white knob instead
        // of over the whole track. Core-driven connecting without a click
        // (failover keeps the old label) keeps the toggle on: a mid-session
        // reconnect must not flip the switch off.
        bool switchOn = m_pendingConnect.has_value()
            ? *m_pendingConnect
            : (connected || (busy && !label.empty()));
        bool showBusyRing = busy;
        bool showConnectingText = busy && m_pendingConnect.has_value();

        Services::StartupLog::Write("RenderState 1");
        ConnectButton().IsEnabled(!busy);
        ToggleTrack().Background(StateUi::ToggleBrush(switchOn));
        Knob().Margin(StateUi::KnobMargin(switchOn));
        BusyRing().IsActive(showBusyRing);
        BusyRing().Visibility(showBusyRing ? Visibility::Visible : Visibility::Collapsed);
        Services::StartupLog::Write("RenderState 2");

        if (showConnectingText)
        {
            // "正在连接中/正在断开中" — the target text for the user's click.
            RelayLabel().Text(I18n::Tr(*m_pendingConnect
                ? L"home.connecting" : L"home.disconnecting"));
        }
        else
        {
            RelayLabel().Text(label.empty() ? I18n::Tr(L"home.notConnected") : label);
        }

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

        // A failed connect surfaces once per failure: the smart pseudo-node's
        // own ladder exhausted every candidate (error bar), while a regular
        // node prompts for smart routing or a new pick.
        if (state.status == L"failed")
        {
            auto error = state.lastError.value_or(L"");
            auto selected = Services::RelayStore::Instance().SelectedId();
            if (Services::RelayDirectory::IsRetryableConnectError(error))
            {
                std::wstring failedId = state.connection
                    ? state.connection->relayId : selected;
                auto key = failedId + L"|" + error;
                if (key != m_promptedFailureKey)
                {
                    if (selected == Services::RelayDirectory::kSmartRelayId)
                    {
                        // The core's ladder tried every candidate and none
                        // answered; don't re-offer smart routing to itself.
                        ShowError(I18n::Tr(L"relay.smartExhausted"));
                        m_promptedFailureKey = key;
                    }
                    else
                    {
                        std::wstring title = failedId;
                        for (auto const& relay : Services::RelayStore::Instance().Relays())
                            if (relay.id == failedId)
                            {
                                title = Services::RelayDirectory::DisplayTitleOf(relay);
                                break;
                            }
                        if (OfferSmartFallback(title))
                            m_promptedFailureKey = key;
                    }
                }
            }
        }
        else
        {
            m_promptedFailureKey.clear();
        }
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
        m_pendingConnect = !connected;
        RenderState();

        // The smart-routing pseudo-node dispatches the core's auto-select
        // ladder; its callback surfaces synchronous refusals only.
        if (!connected &&
            selectedId == Services::RelayDirectory::kSmartRelayId)
        {
            BeginSmartConnect();
            return;
        }

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
                    m_pendingConnect.reset();
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
                    m_pendingConnect.reset();
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
                    m_pendingConnect.reset();
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

    // Smart routing (the pseudo-node at the top of the list): dispatch the
    // core's auto-select connect; this callback surfaces a synchronous
    // refusal (428/409) — every other outcome arrives via state events.
    void HomePage::BeginSmartConnect()
    {
        Services::RelayDirectory::ConnectSmart(
            [this, weak = m_lifetime.Weak()](std::exception_ptr ep) {
                Services::Ui::Post([this, weak, ep] {
                    if (!StateUi::Lifetime::Live(weak))
                        return;
                    m_pendingSince.reset();
                    m_pendingConnect.reset();
                    RenderState();
                    if (!ep)
                        return; // regular end: connected, or exhausted (store error shows)
                    try
                    {
                        std::rethrow_exception(ep);
                    }
                    catch (Services::ElevationRequiredException const& ex)
                    {
                        OfferElevatedRestart(ex.WideMessage());
                    }
                    catch (Services::CoreApiException const& ex)
                    {
                        ShowError(ex.WideMessage());
                    }
                    catch (std::exception const&)
                    {
                        ShowError(I18n::Tr(L"dlg.connectFailed"));
                    }
                });
            });
    }

    // Failure surface for a regular node that failed to dial: offer smart
    // routing (primary) or let the user pick another node in the list.
    bool HomePage::OfferSmartFallback(std::wstring const& failedTitle)
    {
        auto xamlRoot = XamlRoot();
        if (!xamlRoot)
            return false; // page not in a frame yet; retry on the next tick
        ContentDialog dialog;
        dialog.Title(box_value(winrt::hstring(I18n::Tr(L"dlg.smartFailTitle"))));
        dialog.Content(box_value(winrt::hstring(I18n::Tr(L"dlg.smartFailBody", failedTitle))));
        dialog.PrimaryButtonText(winrt::hstring(I18n::Tr(L"dlg.smartUse")));
        dialog.CloseButtonText(winrt::hstring(I18n::Tr(L"dlg.close")));
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.XamlRoot(xamlRoot);

        auto operation = dialog.ShowAsync();
        operation.Completed([this, weak = m_lifetime.Weak()](
                                Windows::Foundation::IAsyncOperation<ContentDialogResult> const& async,
                                Windows::Foundation::AsyncStatus status) {
            if (status != Windows::Foundation::AsyncStatus::Completed ||
                async.get() != ContentDialogResult::Primary)
                return; // "关闭": the user picks another node themselves
            if (!StateUi::Lifetime::Live(weak))
                return;

            // Switch the selection to smart routing and connect at once.
            Services::RelayStore::Instance().SetSelectedId(
                Services::RelayDirectory::kSmartRelayId);
            auto state = Services::AppState::Instance().Current();
            if (StateUi::IsBusy(state) || m_pendingSince)
                return;
            m_pendingSince = std::chrono::steady_clock::now();
            m_pendingConnect = true;
            RenderState();
            BeginSmartConnect();
        });
        return true;
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
