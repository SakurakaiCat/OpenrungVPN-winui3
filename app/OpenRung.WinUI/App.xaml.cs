using Microsoft.UI.Xaml;
using OpenRung.WinUI.Services;
using OpenRung.WinUI.ViewModels;

namespace OpenRung.WinUI;

/// <summary>
/// Application entry point: owns the <see cref="CoreSupervisor"/> and the shared
/// view models, starts the core + SSE stream, and guarantees a graceful core
/// shutdown when the window closes (the tray keeps the engine alive otherwise).
/// </summary>
public partial class App : Application
{
    public static CoreSupervisor Supervisor { get; } = new();

    public static AppStateViewModel State { get; private set; } = null!;
    public static ServersViewModel Servers { get; } = new();
    public static LogsViewModel Logs { get; } = new();

    private Window? _window;
    private Mutex? _singleInstance;

    public App()
    {
        UnhandledException += (_, e) => TryLogCrash(e.Exception);
        InitializeComponent();
    }

    /// <summary>Writes crash details to %LOCALAPPDATA%\OpenRung\last-crash.txt before death.</summary>
    private static void TryLogCrash(Exception? ex)
    {
        try
        {
            var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OpenRung");
            Directory.CreateDirectory(dir);
            File.AppendAllText(Path.Combine(dir, "last-crash.txt"),
                $"[{DateTime.Now:O}] {ex}\r\n\r\n");
        }
        catch
        {
            // Crash logging must never itself throw.
        }
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        // Single instance: two supervisors would fight over the shared core
        // endpoint file (spawn/kill cycles against each other), so a second
        // launch just exits. The mutex dies with the owning process.
        bool owned;
        _singleInstance = new Mutex(true, @"Local\OpenRung.WinUI.SingleInstance", out owned);
        if (!owned)
            Environment.Exit(0); // second launch: the first instance owns the core

        try
        {
            _window = new Views.MainWindow();
            State = new AppStateViewModel(_window.DispatcherQueue);
        }
        catch (Exception ex)
        {
            TryLogCrash(ex);
            throw;
        }

        AppLog.Attach(_window.DispatcherQueue);

        Supervisor.StateChanged += (_, s) => State.ApplyState(s);
        Supervisor.LogReceived += (_, line) =>
            _window.DispatcherQueue.TryEnqueue(() =>
                Logs.Append($"[{line.Time}] {line.Line}"));

        _window.Activate();
        // Fire-and-forget must never leak an unobserved fault into the process
        // failfast (a missing core exe would otherwise kill the whole app).
        _ = Supervisor.StartAsync().ContinueWith(
            t =>
            {
                if (t.IsFaulted)
                {
                    var ex = t.Exception?.GetBaseException() ?? new InvalidOperationException("core startup failed");
                    TryLogCrash(ex);
                    AppLog.Write($"core startup failed: {ex.Message}");
                    _window?.DispatcherQueue.TryEnqueue(() =>
                        Logs.Append($"[error] core startup failed: {ex.Message}"));
                    return;
                }
                AppLog.Write("core ready; event stream starting");
                // "Auto-clear existing system proxy": a stale third-party proxy
                // left by another client would otherwise sit in front of the
                // tunnel. Clearing it here (idempotent, no-op when empty) makes
                // the app own the proxy decision from second zero.
                if (AppSettings.Load().AutoClearProxy)
                {
                    _ = Supervisor.Core.Api.ClearSystemProxyAsync().ContinueWith(
                        c =>
                        {
                            if (c.IsFaulted)
                                AppLog.Write($"auto-clear system proxy failed: {c.Exception?.GetBaseException()?.Message}");
                            else
                                AppLog.Write("auto-clear: system proxy removed at startup");
                        },
                        TaskContinuationOptions.OnlyOnFaulted);
                }
            });
    }

    /// <summary>Called by MainWindow's Close handler to tear the core down first.</summary>
    public static Task ShutdownAsync()
    {
        AppLog.Write("app exiting; stopping core");
        return Supervisor.DisposeAsync().AsTask();
    }
}
