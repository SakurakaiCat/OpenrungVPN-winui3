using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using OpenRung.WinUI.ViewModels;
using Windows.ApplicationModel.DataTransfer;

namespace OpenRung.WinUI.Views;

public sealed partial class LogsPage : Page
{
    public LogsViewModel ViewModel => App.Logs;

    public LogsPage()
    {
        InitializeComponent();
        ViewModel.Lines.CollectionChanged += (_, _) =>
        {
            if (ViewModel.AutoScroll && LogList.Items.Count > 0)
                LogList.ScrollIntoView(ViewModel.Lines[^1]);
        };
        // Seed: the stream replays backlog before live lines; this page may be
        // opened later, so pull a tail snapshot on first navigation.
        Loaded += async (_, _) =>
        {
            if (ViewModel.Lines.Count > 0)
                return;
            try
            {
                var logs = await App.Supervisor.Core.Api.GetLogsAsync(500);
                foreach (var l in logs.Logs)
                    ViewModel.Append($"[{l.Time}] {l.Line}");
            }
            catch { /* engine unreachable; stream will feed live lines anyway */ }
        };
    }

    private void Clear_Click(object sender, RoutedEventArgs e) => ViewModel.Clear();

    private void Copy_Click(object sender, RoutedEventArgs e)
    {
        var dp = new DataPackage();
        dp.SetText(string.Join(Environment.NewLine, ViewModel.Lines));
        Clipboard.SetContent(dp);
    }
}
