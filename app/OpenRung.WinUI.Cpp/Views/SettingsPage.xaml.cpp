#include "pch.h"
#include "SettingsPage.xaml.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

#include "../Models/Dto.h"
#include "../Services/AppLog.h"
#include "../Services/AppSettings.h"
#include "../Services/AppState.h"
#include "../Services/CoreApiClient.h"
#include "../Services/CoreSupervisor.h"
#include "../Services/Localization.h"
#include "../Services/UpdateCheck.h"
#include "StateUi.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Services;
using namespace ::StateUi;

namespace winrt::OpenRung::WinUI::implementation
{
    SettingsPage::SettingsPage()
    {
        InitializeComponent();

        m_loadedToken = Loaded([this](Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
            OnLoaded();
        });
        ApplyStrings();
        // Reflect the persisted language choice without firing the handler.
        m_suppressLanguage = true;
        auto lang = Services::AppSettings::Load().language;
        LanguageCombo().SelectedIndex(lang == L"zh" ? 1 : lang == L"en" ? 2 : 0);
        m_suppressLanguage = false;
        RenderSystemProxy();
    }

    SettingsPage::~SettingsPage()
    {
        m_lifetime.End();
        Loaded(m_loadedToken);
        Services::AppState::Instance().RemoveListener(&m_stateKey);
    }

    void SettingsPage::OnLoaded()
    {
        AppVersionText().Text(I18n::Tr(L"settings.curVer", Services::AppVersion));
        RenderCoreManagerStatus();

        // Reflect the persisted engine mode without firing the handler.
        SetComboIndex(Services::AppState::Instance().Current().mode == L"tun" ? 1 : 0);

        // Persisted auto-clear preference + live system-proxy display.
        m_suppressAutoClear = true;
        AutoClearCheck().IsChecked(Services::AppSettings::Load().autoClearProxy);
        m_suppressAutoClear = false;
        RenderSystemProxy();
        Services::AppState::Instance().AddListener(&m_stateKey, [this, weak = m_lifetime.Weak()] {
            if (!StateUi::Lifetime::Live(weak)) return;
            RenderSystemProxy();
            RenderCoreManagerStatus();
        });

        auto weak = m_lifetime.Weak();
        std::thread([this, weak] {
            try
            {
                auto v = Services::CoreSupervisor::Instance().Core().EnsureRunning(false).GetVersion();
                Services::Ui::Post([this, weak, v] {
                    if (!StateUi::Lifetime::Live(weak))
                        return;
                    CoreVersionText().Text(L"OpenRung Core " + v.core);
                    EngineText().Text(v.engine);
                });

                auto& manager = Services::CoreSupervisor::Instance().Core();
                auto port = manager.EndpointPort();
                auto pid = manager.EndpointPid();
                if (port && pid)
                {
                    Services::Ui::Post([this, weak, port = *port, pid = *pid] {
                        if (!StateUi::Lifetime::Live(weak))
                            return;
                        EndpointText().Text(I18n::Tr(L"settings.endpointFmt",
                            L"127.0.0.1:" + std::to_wstring(port), std::to_wstring(pid)));
                    });
                }
            }
            catch (...)
            {
                // page is informational only
            }
        }).detach();
    }

    void SettingsPage::RenderSystemProxy()
    {
        auto proxy = Services::AppState::Instance().Current().systemProxy;
        SystemProxyText().Text(I18n::Tr(L"settings.systemProxyText",
            StateUi::ProxyDisplay(proxy)));
    }

    void SettingsPage::PersistAutoClear(bool value)
    {
        auto settings = Services::AppSettings::Load();
        settings.autoClearProxy = value;
        settings.Save();
    }

    void SettingsPage::AutoClearProxy_Checked(Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        if (m_suppressAutoClear)
            return;
        PersistAutoClear(true);
        ClearNow(L"auto-clear enabled");
    }

    void SettingsPage::AutoClearProxy_Unchecked(Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        if (m_suppressAutoClear)
            return;
        PersistAutoClear(false);
        Services::AppLog::Write(L"auto-clear system proxy disabled");
    }

