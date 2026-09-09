using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using OpenRung.WinUI.Models;

namespace OpenRung.WinUI.ViewModels;

/// <summary>
/// Servers page state: the ranked relay directory plus the selection the user
/// wants connect calls to target. Loading is driven from the page's code-behind
/// button so view models stay passively bindable.
/// </summary>
public partial class ServersViewModel : ObservableObject
{
    [ObservableProperty]
    private ObservableCollection<RelayItem> _relays = new();

    [ObservableProperty]
    private RelayItem? _selected;

    [ObservableProperty]
    private bool _loading;

    [ObservableProperty]
    private string? _error;

    [ObservableProperty]
    private string _summaryText = "";
}

/// <summary>
/// Logs page state. The stream appends in real time; the collection is capped
/// so a long session does not grow memory without bound.
/// </summary>
public partial class LogsViewModel : ObservableObject
{
    private const int MaxLines = 1000;

    public ObservableCollection<string> Lines { get; } = new();

    partial void OnAutoScrollChanged(bool value) { /* page reads the property when appending */ }

    [ObservableProperty]
    private bool _autoScroll = true;

    public void Append(string line)
    {
        Lines.Add(line);
        if (Lines.Count > MaxLines)
            Lines.RemoveAt(0);
    }

    public void Clear() => Lines.Clear();
}
