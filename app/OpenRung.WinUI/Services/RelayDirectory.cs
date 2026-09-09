using OpenRung.WinUI.Models;
using OpenRung.WinUI.ViewModels;

namespace OpenRung.WinUI.Services;

/// <summary>
/// Shared relay-directory loading for the home server card and the servers
/// page: fetches the ranked directory (broker latency sweep) from the core,
/// assigns the 国家+地区+编号 display titles, fills <see cref="App.Servers"/>,
/// and auto-selects the lowest-latency relay when nothing is selected yet.
/// </summary>
public static class RelayDirectory
{
    /// <summary>Fetch and publish the relay directory. Throws; callers own the error surface.</summary>
    public static async System.Threading.Tasks.Task LoadAsync()
    {
        var resp = await App.Supervisor.Core.Api.GetRelaysAsync().ConfigureAwait(true);
        AssignDisplayTitles(resp.Relays);

        var vm = App.Servers;
        vm.Relays.Clear();
        foreach (var r in resp.Relays)
            vm.Relays.Add(r);

        var ranked = resp.Relays.Count(r => r.LatencyMs.HasValue);
        vm.SummaryText = $"{resp.Relays.Count} 个节点，已测速 {ranked} 个";

        if (vm.Selected is null)
            SelectLowestLatency(vm);
    }

    /// <summary>Select the lowest-latency probed relay; no probed relay → no change.</summary>
    public static void SelectLowestLatency(ServersViewModel vm)
    {
        vm.Selected = vm.Relays
            .Where(r => r.LatencyMs.HasValue)
            .OrderBy(r => r.LatencyMs!.Value)
            .FirstOrDefault();
    }

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
}
