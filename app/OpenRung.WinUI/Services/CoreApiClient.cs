using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using OpenRung.WinUI.Models;

namespace OpenRung.WinUI.Services;

/// <summary>
/// Typed client for the core's loopback JSON API (API-CONTRACT.md). One instance
/// per core process; when the core is restarted (e.g. elevated relaunch) a new
/// port/token arrives, so create a new client — this one is immutable.
/// </summary>
public sealed class CoreApiClient
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);

    private readonly HttpClient _http;

    public CoreApiClient(int port, string token, HttpClient? inner = null)
    {
        Port = port;
        // The core itself owns the system proxy (loopback sing-box listener).
        // Routing sidecar API traffic through the OS proxy would send our
        // POSTs into that listener, which rejects non-GET/CONNECT methods
        // with 405 - so loopback management traffic always goes direct.
        _http = inner ?? new HttpClient(new HttpClientHandler { UseProxy = false })
        {
            Timeout = TimeSpan.FromSeconds(100),
        };
        _http.DefaultRequestHeaders.Add("X-OpenRung-Token", token);
        BaseAddress = new Uri($"http://127.0.0.1:{port}");
    }

    public int Port { get; }

    public Uri BaseAddress { get; }

    public Task<VersionInfo> GetVersionAsync(CancellationToken ct = default) =>
        GetAsync<VersionInfo>("/api/version", ct);

    public Task<StateSnapshot> GetStateAsync(CancellationToken ct = default) =>
        GetAsync<StateSnapshot>("/api/state", ct);

    public Task<LogsResponse> GetLogsAsync(int tail = 200, CancellationToken ct = default) =>
        GetAsync<LogsResponse>($"/api/logs?tail={tail}", ct);

    public Task<RelaysResponse> GetRelaysAsync(string? broker = null, CancellationToken ct = default)
    {
        var url = "/api/relays?ranked=1";
        if (!string.IsNullOrEmpty(broker))
            url += "&broker=" + Uri.EscapeDataString(broker);
        return GetAsync<RelaysResponse>(url, ct);
    }

    public Task ConnectAsync(string? brokerUrl = null, string? relayId = null, string? country = null,
        CancellationToken ct = default) =>
        PostAsync("/api/connect", new ConnectRequest
        {
            BrokerUrl = brokerUrl ?? "",
            RelayId = relayId ?? "",
            Country = country ?? ""
        }, ct);

    public Task DisconnectAsync(CancellationToken ct = default) =>
        PostAsync("/api/disconnect", body: null, ct);

    public Task<OkResponse> SetModeAsync(string mode, CancellationToken ct = default) =>
        PostAsync<OkResponse>("/api/mode", new ModeRequest { Mode = mode }, ct);

    /// <summary>POST /api/heartbeat — 204 No Content on success.</summary>
    public Task HeartbeatAsync(CancellationToken ct = default) =>
        PostAsync("/api/heartbeat", body: null, ct);

    public Task ShutdownAsync(CancellationToken ct = default) =>
        PostAsync("/api/shutdown", body: null, ct);

    private async Task<T> GetAsync<T>(string url, CancellationToken ct)
    {
        using var resp = await _http.GetAsync(BaseAddress + url, ct).ConfigureAwait(false);
        return await ReadAsync<T>(resp, ct).ConfigureAwait(false);
    }

    private Task PostAsync(string url, object? body, CancellationToken ct) =>
        PostAsyncOk(url, body, ct);

    private async Task PostAsyncOk(string url, object? body, CancellationToken ct)
    {
        using var resp = await SendAsync(url, body, ct).ConfigureAwait(false);
        await EnsureSuccessAsync(resp, ct).ConfigureAwait(false);
    }

    private async Task<T> PostAsync<T>(string url, object? body, CancellationToken ct)
    {
        using var resp = await SendAsync(url, body, ct).ConfigureAwait(false);
        return await ReadAsync<T>(resp, ct).ConfigureAwait(false);
    }

    private Task<HttpResponseMessage> SendAsync(string url, object? body, CancellationToken ct)
    {
        HttpContent? content = body is null
            ? null
            : new StringContent(JsonSerializer.Serialize(body, Json),
                new MediaTypeHeaderValue("application/json"));
        return _http.PostAsync(BaseAddress + url, content, ct);
    }

    private static async Task<T> ReadAsync<T>(HttpResponseMessage resp, CancellationToken ct)
    {
        await EnsureSuccessAsync(resp, ct).ConfigureAwait(false);
        var json = await resp.Content.ReadAsStringAsync(ct).ConfigureAwait(false);
        var value = JsonSerializer.Deserialize<T>(json, Json);
        return value ?? throw new CoreApiException(resp.StatusCode, null, "empty response body");
    }

    /// <summary>
    /// Maps non-2xx to the typed exceptions the UI branches on: 428 with
    /// code "elevation_required" becomes <see cref="ElevationRequiredException"/>,
    /// everything else <see cref="CoreApiException"/> carrying the contract code.
    /// </summary>
    private static async Task EnsureSuccessAsync(HttpResponseMessage resp, CancellationToken ct)
    {
        if (resp.IsSuccessStatusCode)
            return;

        string? message = null;
        string? code = null;
        try
        {
            var json = await resp.Content.ReadAsStringAsync(ct).ConfigureAwait(false);
            var err = JsonSerializer.Deserialize<ErrorResponse>(json, Json);
            message = err?.Error;
            code = err?.Code;
        }
        catch { /* non-JSON error body: fall through with the status line */ }

        message ??= $"core returned HTTP {(int)resp.StatusCode}";
        if ((int)resp.StatusCode == 428 || code == "elevation_required")
            throw new ElevationRequiredException(message);
        throw new CoreApiException(resp.StatusCode, code, message);
    }
}
