using System.Runtime.InteropServices;

namespace OpenRung.WinUI.Services;

/// <summary>
/// Popup menu for the tray icon (右鍵選單). Pure Win32; the command loop maps
/// menu picks back to managed delegates.
/// </summary>
public static class TrayMenu
{
    private const uint TPM_RETURNCMD = 0x0100;
    private const uint TPM_RIGHTBUTTON = 0x0002;
    private const uint MF_STRING = 0x0000;

    /// <summary>Shows the two-item tray menu at the cursor and returns "open" | "quit" | null.</summary>
    public static string? Show(IntPtr hwndOwner, string openLabel, string quitLabel)
    {
        var menu = CreatePopupMenu();
        if (menu == IntPtr.Zero)
            return null;
        try
        {
            _ = AppendMenuW(menu, MF_STRING, 1, openLabel);
            _ = AppendMenuW(menu, MF_STRING, 2, quitLabel);
            GetCursorPos(out var pt);
            // Required so the menu dismisses on outside click.
            SetForegroundWindow(hwndOwner);
            var cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.X, pt.Y, 0, hwndOwner, IntPtr.Zero);
            return cmd switch
            {
                1 => "open",
                2 => "quit",
                _ => null,
            };
        }
        finally
        {
            DestroyMenu(menu);
        }
    }

    [DllImport("user32.dll")]
    private static extern IntPtr CreatePopupMenu();

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool AppendMenuW(IntPtr hMenu, uint uFlags, nuint uIDNewItem, string lpNewItem);

    [DllImport("user32.dll")]
    private static extern int TrackPopupMenu(IntPtr hMenu, uint uFlags, int x, int y,
        int nReserved, IntPtr hWnd, IntPtr prcRect);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DestroyMenu(IntPtr hMenu);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetCursorPos(out POINT lpPoint);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT
    {
        public int X;
        public int Y;
    }
}
