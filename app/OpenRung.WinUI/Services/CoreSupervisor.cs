using System.Text.Json;
using OpenRung.WinUI.Models;

namespace OpenRung.WinUI.Services;

/// <summary>
/// UI-facing facade over the core: holds the latest <see cref="StateSnapshot"/>,
/// runs the SSE stream with reconnect/backoff, and exposes log entries and
/// relays for the pages. All callbacks fire on whatever thread delivers them —
/// callers marshal to the UI thread via DispatcherQueue.
/// </summary>
public sealed class CoreSupervisor : IAsyncDisposable
{
    private readonly CoreManager _core = new();
    private readonly object _gate = new();
    private CancellationTokenSource? _eventsCts;
    private Task? _eventsTask;
    private bool _disposed;

    public CoreManager Core => _core;

    public StateSnapshot? LatestState { get; private set; }

    public event EventHandler<StateSnapshot>? StateChanged;

    public event EventHandler<LogLine>? LogReceived;

    /// <summary>Raised when the stream drops and will be retried/restarted.</summary>
    public event EventHandler? ConnectionLost;

    /// <summary>Start (or adopt) the core, then begin the event stream. Idempotent.</summary>
    public async Task StartAsync(CancellationToken ct = default)
    {
        var api = await _core.EnsureRunningAsync(ct: ct).ConfigureAwait(false);
        // Seed state before the stream so the UI renders in one pass.
        try
        {
            var initial = await api.GetStateAsync(ct).ConfigureAwait(false);
            RaiseState(initial);
        }
        catch { /* stream reconnect loop will pick it up */ }
        RestartEventStream();
    }

    private void RestartEventStream()
    {
        lock (_gate)
        {
            _eventsCts?.Cancel();
            _eventsCts = new CancellationTokenSource();
            var ct = _eventsCts.Token;
            var api = _core.Api;
            var events = new CoreEventsClient(api.BaseAddress, _core.Endpoint!.Token);
            _eventsTask = Task.Run(() => RunEventLoopAsync(events, ct), ct);
        }
    }

    private async Task RunEventLoopAsync(CoreEventsClient events, CancellationToken ct)
    {
        var delay = TimeSpan.FromMilliseconds(250);
        while (!ct.IsCancellationRequested && !_disposed)
        {
            try
            {
                await events.RunAsync(DispatchEvent, ct).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested)
            {
                return;
            }
            catch
            {
                // transport error: fall through to reconnect
            }
            if (ct.IsCancellationRequested)
                return;
            AppLog.Write($"event stream dropped; reconnecting in {delay.TotalSeconds:0.#}s");
            ConnectionLost?.Invoke(this, EventArgs.Empty);
            await Task.Delay(delay, ct).ConfigureAwait(false);
            delay = TimeSpan.FromSeconds(Math.Min(delay.TotalSeconds * 2, 10));
        }
    }

    private void DispatchEvent(EventItem item)
    {
        switch (item.Event)
        {
            case "state":
                var state = item.As<StateSnapshot>();
                if (state is not null)
                    RaiseState(state);
                break;
            case "log":
                var log = item.As<LogLine>();
                if (log is not null)
                    LogReceived?.Invoke(this, log);
                break;
        }
    }

    private void RaiseState(StateSnapshot state)
    {
        LatestState = state;
        StateChanged?.Invoke(this, state);
    }

    /// <summary>Restart the core elevated and re-establish the stream. Used by the TUN flow.</summary>
    public async Task RestartElevatedAsync(CancellationToken ct = default)
    {
        lock (_gate)
        {
            _eventsCts?.Cancel();
        }
        AppLog.Write("restarting core elevated for TUN mode (UAC prompt expected)");
        await _core.RestartElevatedAsync(ct).ConfigureAwait(false);
        RestartEventStream();
        AppLog.Write("elevated core ready; event stream restarted");
    }

    public async ValueTask DisposeAsync()
    {
        _disposed = true;
        lock (_gate)
        {
            _eventsCts?.Cancel();
        }
        if (_eventsTask is not null)
        {
            try { await _eventsTask.ConfigureAwait(false); } catch { }
        }
        await _core.DisposeAsync().ConfigureAwait(false);
    }
}
