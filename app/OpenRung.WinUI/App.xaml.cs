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
                var ex = t.Exception?.GetBaseException() ?? new InvalidOperationException("core startup failed");
                TryLogCrash(ex);
                _window?.DispatcherQueue.TryEnqueue(() =>
                    Logs.Append($"[error] core startup failed: {ex.Message}"));
            },
            TaskContinuationOptions.OnlyOnFaulted);
    }

    /// <summary>Called by MainWindow's Close handler to tear the core down first.</summary>
    public static Task ShutdownAsync()
    {
        return Supervisor.DisposeAsync().AsTask();
    }
}
