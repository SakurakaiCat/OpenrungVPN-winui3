using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml.Controls;
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
}

public sealed partial class SettingsPage : Page
{
    public SettingsViewModel ViewModel { get; } = new();

    private bool _suppressSelectionChanged;

    public SettingsPage()
    {
        InitializeComponent();

        Loaded += async (_, _) =>
        {
            // Reflect the persisted engine mode without firing the handler.
            _suppressSelectionChanged = true;
            ModeCombo.SelectedIndex = App.State.Mode == "tun" ? 1 : 0;
            _suppressSelectionChanged = false;

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

    private async void ModeCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_suppressSelectionChanged || ModeCombo.SelectedItem is not ComboBoxItem item)
            return;
        var mode = item.Tag as string ?? "proxy";

        try
        {
            await App.Supervisor.Core.Api.SetModeAsync(mode);
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
            }
            catch (Exception ex2)
            {
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
