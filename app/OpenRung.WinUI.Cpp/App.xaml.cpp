#include "pch.h"
#include "App.xaml.h"
#if __has_include("App.xaml.g.cpp")
#include "App.xaml.g.cpp"
#endif
#if __has_include("App.g.cpp")
#include "App.g.cpp"
#endif
#include "Views\MainWindow.xaml.h"
#include "Services\AppLog.h"
#include "Services\AppSettings.h"
#include "Services\AppState.h"
#include "Services\CoreSupervisor.h"
#include "Services\StartupLog.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Services;

namespace winrt::OpenRung::WinUI::implementation
{
    App::App()
    {
        StartupLog::Write("App::App enter");
        InitializeComponent();
        StartupLog::Write("App::App InitializeComponent done");
        UnhandledException({ this, &App::OnUnhandledException });
        StartupLog::Write("App::App done");
    }

    void App::OnUnhandledException(IInspectable const&, UnhandledExceptionEventArgs const& e)
    {
        AppLog::LogCrash(e.Exception());
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        StartupLog::Write("OnLaunched enter");
        try
        {
            m_window = make<MainWindow>();
            StartupLog::Write("OnLaunched window created");
        }
        catch (winrt::hresult_error const& ex)
        {
            StartupLog::Write(L"OnLaunched window create failed: " + StartupLog::Describe(ex));
            AppLog::LogCrash(ex.code());
            throw;
        }

        AppLog::Attach(m_window.DispatcherQueue());

        auto& supervisor = CoreSupervisor::Instance();
        supervisor.StateChanged = [](Services::StateSnapshot const& s) {
            AppState::Instance().ApplyState(s);
        };
        supervisor.LogReceived = [](Services::LogLine const& log) {
            AppLog::AppendCoreLine(log.time, log.line);
        };

        m_window.Activate();
        StartupLog::Write("OnLaunched window activated");

        // Fire-and-forget: a core startup failure is surfaced on the logs page,
        // never as an unhandled exception that would kill the app.
        std::thread([] {
            try
            {
                CoreSupervisor::Instance().Start();
                AppLog::Write(L"core ready; event stream starting");
                if (AppSettings::Load().autoClearProxy)
                {
                    try
                    {
                        CoreSupervisor::Instance().Core().EnsureRunning(false).ClearSystemProxy();
                        AppLog::Write(L"auto-clear: system proxy removed at startup");
                    }
                    catch (std::exception const& ex)
                    {
                        AppLog::Write(L"auto-clear system proxy failed: " + Utf8ToWide(ex.what()));
                    }
                }
            }
            catch (std::exception const& ex)
            {
                AppLog::LogCrash(ex.what());
                AppLog::Write(L"core startup failed: " + Utf8ToWide(ex.what()));
            }
        }).detach();
    }

    void App::ShutdownAsync()
    {
        AppLog::Write(L"app exiting; stopping core");
        CoreSupervisor::Instance().Stop();
    }
}

// ---------------------------------------------------------------------------
// Entry point (replaces the XAML-generated wWinMain, suppressed via
// DISABLE_XAML_GENERATED_MAIN on App.xaml in the project file... kept here so
// the single-instance check runs before any XAML work).
// ---------------------------------------------------------------------------

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    Services::StartupLog::Init();
    Services::StartupLog::Write("wWinMain enter");

    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        Services::StartupLog::Write("apartment initialized");
    }
    catch (...)
    {
        Services::StartupLog::Write("apartment init failed");
        throw;
    }

    // Single instance: two supervisors would fight over the shared core
    // endpoint file (spawn/kill cycles against each other), so a second
    // launch just exits. The mutex dies with the owning process.
    HANDLE singleInstance = ::CreateMutexW(nullptr, TRUE, L"Local\\OpenRung.WinUI.SingleInstance");
    if (singleInstance && ::GetLastError() == ERROR_ALREADY_EXISTS)
    {
        Services::StartupLog::Write("second instance detected; exiting");
        ::CloseHandle(singleInstance);
        return 0; // second launch: the first instance owns the core
    }

    Services::StartupLog::Write("Application::Start entering");
    try
    {
        ::winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
            Services::StartupLog::Write("Start callback: creating App");
            ::winrt::make<::winrt::OpenRung::WinUI::implementation::App>();
            Services::StartupLog::Write("Start callback: App created");
        });
        Services::StartupLog::Write("Application::Start returned");
    }
    catch (winrt::hresult_error const& ex)
    {
        Services::StartupLog::Write(L"Application::Start threw: " + Services::StartupLog::Describe(ex));
        throw;
    }
    catch (std::exception const& ex)
    {
        Services::StartupLog::Write(std::string("Application::Start threw: ") + ex.what());
        throw;
    }
    catch (...)
    {
        Services::StartupLog::Write("Application::Start threw unknown exception");
        throw;
    }

    if (singleInstance) ::CloseHandle(singleInstance);
    return 0;
}
