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
        AppVersionText().Text(L"当前版本：" + std::wstring(Services::AppVersion));

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
                        EndpointText().Text(L"127.0.0.1:" + std::to_wstring(port)
                            + L"（PID " + std::to_wstring(pid) + L"）");
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
        SystemProxyText().Text(
            L"当前系统代理：" + StateUi::ProxyDisplay(proxy)
            + L"。代理模式下内核会先接管已有代理、断开时恢复；开启自动清除后，启动时直接移除已有代理（例如其他代理工具留下的）。");
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
        CheckUpdateButton().IsEnabled(false);
        UpdateStatusText().Text(L"正在检查更新…");

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
                    UpdateStatusText().Text(L"检查失败");
                    ShowDialog(L"检查更新失败", failure, {}, L"关闭");
                    return;
                }
                if (!update)
                {
                    UpdateStatusText().Text(L"已是最新版本");
                    ShowDialog(L"检查更新",
                        std::wstring(L"当前已是最新版本（") + Services::AppVersion + L"）。",
                        {}, L"好");
                    return;
                }

                UpdateStatusText().Text(L"发现新版本 " + update->tag);
                ShowUpdateDialog(*update);
            });
        }).detach();
    }

    void SettingsPage::ShowUpdateDialog(Services::UpdateInfo const& update)
    {
        ContentDialog dialog;
        dialog.Title(box_value(L"发现新版本 " + winrt::hstring(update.tag)));
        dialog.Content(box_value(std::wstring(L"当前版本 ") + Services::AppVersion
            + L"，最新版本 " + update.tag + L"。\n\n请前往 GitHub Releases 页面下载新的压缩包并解压替换。"));
        dialog.PrimaryButtonText(L"前往下载");
        dialog.CloseButtonText(L"关闭");
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
                    ShowDialog(L"无法切换模式", msg, {}, L"关闭");
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
        dialog.Title(box_value(L"需要管理员权限"));
        dialog.Content(box_value(why + L"\n\n是否重启内核为管理员模式并启用 TUN？"));
        dialog.PrimaryButtonText(L"重启核心");
        dialog.CloseButtonText(L"取消");
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
                    ShowDialog(L"重启失败", failure, {}, L"关闭");
                });
            }).detach();
        });
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
