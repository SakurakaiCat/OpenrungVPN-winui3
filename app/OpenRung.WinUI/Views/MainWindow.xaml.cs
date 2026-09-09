using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using OpenRung.WinUI.Services;
using OpenRung.WinUI.ViewModels;
using WinRT.Interop;

namespace OpenRung.WinUI.Views;

/// <summary>
/// Shell: hosts the four pages in a frame, renders the shared status bar from
/// <see cref="AppStateViewModel"/>, and owns the tray icon. Closing the window
/// hides it to the tray (the engine keeps running); the tray menu's 退出 item
/// shuts the core down and exits.
/// </summary>
public sealed partial class MainWindow : Window
{
    public AppStateViewModel ViewModel => App.State;

    private Win32TrayIcon? _tray;
    private bool _quitting;

    public MainWindow()
    {
        InitializeComponent();
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);

        Title = "OpenRung";
        SetIcon();
        ContentFrame.Navigate(typeof(HomePage));

        var ticker = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        ticker.Tick += (_, _) => App.State.Tick();
        ticker.Start();

        Closed += OnClosed;
        AppWindow.Closing += OnWindowClosing;
        SetupTray();
    }

    private void SetIcon()
    {
        var iconPath = Path.Combine(AppContext.BaseDirectory, "Assets", "app.ico");
        if (File.Exists(iconPath))
            AppWindow.SetIcon(iconPath);
    }

    private void SetupTray()
    {
        _tray = new Win32TrayIcon();
        _tray.Add("OpenRung");
        _tray.LeftClick += (_, _) => ShowFromTray();
        _tray.RightClick += (_, _) =>
        {
            var hwnd = WindowNative.GetWindowHandle(this);
            var choice = TrayMenu.Show(hwnd, "打开窗口", "退出");
            if (choice == "open")
                ShowFromTray();
            else if (choice == "quit")
                DispatcherQueue.TryEnqueue(async () => await QuitAsync());
        };
    }

    private void ShowFromTray()
    {
        DispatcherQueue.TryEnqueue(() =>
        {
            this.Activate();
            if (AppWindow.Presenter is OverlappedPresenter p)
                p.Restore();
        });
    }

    private void OnWindowClosing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (_quitting)
            return;
        // Default close = hide to tray; engine keeps running.
        args.Cancel = true;
        HideToTray();
    }

    private void HideToTray()
    {
        // Hide without closing: removes taskbar presence but keeps the window.
        if (AppWindow.Presenter is OverlappedPresenter p)
        {
            AppWindow.Hide();
        }
    }

    private void OnClosed(object sender, WindowEventArgs args)
    {
        if (_quitting)
            return; // fled through QuitAsync
        // Fallback if Closing was not cancelled (e.g. End Task): clean up core.
        args.Handled = true;
        _ = QuitAsync();
    }

    private async Task QuitAsync()
    {
        if (_quitting)
            return;
        _quitting = true;
        try
        {
            await App.ShutdownAsync().ConfigureAwait(false);
        }
        catch { /* shutting down is best-effort */ }
        DispatcherQueue.TryEnqueue(() =>
        {
            _tray?.Dispose();
            Closed -= OnClosed;
            AppWindow.Destroy();
            Application.Current.Exit();
        });
    }

    private void NavView_SelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (args.IsSettingsSelected)
        {
            ContentFrame.Navigate(typeof(SettingsPage));
            return;
        }
        if (args.SelectedItem is NavigationViewItem item && item.Tag is string tag)
        {
            var page = tag switch
            {
                "servers" => typeof(ServersPage),
                "logs" => typeof(LogsPage),
                _ => typeof(HomePage),
            };
            ContentFrame.Navigate(page);
        }
    }
}
