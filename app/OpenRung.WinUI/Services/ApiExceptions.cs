using System.Net;

namespace OpenRung.WinUI.Services;

/// <summary>A non-success response from the core API. <see cref="Code"/> carries the
/// machine-readable contract code (e.g. "already_connecting", "connected").</summary>
public class CoreApiException : Exception
{
    public HttpStatusCode StatusCode { get; }

    public string? Code { get; }

    public CoreApiException(HttpStatusCode statusCode, string? code, string message)
        : base(message)
    {
        StatusCode = statusCode;
        Code = code;
    }
}

/// <summary>HTTP 428 with code "elevation_required": the requested operation needs an
/// elevated core (TUN mode on Windows). The UI restarts the core as administrator.</summary>
public sealed class ElevationRequiredException : CoreApiException
{
    public ElevationRequiredException(string message)
        : base((HttpStatusCode)428, "elevation_required", message)
    {
    }
}
