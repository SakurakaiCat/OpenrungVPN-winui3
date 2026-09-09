using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using OpenRung.WinUI.Services;
using OpenRung.WinUI.ViewModels;

namespace OpenRung.WinUI.Views;

public sealed partial class ServersPage : Page
{
    public ServersViewModel ViewModel => App.Servers;

    // Guards re-entrancy: a dialog thrown while another is up throws.
    private bool _errorDialogOpen;

    public ServersPage()
    {
        InitializeComponent();
    }

    protected override void OnNavigatedTo(Microsoft.UI.Xaml.Navigation.NavigationEventArgs e)
    {
        base.OnNavigatedTo(e);
        if (ViewModel.Relays.Count == 0)
            Reload();
    }

    private void Refresh_Click(object sender, RoutedEventArgs e) => Reload();

    private async void Reload()
    {
        ViewModel.Loading = true;
        ViewModel.Error = null;
        try
        {
            var resp = await App.Supervisor.Core.Api.GetRelaysAsync();
            ViewModel.Relays.Clear();
            foreach (var r in resp.Relays)
                ViewModel.Relays.Add(r);
            var ranked = resp.Relays.Count(r => r.LatencyMs.HasValue);
            ViewModel.SummaryText = $"{resp.Relays.Count} 个节点，已测速 {ranked} 个";
        }
        catch (Exception ex)
        {
            await ShowErrorAsync(ex.Message);
        }
        finally
        {
            ViewModel.Loading = false;
        }
    }

    private async System.Threading.Tasks.Task ShowErrorAsync(string message)
    {
        if (_errorDialogOpen)
            return;
        _errorDialogOpen = true;
        try
        {
            var dialog = new ContentDialog
            {
                Title = "刷新失败",
                Content = message,
                CloseButtonText = "关闭",
                XamlRoot = XamlRoot,
            };
            await dialog.ShowAsync();
        }
        finally
        {
            _errorDialogOpen = false;
        }
    }
}
