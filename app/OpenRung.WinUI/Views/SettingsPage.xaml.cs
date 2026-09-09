using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using OpenRung.WinUI.Models;
using OpenRung.WinUI.Services;

namespace OpenRung.WinUI.Views;

public partial class SettingsViewModel : ObservableObject
{
    [ObservableProperty]
    private string _coreVersion = "";

    [ObservableProperty]
    private string _engineLine = "";

    [ObservableProperty]
    private string _endpointText = "";

    /// <summary>Current OS system proxy ("host:port" / PAC) or "无".</summary>
    [ObservableProperty]
    private string _systemProxyText = "无";

    [ObservableProperty]
    private bool _autoClearProxy;

    [ObservableProperty]
    private bool _proxyBusy;

    public bool NotProxyBusy => !ProxyBusy;

    partial void OnProxyBusyChanged(bool value) => OnPropertyChanged(nameof(NotProxyBusy));
}

public sealed partial class SettingsPage : Page
{
    public SettingsViewModel ViewModel { get; } = new();

    private bool _suppressSelectionChanged;
    private bool _suppressAutoClear;

    public SettingsPage()
    {
        InitializeComponent();

        Loaded += async (_, _) =>
        {
            // Reflect the persisted engine mode without firing the handler.
            _suppressSelectionChanged = true;
            ModeCombo.SelectedIndex = App.State.Mode == "tun" ? 1 : 0;
            _suppressSelectionChanged = false;

            // Persisted auto-clear preference + live system-proxy display.
            _suppressAutoClear = true;
            ViewModel.AutoClearProxy = AppSettings.Load().AutoClearProxy;
            _suppressAutoClear = false;
            ViewModel.SystemProxyText = ProxyText(App.State.SystemProxy);
            App.Supervisor.StateChanged += OnStateChanged;
            Unloaded += OnPageUnloaded;

            try
            {
                var v = await App.Supervisor.Core.Api.GetVersionAsync();
                ViewModel.CoreVersion = $"OpenRung Core {v.Core}";
                ViewModel.EngineLine = v.Engine;
                var ep = App.Supervisor.Core.Endpoint;
                if (ep is not null)
                    ViewModel.EndpointText = $"127.0.0.1:{ep.Port}（PID {ep.Pid}）";
            }
            catch { /* page is informational only */ }
        };
    }

    private static string ProxyText(string? systemProxy) =>
        string.IsNullOrEmpty(systemProxy) ? "无" : systemProxy;

    private void OnPageUnloaded(object sender, RoutedEventArgs e)
    {
        Unloaded -= OnPageUnloaded;
        App.Supervisor.StateChanged -= OnStateChanged;
    }

    private void OnStateChanged(object? sender, StateSnapshot state)
    {
        var text = ProxyText(state.SystemProxy);
        if (ViewModel.SystemProxyText != text)
            ViewModel.SystemProxyText = text;
    }

    private void PersistAutoClear(bool value)
    {
        var s = AppSettings.Load();
        s.AutoClearProxy = value;
        s.Save();
    }

    private async void AutoClearProxy_Checked(object sender, RoutedEventArgs e)
    {
        if (_suppressAutoClear)
            return;
        PersistAutoClear(true);
        await ClearNowAsync("auto-clear enabled");
    }

    private void AutoClearProxy_Unchecked(object sender, RoutedEventArgs e)
    {
        if (_suppressAutoClear)
            return;
        PersistAutoClear(false);
        AppLog.Write("auto-clear system proxy disabled");
    }

    private async void ClearProxy_Click(object sender, RoutedEventArgs e) =>
        await ClearNowAsync("user request");

    private async Task ClearNowAsync(string reason)
    {
        if (ViewModel.ProxyBusy)
            return;
        ViewModel.ProxyBusy = true;
        try
        {
            await App.Supervisor.Core.Api.ClearSystemProxyAsync();
            AppLog.Write($"system proxy cleared ({reason})");
        }
        catch (Exception ex)
        {
            AppLog.Write($"clear system proxy failed: {ex.Message}");
        }
        finally
        {
            ViewModel.ProxyBusy = false;
        }
    }

    private async void ModeCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_suppressSelectionChanged || ModeCombo.SelectedItem is not ComboBoxItem item)
            return;
        var mode = item.Tag as string ?? "proxy";

        try
        {
            await App.Supervisor.Core.Api.SetModeAsync(mode);
            AppLog.Write($"mode switched to {mode}");
        }
        catch (ElevationRequiredException ex)
        {
            _suppressSelectionChanged = true;
            ModeCombo.SelectedIndex = App.State.Mode == "tun" ? 1 : 0;
            _suppressSelectionChanged = false;

            var dialog = new ContentDialog
            {
                Title = "需要管理员权限",
                Content = $"{ex.Message}\n\n是否重启内核为管理员模式并启用 TUN？",
                PrimaryButtonText = "重启核心",
                CloseButtonText = "取消",
                DefaultButton = ContentDialogButton.Primary,
                XamlRoot = XamlRoot,
            };
            if (await dialog.ShowAsync() != ContentDialogResult.Primary)
                return;

            try
            {
                await App.Supervisor.RestartElevatedAsync();
                await App.Supervisor.Core.Api.SetModeAsync("tun");
                _suppressSelectionChanged = true;
                ModeCombo.SelectedIndex = 1;
                _suppressSelectionChanged = false;
            }
            catch (OperationCanceledException)
            {
                // user declined UAC; the combo already shows the old mode
                AppLog.Write("elevation cancelled by user (UAC declined)");
            }
            catch (Exception ex2)
            {
                AppLog.Write($"elevated restart failed: {ex2.Message}");
                var err = new ContentDialog
                {
                    Title = "重启失败",
                    Content = ex2.Message,
                    CloseButtonText = "关闭",
                    XamlRoot = XamlRoot,
                };
                await err.ShowAsync();
            }
        }
        catch (CoreApiException ex)
        {
            // Most likely 409 "connected": disconnect first on Home.
            var dialog = new ContentDialog
            {
                Title = "无法切换模式",
                Content = ex.Message,
                CloseButtonText = "关闭",
                XamlRoot = XamlRoot,
            };
            await dialog.ShowAsync();
            _suppressSelectionChanged = true;
            ModeCombo.SelectedIndex = App.State.Mode == "tun" ? 1 : 0;
            _suppressSelectionChanged = false;
        }
        catch (System.Net.Http.HttpRequestException)
        {
            // Connection-level failure (core dead / restarting): refused
            // sockets surface here, not as CoreApiException.
            _suppressSelectionChanged = true;
            ModeCombo.SelectedIndex = App.State.Mode == "tun" ? 1 : 0;
            _suppressSelectionChanged = false;
        }
    }
}
