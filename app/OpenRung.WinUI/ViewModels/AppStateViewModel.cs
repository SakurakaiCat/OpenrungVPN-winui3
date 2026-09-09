using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Dispatching;
using OpenRung.WinUI.Models;
using OpenRung.WinUI.Services;

namespace OpenRung.WinUI.ViewModels;

/// <summary>
/// Shared app state: the latest StateSnapshot from the SSE stream, surfaced in
/// UI-friendly form. Pages bind to this; App owns the <see cref="CoreSupervisor"/>
/// and pumps events in through <see cref="ApplyState"/>.
/// </summary>
public partial class AppStateViewModel : ObservableObject
{
    private readonly DispatcherQueue _ui;

    public AppStateViewModel(DispatcherQueue ui)
    {
        _ui = ui;
    }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsConnected))]
    [NotifyPropertyChangedFor(nameof(IsBusy))]
    [NotifyPropertyChangedFor(nameof(NotBusy))]
    [NotifyPropertyChangedFor(nameof(StatusGlyph))]
    [NotifyPropertyChangedFor(nameof(ConnectButtonText))]
    private string _status = "disconnected";

    [ObservableProperty]
    private string? _relayLabel;

    [ObservableProperty]
    private string? _lastError;

    [ObservableProperty]
    private string _mode = "proxy";

    [ObservableProperty]
    private string _proxyText = "";

    [ObservableProperty]
    private string _transportText = "";

    [ObservableProperty]
    private string _elapsedText = "";

    [ObservableProperty]
    private bool _elevated;

    private DateTimeOffset? _connectedAt;

    public bool IsConnected => Status == "connected";

    public bool IsBusy => Status is "preparing" or "connecting" or "disconnecting";

    public bool NotBusy => !IsBusy;

    public string StatusGlyph => Status switch
    {
        "connected" => "\uEA18",   // CheckMark-ish filled circle
        "preparing" or "connecting" => "\uE15E",
        "disconnecting" => "\uE15E",
        "failed" => "\uEA39",
        _ => "\uE81E",
    };

    public string ConnectButtonText => IsConnected ? "断开连接" : (IsBusy ? "……" : "连接");

    /// <summary>Replace the local view of the core state (from SSE or polling).</summary>
    public void ApplyState(StateSnapshot s)
    {
        _ui.TryEnqueue(() =>
        {
            Status = s.Status;
            RelayLabel = s.RelayLabel;
            LastError = s.LastError;
            Mode = s.Mode;
            Elevated = s.Elevated;
            ProxyText = s.Proxy is not null ? $"{s.Proxy.Host}:{s.Proxy.Port}" : "";
            TransportText = s.Connection is not null ? s.Connection.Transport : "";
            _connectedAt = s.Connection?.StartedAt;
        });
    }

    /// <summary>Called once per second by the shell's timer while connected.</summary>
    public void Tick()
    {
        if (_connectedAt is { } t)
        {
            var span = DateTimeOffset.UtcNow - t;
            _ui.TryEnqueue(() =>
                ElapsedText = $"{(int)span.TotalHours:D2}:{span.Minutes:D2}:{span.Seconds:D2}");
        }
    }
}
