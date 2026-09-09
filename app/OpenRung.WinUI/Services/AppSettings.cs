using System.Text.Json;

namespace OpenRung.WinUI.Services;

/// <summary>
/// UI-side preferences, persisted to %LOCALAPPDATA%\OpenRung\app-settings.json
/// (next to the core's own client state). The core stays unaware of these.
/// </summary>
public sealed class AppSettings
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);

    /// <summary>
    /// When true, the app clears any pre-existing OS system proxy (e.g. left by
    /// another proxy client) on startup and whenever the user enables the
    /// option, instead of letting proxy mode take it over and restore it later.
    /// </summary>
    public bool AutoClearProxy { get; set; }

    public static string SettingsPath
    {
        get
        {
            if (OperatingSystem.IsWindows())
                return Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                    "OpenRung", "app-settings.json");
            var configHome = Environment.GetEnvironmentVariable("XDG_CONFIG_HOME");
            if (string.IsNullOrEmpty(configHome))
                configHome = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".config");
            return Path.Combine(configHome, "openrung", "app-settings.json");
        }
    }

    public static AppSettings Load()
    {
        try
        {
            var path = SettingsPath;
            if (File.Exists(path))
            {
                var s = JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(path), Json);
                if (s is not null)
                    return s;
            }
        }
        catch { /* corrupt file: fall back to defaults */ }
        return new AppSettings();
    }

    public void Save()
    {
        try
        {
            var path = SettingsPath;
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, JsonSerializer.Serialize(this, Json));
        }
        catch { /* best effort: preference persistence must never break the UI */ }
    }
}
