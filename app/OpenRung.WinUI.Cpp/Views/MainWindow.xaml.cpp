#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "App.xaml.h"
#include "../Models/Dto.h"
#include "../Services/AppState.h"
#include "../Services/AppLog.h"
#include "../Services/StartupLog.h"
#include "../Services/CoreSupervisor.h"
#include "../Services/Localization.h"
#include "../Services/RelayDirectory.h"
#include "StateUi.h"
#include "HomePage.xaml.h"
#include "ServersPage.xaml.h"
#include "LogsPage.xaml.h"
#include "SettingsPage.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Services;

namespace
{
    HWND WindowHandle(winrt::Microsoft::UI::Xaml::Window const& window)
    {
        // IWindowNative is not in the cppwinrt projection, so QueryInterface
        // for the raw MIDL interface (declared in microsoft.ui.xaml.window.h).
        HWND hwnd = nullptr;
        ::IWindowNative* native = nullptr;
        if (SUCCEEDED(winrt::get_unknown(window)->QueryInterface(
                __uuidof(::IWindowNative), reinterpret_cast<void**>(&native))))
        {
            native->get_WindowHandle(&hwnd);
            native->Release();
        }
        return hwnd;
    }
}

namespace winrt::OpenRung::WinUI::implementation
{
    MainWindow::MainWindow()
    {
        Services::StartupLog::Write("MainWindow enter");
        InitializeComponent();
        Services::StartupLog::Write("MainWindow InitializeComponent done");
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        Services::StartupLog::Write("MainWindow titlebar done");

        Title(L"OpenRung");
        SetIcon();
        Services::StartupLog::Write("MainWindow icon done");
        ContentFrame().Navigate(winrt::xaml_typename<winrt::OpenRung::WinUI::HomePage>());
        Services::StartupLog::Write("MainWindow navigate done");

        m_ticker = DispatcherTimer();
        m_ticker.Interval(Windows::Foundation::TimeSpan(std::chrono::seconds(1)));
        m_ticker.Tick([](Windows::Foundation::IInspectable const&, Windows::Foundation::IInspectable const&) {
            Services::AppState::Instance().Tick();
        });
        m_ticker.Start();
        Services::StartupLog::Write("MainWindow ticker done");

        Closed({ this, &MainWindow::OnClosed });
        m_closingToken = AppWindow().Closing({ this, &MainWindow::OnWindowClosing });
        Services::StartupLog::Write("MainWindow closing hooks done");
        SetupTray();
        Services::StartupLog::Write("MainWindow tray done");

        Services::AppState::Instance().AddListener(&m_stateKey, [this, weak = m_lifetime.Weak()] {
            if (!StateUi::Lifetime::Live(weak)) return;
            UpdateStatusBar();
        });
        UpdateStatusBar();
        Services::StartupLog::Write("MainWindow done");
    }

    void MainWindow::SetIcon()
    {
        wchar_t exePath[MAX_PATH] = {};
        if (!::GetModuleFileNameW(nullptr, exePath, MAX_PATH))
            return;
        std::filesystem::path iconPath = std::filesystem::path(exePath).parent_path()
            / L"Assets" / L"app.ico";
        if (std::filesystem::exists(iconPath))
            AppWindow().SetIcon(iconPath.wstring());
    }

    void MainWindow::SetupTray()
    {
        m_tray.Add(L"OpenRung");
        m_tray.LeftClick = [this] { ShowFromTray(); };
        m_tray.RightClick = [this] {
            // Blocks in TrackPopupMenu on the tray's message window (the UI
            // thread here); the choice is dispatched like the C# version.
            auto choice = Services::ShowTrayMenu(WindowHandle(*this),
                I18n::Tr(L"tray.open"), I18n::Tr(L"tray.quit"));
            if (choice == 1)
                ShowFromTray();
            else if (choice == 2)
                QuitAsync();
        };

        // Language switch: refresh the shell strings, re-title the cached
        // relay list, and re-navigate the frame so the active page rebuilds
        // with the new language.
        I18n::LanguageChanged = [this] {
            Services::Ui::Post([this] {
                if (!StateUi::Lifetime::Live(m_lifetime.Weak()))
                    return;
                Services::RelayDirectory::Retitle();
                ApplyLanguage();
            });
        };
        ApplyLanguage(false);
    }

    void MainWindow::ShowFromTray()
    {
        Services::Ui::Post([this] {
            Activate();
            if (auto presenter = AppWindow().Presenter().try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>())
                presenter.Restore();
        });
    }

    void MainWindow::HideToTray()
    {
        // Hide without closing: removes taskbar presence but keeps the window.
        AppWindow().Hide();
    }

