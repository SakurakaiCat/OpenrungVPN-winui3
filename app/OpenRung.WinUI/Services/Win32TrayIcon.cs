using System.Runtime.InteropServices;

namespace OpenRung.WinUI.Services;

/// <summary>
/// Minimal Shell_NotifyIcon service for the unpackaged app. No XAML islands
/// or taskbar packages: a hidden message window receives tray clicks, and the
/// WinUI window is shown/hidden from its handlers. The tray icon only exists
/// while the app runs, which is what the README's "close keeps the engine
/// alive" flow wants: closing the window hides it and leaves the icon.
/// </summary>
public sealed partial class Win32TrayIcon : IDisposable
{
    private const uint WM_APP_TRAY = 0x8000 + 0x200;
    private const uint WM_LBUTTONUP = 0x0202;
    private const uint WM_RBUTTONUP = 0x0205;

    private const uint NIM_ADD = 0x00;
    private const uint NIM_MODIFY = 0x01;
    private const uint NIM_DELETE = 0x02;
    private const uint NIF_MESSAGE = 0x01;
    private const uint NIF_ICON = 0x02;
    private const uint NIF_TIP = 0x04;
    private const uint NIF_SHOWTIP = 0x80;

    private static readonly Guid IconGuid = Guid.Parse("2f8d0e2a-1f6d-4b2c-9a0e-3c1d4a5b6c7d");

    private readonly HwndSourceWindow _msgWindow;
    private bool _added;

    public event EventHandler? LeftClick;

    public event EventHandler? RightClick;

    public Win32TrayIcon()
    {
        _msgWindow = new HwndSourceWindow(WndProcThunk);
    }

    public void Add(string tooltip)
    {
        var icon = LoadAppIcon();
        var data = MakeData(_msgWindow.Hwnd, tooltip, icon);
        _added = Shell_NotifyIconW(NIM_ADD, ref data) || Shell_NotifyIconW(NIM_MODIFY, ref data);
    }

    public void SetTooltip(string tooltip)
    {
        if (!_added)
            return;
        var data = MakeData(_msgWindow.Hwnd, tooltip, IntPtr.Zero);
        data.uFlags = NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, ref data);
    }

    public void Remove()
    {
        if (!_added)
            return;
        var data = MakeData(_msgWindow.Hwnd, "", IntPtr.Zero);
        Shell_NotifyIconW(NIM_DELETE, ref data);
        _added = false;
    }

    private NOTIFYICONDATAW MakeData(IntPtr hwnd, string tip, IntPtr icon)
    {
        return new NOTIFYICONDATAW
        {
            cbSize = (uint)Marshal.SizeOf<NOTIFYICONDATAW>(),
            hWnd = hwnd,
            uID = 1,
            uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP,
            uCallbackMessage = WM_APP_TRAY,
            hIcon = icon,
            szTip = tip,
            uTimeoutOrVersion = 4,
            guidItem = IconGuid,
        };
    }

    private IntPtr WndProcThunk(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam)
    {
        if (msg == WM_APP_TRAY)
        {
            var button = unchecked((uint)lParam.ToInt32()) & 0xFFFF;
            if (button == WM_LBUTTONUP)
                LeftClick?.Invoke(this, EventArgs.Empty);
            else if (button == WM_RBUTTONUP)
                RightClick?.Invoke(this, EventArgs.Empty);
            return IntPtr.Zero;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    private static IntPtr LoadAppIcon()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "Assets", "app.ico");
        if (File.Exists(path))
        {
            var h = LoadImageW(IntPtr.Zero, path, 1 /* IMAGE_ICON */, 0, 0,
                0x00000010 /* LR_LOADFROMFILE */ | 0x00000040 /* LR_DEFAULTSIZE */);
            if (h != IntPtr.Zero)
                return h;
        }
        return LoadIconW(IntPtr.Zero, 32512 /* IDI_APPLICATION */);
    }

    public void Dispose()
    {
        Remove();
        _msgWindow.Dispose();
    }

    // [DllImport] (not [LibraryImport]) because NOTIFYICONDATAW holds a UTF-16
    // fixed-size char buffer and a string? field with LPWStr marshalling, both
    // of which the source-generated P/Invoke marshaller rejects (SYSLIB1051).
    // Runtime marshalling handles both fine.
    [DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool Shell_NotifyIconW(uint dwMessage, ref NOTIFYICONDATAW lpData);

    [LibraryImport("user32.dll", SetLastError = true)]
    private static partial IntPtr DefWindowProcW(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [LibraryImport("user32.dll", SetLastError = true, StringMarshalling = StringMarshalling.Utf16)]
    private static partial IntPtr LoadImageW(IntPtr hInst, string name, uint type, int cx, int cy, uint fuLoad);

    [LibraryImport("user32.dll", SetLastError = true)]
    private static partial IntPtr LoadIconW(IntPtr hInstance, nint lpIconName);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct NOTIFYICONDATAW
    {
        public uint cbSize;
        public IntPtr hWnd;
        public uint uID;
        public uint uFlags;
        public uint uCallbackMessage;
        public IntPtr hIcon;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string szTip;
        public uint dwState;
        public uint dwStateMask;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string szInfo;
        public uint uTimeoutOrVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string szInfoTitle;
        public uint dwInfoFlags;
        public Guid guidItem;
        public IntPtr hBalloonIcon;
    }

    /// <summary>Hidden message-only window the tray callbacks arrive on.</summary>
    private sealed class HwndSourceWindow : IDisposable
    {
        private readonly WndProc _proc;

        public HwndSourceWindow(Func<IntPtr, uint, IntPtr, IntPtr, IntPtr> handler)
        {
            _proc = (h, m, w, l) => handler(h, m, w, l);
            var wc = new WNDCLASSW
            {
                lpfnWndProc = Marshal.GetFunctionPointerForDelegate(_proc),
                lpszClassName = "OpenRungTrayMessageWindow",
            };
            RegisterClassW(ref wc);
            Hwnd = CreateWindowExW(0, wc.lpszClassName, string.Empty, 0, 0, 0, 0, 0,
                HWND_MESSAGE, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
            // Tray messages must arrive on the WinUI window, so the caller
            // redirects us: record our own hwnd as the event sink by returning
            // events through the callback. We also keep the message window as
            // a fallback target.
        }

        public IntPtr Hwnd { get; }

        public void Dispose()
        {
            if (Hwnd != IntPtr.Zero)
                DestroyWindow(Hwnd);
        }

        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern ushort RegisterClassW(ref WNDCLASSW lpWndClass);

        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr CreateWindowExW(uint dwExStyle, string lpClassName,
            string lpWindowName, uint dwStyle, int x, int y, int nWidth, int nHeight,
            IntPtr hWndParent, IntPtr hMenu, IntPtr hInstance, IntPtr lpParam);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DestroyWindow(IntPtr hWnd);

        private delegate IntPtr WndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        private static readonly IntPtr HWND_MESSAGE = new(-3);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WNDCLASSW
        {
            public uint style;
            public IntPtr lpfnWndProc;
            public int cbClsExtra;
            public int cbWndExtra;
            public IntPtr hInstance;
            public IntPtr hIcon;
            public IntPtr hCursor;
            public IntPtr hbrBackground;
            [MarshalAs(UnmanagedType.LPWStr)]
            public string? lpszMenuName;
            [MarshalAs(UnmanagedType.LPWStr)]
            public string lpszClassName;
        }
    }
}
