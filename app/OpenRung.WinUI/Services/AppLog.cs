using Microsoft.UI.Dispatching;

namespace OpenRung.WinUI.Services;

/// <summary>
/// Surfaces app-side events (core lifecycle, event stream, API failures, proxy
/// actions) on the logs page alongside the core's own lines, so the page shows
/// the complete picture of what the app and the core did — not just engine
/// output. Lines are prefixed "[app]"; the core's lines arrive via SSE.
/// </summary>
public static class AppLog
{
    private static DispatcherQueue? _ui;

    /// <summary>Bind the UI thread once the window exists; calls before then are dropped.</summary>
    public static void Attach(DispatcherQueue ui) => _ui = ui;

    public static void Write(string line)
    {
        var queue = _ui;
        if (queue is null)
            return;
        var text = $"[{DateTime.Now:HH:mm:ss}] [app] {line}";
        queue.TryEnqueue(() => App.Logs.Append(text));
    }
}
