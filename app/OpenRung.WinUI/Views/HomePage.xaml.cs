using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using OpenRung.WinUI.Services;
using OpenRung.WinUI.ViewModels;

namespace OpenRung.WinUI.Views;

public sealed partial class HomePage : Page
{
    public AppStateViewModel ViewModel => App.State;

    public HomePage()
    {
        InitializeComponent();
    }

    private async void ConnectButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (ViewModel.IsConnected)
            {
                await App.Supervisor.Core.Api.DisconnectAsync();
                return;
            }

            var server = App.Servers.Selected;
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
            await App.Supervisor.Core.Api.ConnectAsync(relayId: App.Servers.Selected?.Id);
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
