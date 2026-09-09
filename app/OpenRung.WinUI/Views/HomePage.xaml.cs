using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using OpenRung.WinUI.Services;
using OpenRung.WinUI.ViewModels;

namespace OpenRung.WinUI.Views;

public sealed partial class HomePage : Page
{
    public AppStateViewModel ViewModel => App.State;

    /// <summary>Relay directory state, bound by the home server card.</summary>
    public ServersViewModel Servers => App.Servers;

    public HomePage()
    {
        InitializeComponent();
    }

    protected override void OnNavigatedTo(Microsoft.UI.Xaml.Navigation.NavigationEventArgs e)
    {
        base.OnNavigatedTo(e);
        if (Servers.Relays.Count == 0)
            _ = LoadRelaysAsync();
    }

    // Fire-and-forget initial load: the home card surfaces failures inline
    // (Servers.Error) instead of a dialog, so no XamlRoot is needed.
    private async System.Threading.Tasks.Task LoadRelaysAsync()
    {
        Servers.Loading = true;
        Servers.Error = null;
        try
        {
            await RelayDirectory.LoadAsync();
        }
        catch (Exception ex)
        {
            Servers.Error = ex.Message;
        }
        finally
        {
            Servers.Loading = false;
        }
    }

    private async void Refresh_Click(object sender, RoutedEventArgs e) => await LoadRelaysAsync();

    private void AutoSelect_Click(object sender, RoutedEventArgs e) =>
        RelayDirectory.SelectLowestLatency(Servers);

    private async void ConnectButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (ViewModel.IsConnected)
            {
                await App.Supervisor.Core.Api.DisconnectAsync();
                return;
            }

            var server = Servers.Selected;
            await App.Supervisor.Core.Api.ConnectAsync(relayId: server?.Id);
        }
        catch (ElevationRequiredException ex)
        {
            await OfferElevatedRestartAsync(ex.Message);
        }
        catch (CoreApiException ex)
        {
            ShowError(ex.Message);
        }
        catch (System.Net.Http.HttpRequestException)
        {
            // Connection-level failure (core dead / restarting): PostAsyncOk only
            // wraps HTTP error responses, refused sockets surface here.
            ShowError("无法连接核心进程，请稍后重试。");
        }
    }

    private void ShowError(string message)
    {
        ErrorBar.Message = message;
        ErrorBar.IsOpen = true;
    }

    // 428 path from the contract: offer to relaunch the core as Administrator,
    // then retry the same mode/connect after the restart.
    private async Task OfferElevatedRestartAsync(string why)
    {
        var dialog = new ContentDialog
        {
            Title = "需要管理员权限",
            Content = $"TUN 模式需要以管理员身份运行核心进程。\n\n{why}",
            PrimaryButtonText = "以管理员身份重启核心",
            CloseButtonText = "取消",
            DefaultButton = ContentDialogButton.Primary,
            XamlRoot = XamlRoot,
        };
        if (await dialog.ShowAsync() != ContentDialogResult.Primary)
            return;

        try
        {
            await App.Supervisor.RestartElevatedAsync();
            await App.Supervisor.Core.Api.ConnectAsync(relayId: Servers.Selected?.Id);
        }
        catch (OperationCanceledException)
        {
            ShowError("已取消管理员授权。");
        }
        catch (Exception ex)
        {
            ShowError(ex.Message);
        }
    }
}
