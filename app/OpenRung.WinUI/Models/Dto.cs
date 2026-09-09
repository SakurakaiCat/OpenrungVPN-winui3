using System.Text.Json.Serialization;

namespace OpenRung.WinUI.Models;

// Every shape below mirrors API-CONTRACT.md field-for-field. Property names are
// pinned with JsonPropertyName; do not rename without updating the contract.

/// <summary>GET /api/state response (and the <c>state</c> SSE payload).</summary>
public sealed class StateSnapshot
{
    /// <summary>disconnected | preparing | connecting | connected | disconnecting | failed</summary>
    [JsonPropertyName("status")]
    public string Status { get; set; } = "disconnected";

    [JsonPropertyName("relayLabel")]
    public string? RelayLabel { get; set; }

    [JsonPropertyName("lastError")]
    public string? LastError { get; set; }

    /// <summary>proxy | tun</summary>
    [JsonPropertyName("mode")]
    public string Mode { get; set; } = "proxy";

    [JsonPropertyName("proxy")]
    public ProxyInfo? Proxy { get; set; }

    [JsonPropertyName("connection")]
    public ConnectionSnapshot? Connection { get; set; }

    [JsonPropertyName("recents")]
    public List<RecentNode> Recents { get; set; } = new();

    [JsonPropertyName("elevated")]
    public bool Elevated { get; set; }

    [JsonPropertyName("coreVersion")]
    public string CoreVersion { get; set; } = "";
}

public sealed class ProxyInfo
{
    [JsonPropertyName("host")]
    public string Host { get; set; } = "127.0.0.1";

    [JsonPropertyName("port")]
    public int Port { get; set; }
}

public sealed class ConnectionSnapshot
{
    [JsonPropertyName("relayId")]
    public string RelayId { get; set; } = "";

    [JsonPropertyName("label")]
    public string Label { get; set; } = "";

    [JsonPropertyName("countryCode")]
    public string CountryCode { get; set; } = "";

    /// <summary>direct | punch | wss</summary>
    [JsonPropertyName("transport")]
    public string Transport { get; set; } = "";

    [JsonPropertyName("frontId")]
    public string FrontId { get; set; } = "";

    [JsonPropertyName("startedAt")]
    public DateTimeOffset? StartedAt { get; set; }
}

public sealed class RecentNode
{
    [JsonPropertyName("countryCode")]
    public string CountryCode { get; set; } = "";

    [JsonPropertyName("label")]
    public string Label { get; set; } = "";

    [JsonPropertyName("latitude")]
    public double Latitude { get; set; }

    [JsonPropertyName("longitude")]
    public double Longitude { get; set; }
}

/// <summary>One relay row in GET /api/relays.</summary>
public sealed class RelayItem
{
    [JsonPropertyName("id")]
    public string Id { get; set; } = "";

    [JsonPropertyName("label")]
    public string Label { get; set; } = "";

    [JsonPropertyName("country")]
    public string Country { get; set; } = "";

    [JsonPropertyName("countryCode")]
    public string CountryCode { get; set; } = "";

    [JsonPropertyName("city")]
    public string City { get; set; } = "";

    /// <summary>foundation | volunteer</summary>
    [JsonPropertyName("nodeClass")]
    public string NodeClass { get; set; } = "";

    [JsonPropertyName("punchCapable")]
    public bool PunchCapable { get; set; }

    [JsonPropertyName("maxMbps")]
    public int MaxMbps { get; set; }

    /// <summary>null when not probed or the probe failed.</summary>
    [JsonPropertyName("latencyMs")]
    public long? LatencyMs { get; set; }
}

public sealed class RelaysResponse
{
    [JsonPropertyName("serverTime")]
    public DateTimeOffset? ServerTime { get; set; }

    [JsonPropertyName("relays")]
    public List<RelayItem> Relays { get; set; } = new();
}

public sealed class LogLine
{
    [JsonPropertyName("time")]
    public string Time { get; set; } = "";

    [JsonPropertyName("line")]
    public string Line { get; set; } = "";
}

public sealed class LogsResponse
{
    [JsonPropertyName("logs")]
    public List<LogLine> Logs { get; set; } = new();
}

/// <summary>GET /api/version response.</summary>
public sealed class VersionInfo
{
    [JsonPropertyName("core")]
    public string Core { get; set; } = "";

    [JsonPropertyName("engine")]
    public string Engine { get; set; } = "";

    [JsonPropertyName("os")]
    public string Os { get; set; } = "";

    [JsonPropertyName("arch")]
    public string Arch { get; set; } = "";

    [JsonPropertyName("elevated")]
    public bool Elevated { get; set; }
}

/// <summary>POST /api/connect body; every field optional, empty = auto-select.</summary>
public sealed class ConnectRequest
{
    [JsonPropertyName("brokerUrl")]
    public string BrokerUrl { get; set; } = "";

    [JsonPropertyName("relayId")]
    public string RelayId { get; set; } = "";

    [JsonPropertyName("country")]
    public string Country { get; set; } = "";
}

/// <summary>POST /api/mode body.</summary>
public sealed class ModeRequest
{
    [JsonPropertyName("mode")]
    public string Mode { get; set; } = "";
}

/// <summary>Generic {"ok":true[, "mode":"..."]} response.</summary>
public sealed class OkResponse
{
    [JsonPropertyName("ok")]
    public bool Ok { get; set; }

    [JsonPropertyName("mode")]
    public string? Mode { get; set; }
}

/// <summary>Error envelope {"error":"...","code":"..."} shared by every endpoint.</summary>
public sealed class ErrorResponse
{
    [JsonPropertyName("error")]
    public string Error { get; set; } = "";

    [JsonPropertyName("code")]
    public string? Code { get; set; }
}

/// <summary>On-disk endpoint discovery file written by the core.</summary>
public sealed class CoreEndpoint
{
    [JsonPropertyName("port")]
    public int Port { get; set; }

    [JsonPropertyName("token")]
    public string Token { get; set; } = "";

    [JsonPropertyName("pid")]
    public int Pid { get; set; }

    [JsonPropertyName("startedAt")]
    public DateTimeOffset? StartedAt { get; set; }
}

/// <summary>One parsed Server-Sent Events frame from GET /api/events.</summary>
public sealed class EventItem
{
    /// <summary>Event name: "state" or "log" (":ping" keepalives are filtered out).</summary>
    public string Event { get; set; } = "";

    /// <summary>Raw JSON payload of the data: line(s).</summary>
    public string Data { get; set; } = "";
}
