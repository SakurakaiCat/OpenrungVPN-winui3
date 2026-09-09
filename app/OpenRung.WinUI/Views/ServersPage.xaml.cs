using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using OpenRung.WinUI.Services;

namespace OpenRung.WinUI.Views;

public sealed partial class ServersPage : Page
{
    public ViewModels.ServersViewModel ViewModel => App.Servers;

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

    private void AutoSelect_Click(object sender, RoutedEventArgs e) =>
        RelayDirectory.SelectLowestLatency(ViewModel);

    private async void Reload()
    {
        ViewModel.Loading = true;
        ViewModel.Error = null;
        try
        {
            await RelayDirectory.LoadAsync();
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
        // Always surface inline first: before the page has loaded (e.g. an
        // auto-refresh on navigation) XamlRoot is null and ContentDialog.
        // ShowAsync would throw and fail-fast the process.
        ViewModel.Error = message;
        if (XamlRoot is null || _errorDialogOpen)
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