    void SettingsPage::ClearProxy_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        ClearNow(L"user request");
    }

    void SettingsPage::ClearNow(std::wstring const& reason)
    {
        if (m_proxyBusy)
            return;
        m_proxyBusy = true;
        ClearProxyButton().IsEnabled(false);
        std::thread([this, weak = m_lifetime.Weak(), reason] {
            try
            {
                Services::CoreSupervisor::Instance().Core().EnsureRunning(false).ClearSystemProxy();
                Services::AppLog::Write(L"system proxy cleared (" + reason + L")");
            }
            catch (std::exception const& ex)
            {
                Services::AppLog::Write(L"clear system proxy failed: " + Services::Utf8ToWide(ex.what()));
            }
            Services::Ui::Post([this, weak] {
                if (!StateUi::Lifetime::Live(weak))
                    return;
                m_proxyBusy = false;
                ClearProxyButton().IsEnabled(true);
            });
        }).detach();
    }

    void SettingsPage::CheckUpdate_Click(Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        if (m_updateBusy)
            return;
        m_updateBusy = true;
        m_lastUpdateStatus = UpdateStatus::Checking;
        CheckUpdateButton().IsEnabled(false);
        UpdateStatusText().Text(I18n::Tr(L"status.checking"));

        auto weak = m_lifetime.Weak();
        std::thread([this, weak] {
            std::wstring failure;
            std::optional<Services::UpdateInfo> update;
            try
            {
                update = Services::CheckForUpdate();
            }
            catch (std::exception const& ex)
            {
                failure = Services::Utf8ToWide(ex.what());
            }

            Services::Ui::Post([this, weak, update = std::move(update), failure] {
                if (!StateUi::Lifetime::Live(weak))
                    return;
                m_updateBusy = false;
                CheckUpdateButton().IsEnabled(true);

                if (!failure.empty())
                {
                    m_lastUpdateStatus = UpdateStatus::Failed;
                    UpdateStatusText().Text(I18n::Tr(L"status.checkFailed"));
                    ShowDialog(I18n::Tr(L"dlg.updateCheckFailTitle"), failure, {}, I18n::Tr(L"dlg.close"));
                    return;
                }
                if (!update)
                {
                    m_lastUpdateStatus = UpdateStatus::UpToDate;
                    UpdateStatusText().Text(I18n::Tr(L"dlg.upToDateStatus"));
                    ShowDialog(I18n::Tr(L"dlg.updateTitle"),
                        I18n::Tr(L"dlg.upToDateBody", Services::AppVersion),
                        {}, I18n::Tr(L"dlg.ok"));
                    return;
                }

                m_lastUpdateStatus = UpdateStatus::Found;
                m_lastUpdateTag = I18n::Tr(L"dlg.updateFound", update->tag);
                UpdateStatusText().Text(m_lastUpdateTag);
                ShowUpdateDialog(*update);
            });
        }).detach();
    }

    void SettingsPage::ShowUpdateDialog(Services::UpdateInfo const& update)
    {
        ContentDialog dialog;
        dialog.Title(box_value(winrt::hstring(I18n::Tr(L"dlg.updateFound", update.tag))));
        dialog.Content(box_value(winrt::hstring(I18n::Tr(L"dlg.updateBody",
            Services::AppVersion, update.tag))));
        dialog.PrimaryButtonText(winrt::hstring(I18n::Tr(L"dlg.updateGo")));
        dialog.CloseButtonText(winrt::hstring(I18n::Tr(L"dlg.close")));
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.XamlRoot(XamlRoot());

        dialog.ShowAsync().Completed([page = update.pageUrl](
            Windows::Foundation::IAsyncOperation<ContentDialogResult> const& async,
            Windows::Foundation::AsyncStatus status) {
            if (status != Windows::Foundation::AsyncStatus::Completed)
                return;
            if (async.get() != ContentDialogResult::Primary)
                return;
            // Open the release page in the default browser.
            winrt::Windows::System::Launcher::LaunchUriAsync(
                Windows::Foundation::Uri(page));
        });
    }

    bool SettingsPage::ComboModeIsTun()
    {
        return Services::AppState::Instance().Current().mode == L"tun";
    }

    void SettingsPage::SetComboIndex(int index)
    {
        m_suppressSelection = true;
        ModeCombo().SelectedIndex(index);
        m_suppressSelection = false;
    }

    void SettingsPage::ModeCombo_SelectionChanged(Windows::Foundation::IInspectable const&,
        SelectionChangedEventArgs const&)
    {
        if (m_suppressSelection)
            return;
        auto item = ModeCombo().SelectedItem().try_as<ComboBoxItem>();
        if (!item)
            return;
        auto mode = winrt::unbox_value_or<hstring>(item.Tag(), L"proxy");

        auto weak = m_lifetime.Weak();
        std::thread([this, weak, mode = std::wstring(mode)] {
            try
            {
                Services::CoreSupervisor::Instance().Core().EnsureRunning(false).SetMode(mode);
                Services::AppLog::Write(L"mode switched to " + mode);
            }
            catch (Services::ElevationRequiredException const& ex)
            {
                Services::Ui::Post([this, weak, why = ex.WideMessage()] {
                    if (!StateUi::Lifetime::Live(weak))
                        return;
                    SetComboIndex(ComboModeIsTun() ? 1 : 0);
                    OfferElevation(why);
                });
            }
            catch (Services::CoreApiException const& ex)
            {
                Services::Ui::Post([this, weak, msg = ex.WideMessage()] {
                    if (!StateUi::Lifetime::Live(weak))
                        return;
                    SetComboIndex(ComboModeIsTun() ? 1 : 0);
                    // Most likely 409 "connected": disconnect first on Home.
                    ShowDialog(I18n::Tr(L"dlg.modeFailTitle"), msg, {}, I18n::Tr(L"dlg.close"));
                });
            }
            catch (std::exception const&)
            {
                // Connection-level failure (core dead / restarting).
                Services::Ui::Post([this, weak] {
                    if (!StateUi::Lifetime::Live(weak))
                        return;
                    SetComboIndex(ComboModeIsTun() ? 1 : 0);
                });
            }
        }).detach();
    }

    void SettingsPage::OfferElevation(std::wstring const& why)
    {
        ContentDialog dialog;
        dialog.Title(box_value(winrt::hstring(I18n::Tr(L"dlg.elevTitle"))));
        dialog.Content(box_value(I18n::Tr(L"dlg.elevTunBody", why)));
        dialog.PrimaryButtonText(winrt::hstring(I18n::Tr(L"dlg.restartCore")));
        dialog.CloseButtonText(winrt::hstring(I18n::Tr(L"dlg.cancel")));
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.XamlRoot(XamlRoot());

        auto weak = m_lifetime.Weak();
        dialog.ShowAsync().Completed([this, weak](
                                          Windows::Foundation::IAsyncOperation<ContentDialogResult> const& async,
                                          Windows::Foundation::AsyncStatus status) {
            if (status != Windows::Foundation::AsyncStatus::Completed)
                return;
            if (!StateUi::Lifetime::Live(weak))
                return;
            if (async.get() != ContentDialogResult::Primary)
                return;

            std::thread([this, weak] {
                std::wstring failure;
                try
                {
                    Services::CoreSupervisor::Instance().RestartElevated();
                    Services::CoreSupervisor::Instance().Core().EnsureRunning(false).SetMode(L"tun");
                }
                catch (std::exception const& ex)
                {
                    failure = Services::Utf8ToWide(ex.what());
                }
                Services::Ui::Post([this, weak, failure] {
                    if (!StateUi::Lifetime::Live(weak))
                        return;
                    if (failure.empty())
                    {
                        SetComboIndex(1);
                        return;
                    }
                    // user declined UAC or restart failed; restore old mode
                    Services::AppLog::Write(L"elevated restart failed: " + failure);
                    SetComboIndex(ComboModeIsTun() ? 1 : 0);
                    ShowDialog(I18n::Tr(L"dlg.restartFailTitle"), failure, {}, I18n::Tr(L"dlg.close"));
                });
            }).detach();
        });
    }

    void SettingsPage::ApplyStrings()
    {
        TitleText().Text(I18n::Tr(L"settings.title"));
        CaptureModeHeader().Text(I18n::Tr(L"settings.captureMode"));
        CaptureModeDesc().Text(I18n::Tr(L"settings.captureModeDesc"));
        SystemProxyHeader().Text(I18n::Tr(L"settings.systemProxy"));
        AutoClearCheck().Content(box_value(winrt::hstring(I18n::Tr(L"settings.autoClear"))));
        ClearProxyButton().Content(box_value(winrt::hstring(I18n::Tr(L"settings.clearNow"))));
        LanguageHeader().Text(I18n::Tr(L"settings.language"));
        CoreHeader().Text(I18n::Tr(L"settings.core"));
        CoreStartButton().Content(box_value(winrt::hstring(I18n::Tr(L"settings.coreStart"))));
        CoreStopButton().Content(box_value(winrt::hstring(I18n::Tr(L"settings.coreStop"))));
        CoreRestartButton().Content(box_value(winrt::hstring(I18n::Tr(L"settings.coreRestart"))));
        CoreManagerHint().Text(I18n::Tr(L"settings.coreManagerHint"));
        AboutHeader().Text(I18n::Tr(L"settings.about"));
        AboutMaintainerText().Text(I18n::Tr(L"settings.aboutMaintainer"));
        AboutDevText().Text(I18n::Tr(L"settings.aboutDev"));
        CopyrightText().Text(I18n::Tr(L"settings.copyright"));
        CheckUpdateButton().Content(box_value(winrt::hstring(I18n::Tr(L"settings.checkUpdates"))));

        // Combo item labels (tags stay stable).
        auto modeItems = ModeCombo().Items();
        modeItems.GetAt(0).as<ComboBoxItem>().Content(box_value(winrt::hstring(I18n::Tr(L"settings.modeProxy"))));
        modeItems.GetAt(1).as<ComboBoxItem>().Content(box_value(winrt::hstring(I18n::Tr(L"settings.modeTun"))));
        auto langItems = LanguageCombo().Items();
        langItems.GetAt(0).as<ComboBoxItem>().Content(box_value(winrt::hstring(I18n::Tr(L"settings.langSystem"))));
        langItems.GetAt(1).as<ComboBoxItem>().Content(box_value(winrt::hstring(L"简体中文")));
        langItems.GetAt(2).as<ComboBoxItem>().Content(box_value(winrt::hstring(L"English")));

        RenderSystemProxy();
        RenderCoreManagerStatus();
        if (m_lastUpdateStatus == UpdateStatus::None)
            UpdateStatusText().Text(L"");
        else if (m_lastUpdateStatus == UpdateStatus::Checking)
            UpdateStatusText().Text(I18n::Tr(L"status.checking"));
        else if (m_lastUpdateStatus == UpdateStatus::Failed)
            UpdateStatusText().Text(I18n::Tr(L"status.checkFailed"));
        else if (m_lastUpdateStatus == UpdateStatus::UpToDate)
            UpdateStatusText().Text(I18n::Tr(L"dlg.upToDateStatus"));
        else if (m_lastUpdateStatus == UpdateStatus::Found)
            UpdateStatusText().Text(m_lastUpdateTag);
    }

    void SettingsPage::LanguageCombo_SelectionChanged(Windows::Foundation::IInspectable const&,
        SelectionChangedEventArgs const&)
    {
        if (m_suppressLanguage)
            return;
        auto item = LanguageCombo().SelectedItem().try_as<ComboBoxItem>();
        if (!item)
            return;
        auto lang = std::wstring(winrt::unbox_value_or<hstring>(item.Tag(), L"system"));

        // Full runtime switch: SetPreference persists the choice, resolves
        // the active language, and fires LanguageChanged — the shell then
        // refreshes nav labels and re-navigates the frame (rebuilding this
        // page with the new strings). No process restart needed.
        I18n::SetPreference(lang);
    }

    void SettingsPage::RenderCoreManagerStatus()
    {
        auto info = Services::CoreSupervisor::Instance().Describe();
        bool running = info.phase == Services::CoreSupervisor::CorePhase::Running;
        std::wstring text = running
            ? I18n::Tr(L"settings.coreRunning", std::to_wstring(info.pid))
            : I18n::Tr(L"settings.coreStopped");
        if (!info.lastError.empty())
            text += L" — " + info.lastError;
        CoreStatusText().Text(text);
        CoreStartButton().IsEnabled(!running && !m_coreBusy);
        CoreStopButton().IsEnabled(running && !m_coreBusy);
        CoreRestartButton().IsEnabled(running && !m_coreBusy);
    }

    void SettingsPage::CoreStart_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_coreBusy)
            return;
        m_coreBusy = true;
        std::thread([this, weak = m_lifetime.Weak()] {
            std::wstring failure;
            try
            {
                Services::CoreSupervisor::Instance().Start();
            }
            catch (std::exception const& ex)
            {
                failure = Services::Utf8ToWide(ex.what());
            }
            Services::Ui::Post([this, weak, failure = std::move(failure)] {
                if (!StateUi::Lifetime::Live(weak))
                    return;
                m_coreBusy = false;
                if (failure.empty())
                    Services::AppLog::Write(I18n::Tr(L"log.coreStarted"));
                else
                    Services::AppLog::Write(I18n::Tr(L"log.coreStartFailed") + failure);
                RenderCoreManagerStatus();
            });
        }).detach();
    }

    void SettingsPage::CoreStop_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_coreBusy)
            return;
        m_coreBusy = true;
        std::thread([this, weak = m_lifetime.Weak()] {
            std::wstring failure;
            try
            {
                Services::CoreSupervisor::Instance().Stop();
            }
            catch (std::exception const& ex)
            {
                failure = Services::Utf8ToWide(ex.what());
            }
            Services::Ui::Post([this, weak, failure = std::move(failure)] {
                if (!StateUi::Lifetime::Live(weak))
                    return;
                m_coreBusy = false;
                if (failure.empty())
                    Services::AppLog::Write(I18n::Tr(L"log.coreStopped"));
                else
                    Services::AppLog::Write(I18n::Tr(L"log.coreStopFailed") + failure);
                RenderCoreManagerStatus();
            });
        }).detach();
    }

    void SettingsPage::CoreRestart_Click(Windows::Foundation::IInspectable const&, RoutedEventArgs const&)
    {
        if (m_coreBusy)
            return;
        m_coreBusy = true;
        std::thread([this, weak = m_lifetime.Weak()] {
            std::wstring failure;
            try
            {
                Services::CoreSupervisor::Instance().Core().Restart();
            }
            catch (std::exception const& ex)
            {
                failure = Services::Utf8ToWide(ex.what());
            }
            Services::Ui::Post([this, weak, failure = std::move(failure)] {
                if (!StateUi::Lifetime::Live(weak))
                    return;
                m_coreBusy = false;
                if (failure.empty())
                    Services::AppLog::Write(I18n::Tr(L"log.coreRestarted"));
                else
                    Services::AppLog::Write(I18n::Tr(L"log.coreRestartFailed") + failure);
                RenderCoreManagerStatus();
            });
        }).detach();
    }

    void SettingsPage::ShowDialog(std::wstring const& title, std::wstring const& content,
        std::wstring const& primary, std::wstring const& close)
    {
        ContentDialog dialog;
        dialog.Title(box_value(winrt::hstring(title)));
        dialog.Content(box_value(winrt::hstring(content)));
        if (!primary.empty())
            dialog.PrimaryButtonText(winrt::hstring(primary));
        dialog.CloseButtonText(winrt::hstring(close));
        dialog.XamlRoot(XamlRoot());
        dialog.ShowAsync();
    }
}
