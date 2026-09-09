using System.Diagnostics;
using System.Text.Json;
using OpenRung.WinUI.Models;

namespace OpenRung.WinUI.Services;

/// <summary>
/// Owns the openrung-core.exe child process: launch (or elevated relaunch),
/// endpoint-file discovery, heartbeat, and graceful shutdown. The engine lives
/// in the core; this class only keeps it alive next to the UI and hands out a
/// ready-to-use <see cref="CoreApiClient"/>/<see cref="CoreEventsClient"/>.
///
/// Elevation strategy: the app itself never elevates; when TUN
/// mode needs it, the running core is asked to shut down and a new one is
/// spawned with ShellExecute + runas, which shows the one UAC prompt.
/// </summary>
public sealed class CoreManager : IAsyncDisposable
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);

    private static readonly TimeSpan HeartbeatInterval = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan StartupTimeout = TimeSpan.FromSeconds(15);

    private Process? _process;
    private CoreApiClient? _api;
    private EndpointInfo? _endpoint;
    private CancellationTokenSource? _heartbeatCts;
    private bool _intentionalStop;

    public CoreApiClient Api => _api ?? throw new InvalidOperationException("core is not running");

    public EndpointInfo? Endpoint => _endpoint;

    public event EventHandler? CoreExited;

    /// <summary>Locate the core executable next to the app (dist layout: core\openrung-core.exe).</summary>
    public static string CoreExePath
    {
        get
        {
            var dir = AppContext.BaseDirectory;
            var sibling = Path.Combine(dir, "core", "openrung-core.exe");
            if (File.Exists(sibling))
                return sibling;
            // dev convenience: <repo>\dist\core\openrung-core.exe from a source checkout
            var up = Path.GetFullPath(Path.Combine(dir, "..", "..", "..", "..", "..", "dist", "core", "openrung-core.exe"));
            return up;
        }
    }

    /// <summary>
    /// Ensures the core is running. Adopts a healthy core already on the
    /// endpoint file (e.g. an earlier elevated respawn), otherwise spawns one.
    /// Returns when the API answers, or throws.
    /// </summary>
    public async Task<CoreApiClient> EnsureRunningAsync(bool elevated = false, CancellationToken ct = default)
    {
        // Adopt path: endpoint file names a live core the app (re)started.
        if (_process is null || _process.HasExited)
        {
            var adopted = await TryAdoptAsync(ct).ConfigureAwait(false);
            if (adopted is not null)
                return adopted;
        }
        else if (_api is not null)
        {
            return _api;
        }

        _intentionalStop = false;
        var psi = new ProcessStartInfo
        {
            FileName = CoreExePath,
            WorkingDirectory = Path.GetDirectoryName(CoreExePath)!,
            UseShellExecute = elevated,
            CreateNoWindow = true,
            RedirectStandardOutput = !elevated,
            RedirectStandardError = !elevated,
        };
        // The core is the proxy itself: its own outbound (broker discovery, relay
        // dials, telemetry) must never traverse the OS system proxy, or it loops
        // into our own loopback inbound or a third-party proxy the user runs
        // alongside. Belt and braces on top of the library-side Proxy=nil
        // transports — the env covers any client a future code path forgets.
        psi.Environment["NO_PROXY"] = "*";
        psi.Environment["no_proxy"] = "*";
        psi.ArgumentList.Add("serve");
        psi.ArgumentList.Add("--heartbeat-timeout");
        psi.ArgumentList.Add("45s");
        if (elevated)
            psi.Verb = "runas";

        Process? proc;
        try
        {
            proc = Process.Start(psi);
        }
        catch (System.ComponentModel.Win32Exception ex) when (elevated && ex.NativeErrorCode == 1223)
        {
            // ERROR_CANCELLED: user declined the UAC prompt.
            throw new OperationCanceledException("elevation cancelled by user", ex);
        }
        if (proc is null)
            throw new InvalidOperationException("failed to launch openrung-core.exe");

        _process = proc;
        _process.EnableRaisingEvents = true;
        _process.Exited += (_, _) =>
        {
            if (!_intentionalStop)
            {
                AppLog.Write($"core exited unexpectedly (code {_process.ExitCode})");
                CoreExited?.Invoke(this, EventArgs.Empty);
            }
        };
        // Core writes its own startup lines to stdout/stderr; drain or the
        // buffer fills and blocks the child once the tunnel gets chatty.
        if (!elevated)
        {
            _ = DrainAsync(proc.StandardOutput);
            _ = DrainAsync(proc.StandardError);
        }

        _endpoint = await WaitForEndpointAsync(ct).ConfigureAwait(false);
        _api = new CoreApiClient(_endpoint.Port, _endpoint.Token);
        AppLog.Write($"core {(elevated ? "restarted elevated" : "started")}: 127.0.0.1:{_endpoint.Port} (PID {_endpoint.Pid})");
        StartHeartbeat();
        return _api;
    }

    /// <summary>Read the endpoint file and liveness-probe the core it names.</summary>
    private async Task<CoreApiClient?> TryAdoptAsync(CancellationToken ct)
    {
        var ep = ReadEndpointFile();
        if (ep is null)
            return null;
        try
        {
            var api = new CoreApiClient(ep.Port, ep.Token);
            await api.GetVersionAsync(ct).ConfigureAwait(false);
            if (ProcessAlive(ep.Pid))
            {
                _endpoint = ep;
                _api = api;
                AppLog.Write($"core already running, adopted: 127.0.0.1:{ep.Port} (PID {ep.Pid})");
                StartHeartbeat();
                return api;
            }
        }
        catch { /* stale endpoint file; fall through to spawn */ }
        return null;
    }

    private static bool ProcessAlive(int pid)
    {
        try
        {
            using var p = Process.GetProcessById(pid);
            return !p.HasExited && p.ProcessName.Contains("openrung", StringComparison.OrdinalIgnoreCase);
        }
        catch { return false; }
    }

    private async Task<EndpointInfo> WaitForEndpointAsync(CancellationToken ct)
    {
        // The core writes the file within milliseconds; allow time for slow AV
        // scans on the freshly written exe.
        var deadline = DateTime.UtcNow + StartupTimeout;
        while (DateTime.UtcNow < deadline)
        {
            ct.ThrowIfCancellationRequested();
            if (_process is { HasExited: true })
                throw new InvalidOperationException($"core exited during startup (code {_process.ExitCode})");
            var ep = ReadEndpointFile();
            if (ep is not null)
            {
                try
                {
                    await new CoreApiClient(ep.Port, ep.Token).GetVersionAsync(ct).ConfigureAwait(false);
                    return ep;
                }
                catch { /* file written before listener accepts; retry */ }
            }
            await Task.Delay(150, ct).ConfigureAwait(false);
        }
        throw new TimeoutException("core did not publish its endpoint file in time");
    }

    private static EndpointInfo? ReadEndpointFile()
    {
        try
        {
            var path = EndpointFilePath;
            if (!File.Exists(path))
                return null;
            var dto = JsonSerializer.Deserialize<CoreEndpoint>(File.ReadAllText(path), Json);
            if (dto is null || dto.Port == 0 || string.IsNullOrEmpty(dto.Token))
                return null;
            return new EndpointInfo(dto.Port, dto.Token, dto.Pid, dto.StartedAt);
        }
        catch { return null; }
    }

    public static string EndpointFilePath
    {
        get
        {
            // Mirrors core/server.go: %LOCALAPPDATA%\OpenRung on Windows,
            // os.UserConfigDir()/openrung elsewhere.
            if (OperatingSystem.IsWindows())
                return Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                    "OpenRung", "core-endpoint.json");
            var configHome = Environment.GetEnvironmentVariable("XDG_CONFIG_HOME");
            if (string.IsNullOrEmpty(configHome))
                configHome = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".config");
            return Path.Combine(configHome, "openrung", "core-endpoint.json");
        }
    }

    private void StartHeartbeat()
    {
        _heartbeatCts?.Cancel();
        _heartbeatCts = new CancellationTokenSource();
        var ct = _heartbeatCts.Token;
        _ = Task.Run(async () =>
        {
            while (!ct.IsCancellationRequested)
            {
                await Task.Delay(HeartbeatInterval, ct).ConfigureAwait(false);
                if (ct.IsCancellationRequested || _api is null)
                    return;
                try
                {
                    await _api.HeartbeatAsync(ct).ConfigureAwait(false);
                }
                catch
                {
                    if (!_intentionalStop)
                    {
                        AppLog.Write("heartbeat failed; core unreachable");
                        CoreExited?.Invoke(this, EventArgs.Empty);
                    }
                    return;
                }
            }
        }, ct);
    }

    /// <summary>Politely ask the core to exit (contract /api/shutdown), then wait.</summary>
    public async Task StopAsync()
    {
        _intentionalStop = true;
        _heartbeatCts?.Cancel();
        if (_api is not null)
        {
            try
            {
                using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(3));
                await _api.ShutdownAsync(cts.Token).ConfigureAwait(false);
            }
            catch { /* core already gone */ }
        }
        if (_process is { HasExited: false })
        {
            try
            {
                using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(6));
                await _process.WaitForExitAsync(cts.Token).ConfigureAwait(false);
            }
            catch
            {
                try { _process.Kill(entireProcessTree: true); } catch { }
            }
        }
        _process = null;
        _api = null;
        _endpoint = null;
        AppLog.Write("core stopped");
    }

    /// <summary>Restart the core running elevated (for TUN mode). Caller re-issues SetMode+Connect after.</summary>
    public async Task<CoreApiClient> RestartElevatedAsync(CancellationToken ct = default)
    {
        await StopAsync().ConfigureAwait(false);
        return await EnsureRunningAsync(elevated: true, ct).ConfigureAwait(false);
    }

    private static async Task DrainAsync(StreamReader reader)
    {
        try
        {
            while (await reader.ReadLineAsync().ConfigureAwait(false) is not null) { }
        }
        catch { }
    }

    public async ValueTask DisposeAsync()
    {
        await StopAsync().ConfigureAwait(false);
    }

    public sealed record EndpointInfo(int Port, string Token, int Pid, DateTimeOffset? StartedAt);
}
