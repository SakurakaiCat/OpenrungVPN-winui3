using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using OpenRung.WinUI.Models;
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

    /// <summary>
    /// Titles each relay 国家+地区+编号 (e.g. 日本东京1, or 日本1 when the
    /// broker has no city), numbering within each (country, city) group in
    /// directory order. Set before items bind so x:Bind OneTime picks it up.
    /// </summary>
    private static void AssignDisplayTitles(List<RelayItem> relays)
    {
        var counters = new Dictionary<(string, string), int>();
        foreach (var relay in relays)
        {
            var country = CountryName(relay);
            var city = CityNames.Localize(relay.City);
            // "新加坡新加坡1" reads duplicated; skip a city that matches its country.
            if (city == country)
                city = "";
            var key = (relay.CountryCode ?? "", relay.City ?? "");
            var n = counters.TryGetValue(key, out var seen) ? seen + 1 : 1;
            counters[key] = n;
            relay.DisplayTitle = city.Length == 0 ? $"{country}{n}" : $"{country}{city}{n}";
        }
    }

    private static string CountryName(RelayItem relay)
    {
        var code = relay.CountryCode;
        if (!string.IsNullOrEmpty(code) && code.Length == 2
            && CountryNames.Map.TryGetValue(code.ToUpperInvariant(), out var zh))
            return zh;
        return string.IsNullOrWhiteSpace(relay.Country) ? "节点" : relay.Country.Trim();
    }

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
            AssignDisplayTitles(resp.Relays);
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
