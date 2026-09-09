using System.Text.Json;
using OpenRung.WinUI.Models;

namespace OpenRung.WinUI.Services;

/// <summary>
/// Streams GET /api/events (SSE): parses <c>event:/data:</c> frames, replays the
/// initial state + log backlog the core sends, and yields live items until the
/// stream drops or the token is cancelled. Auto-reconnect is left to the caller
/// (<see cref="CoreSupervisor"/> restarts it with backoff).
/// </summary>
public sealed class CoreEventsClient
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);

    private readonly Uri _baseAddress;
    private readonly string _token;

    public CoreEventsClient(Uri baseAddress, string token)
    {
        _baseAddress = baseAddress;
        _token = token;
    }

    /// <summary>
    /// Runs the stream until cancelled, raising typed events as frames arrive.
    /// Returns normally on a clean server-side close; the caller decides whether
    /// to reconnect.
    /// </summary>
    public async Task RunAsync(Action<EventItem> onEvent, CancellationToken ct)
    {
        // No default timeout for SSE — it is a long-lived stream. Only connect
        // is bounded, via the cancellation token the supervisor passes in.
        // Same reason as CoreApiClient: loopback API traffic must bypass the
        // OS proxy the core itself owns, or the SSE GET is proxied through
        // the tunnel listener and dies with the tunnel.
        using var http = new HttpClient(new HttpClientHandler { UseProxy = false })
        {
            Timeout = System.Threading.Timeout.InfiniteTimeSpan,
        };
        http.DefaultRequestHeaders.Add("X-OpenRung-Token", _token);

        using var resp = await http.GetAsync(_baseAddress + "/api/events",
            HttpCompletionOption.ResponseHeadersRead, ct).ConfigureAwait(false);
        resp.EnsureSuccessStatusCode();

        await using var stream = await resp.Content.ReadAsStreamAsync(ct).ConfigureAwait(false);
        using var reader = new StreamReader(stream);

        string? eventName = null;
        var data = new System.Text.StringBuilder();

        while (!ct.IsCancellationRequested)
        {
            var line = await reader.ReadLineAsync(ct).ConfigureAwait(false);
            if (line is null)
                break; // clean server close
            if (line.Length == 0)
            {
                if (eventName is not null && data.Length > 0)
                {
                    // trim trailing \n joined parts
                    var payload = data.ToString();
                    onEvent(new EventItem { Event = eventName, Data = payload.TrimEnd('\n') });
                }
                eventName = null;
                data.Clear();
                continue;
            }
            if (line[0] == ':')
                continue; // comment/keepalive
            if (line.StartsWith("event:", StringComparison.Ordinal))
            {
                eventName = line["event:".Length..].Trim();
            }
            else if (line.StartsWith("data:", StringComparison.Ordinal))
            {
                if (data.Length > 0)
                    data.Append('\n');
                var part = line["data:".Length..];
                data.Append(part.StartsWith(' ') ? part[1..] : part);
            }
        }
    }
}

public static class EventItemJson
{
    /// <summary>Deserializes an SSE data payload to the typed contract shape.</summary>
    public static T? As<T>(this EventItem item)
    {
        var json = new JsonSerializerOptions(JsonSerializerDefaults.Web);
        return JsonSerializer.Deserialize<T>(item.Data, json);
    }
}