    void MainWindow::OnWindowClosing(winrt::Microsoft::UI::Windowing::AppWindow const&,
        winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args)
    {
        if (m_quitting)
            return;
        // Default close = hide to tray; engine keeps running.
        args.Cancel(true);
        HideToTray();
    }

    void MainWindow::OnClosed(Windows::Foundation::IInspectable const&,
        WindowEventArgs const& args)
    {
        if (m_quitting)
            return; // fled through QuitAsync
        // Fallback if Closing was not cancelled (e.g. End Task): clean up core.
        args.Handled(true);
        QuitAsync();
    }

    void MainWindow::QuitAsync()
    {
        if (m_quitting)
            return;
        m_quitting = true;
        // Stop() blocks (graceful core shutdown); run it off the UI thread and
        // finish the teardown back on it. Shutting down is best-effort.
        std::thread([this] {
            try
            {
                App::ShutdownAsync();
            }
            catch (...)
            {
            }
            Services::Ui::Post([this] {
                m_tray.Remove();
                try
                {
                    AppWindow().Destroy();
                }
                catch (...)
                {
                }
                Application::Current().Exit();
                // Belt and braces: Application::Exit() alone has been known
                // to leave the message pump running when a window reference
                // lingers; WM_QUIT makes the Application::Start loop in
                // wWinMain return deterministically so the process (and the
                // statics that would re-Stop the core) actually unwind.
                ::PostQuitMessage(0);
            });
        }).detach();
    }

    void MainWindow::UpdateStatusBar()
    {
        auto state = Services::AppState::Instance().Current();
        StatusGlyphIcon().Glyph(StateUi::StatusGlyph(state.status));
        StatusText().Text(state.status);
        ProxyText().Text(state.proxy
            ? state.proxy->host + L":" + std::to_wstring(state.proxy->port)
            : L"");
    }

    void MainWindow::NavView_SelectionChanged(Controls::NavigationView const&,
        Controls::NavigationViewSelectionChangedEventArgs const& args)
    {
        auto item = args.SelectedItem().try_as<Controls::NavigationViewItem>();
        if (!item)
            return;
        auto tag = winrt::unbox_value_or<hstring>(item.Tag(), L"home");
        m_currentPage = tag == L"servers" ? L"servers"
            : tag == L"logs" ? L"logs"
            : tag == L"settings" ? L"settings"
            : L"home";
        if (tag == L"servers")
            ContentFrame().Navigate(winrt::xaml_typename<winrt::OpenRung::WinUI::ServersPage>());
        else if (tag == L"logs")
            ContentFrame().Navigate(winrt::xaml_typename<winrt::OpenRung::WinUI::LogsPage>());
        else if (tag == L"settings")
            ContentFrame().Navigate(winrt::xaml_typename<winrt::OpenRung::WinUI::SettingsPage>());
        else
            ContentFrame().Navigate(winrt::xaml_typename<winrt::OpenRung::WinUI::HomePage>());
    }

    void MainWindow::ApplyLanguage(bool navigate)
    {
        // Navigation labels live in XAML; set them from the string table so a
        // language switch can refresh them without recreating the window.
        auto const& items = NavView().MenuItems();
        if (items.Size() >= 4)
        {
            auto home = items.GetAt(0).as<Controls::NavigationViewItem>();
            auto servers = items.GetAt(1).as<Controls::NavigationViewItem>();
            auto logs = items.GetAt(2).as<Controls::NavigationViewItem>();
            auto settings = items.GetAt(3).as<Controls::NavigationViewItem>();
            home.Content(box_value(winrt::hstring(I18n::Tr(L"nav.home"))));
            servers.Content(box_value(winrt::hstring(I18n::Tr(L"nav.servers"))));
            logs.Content(box_value(winrt::hstring(I18n::Tr(L"nav.logs"))));
            settings.Content(box_value(winrt::hstring(I18n::Tr(L"nav.settings"))));
        }

        if (!navigate)
            return;

        // Rebuild the current page so its strings re-apply from the table.
        if (m_currentPage == L"servers")
            ContentFrame().Navigate(winrt::xaml_typename<winrt::OpenRung::WinUI::ServersPage>());
        else if (m_currentPage == L"logs")
            ContentFrame().Navigate(winrt::xaml_typename<winrt::OpenRung::WinUI::LogsPage>());
        else if (m_currentPage == L"settings")
            ContentFrame().Navigate(winrt::xaml_typename<winrt::OpenRung::WinUI::SettingsPage>());
        else
            ContentFrame().Navigate(winrt::xaml_typename<winrt::OpenRung::WinUI::HomePage>());
    }
}
