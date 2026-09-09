# OpenRung WinUI3 Client

A modern, native Windows client for [OpenRung](../openrung) built with **WinUI 3**
(Windows App SDK), replacing the old Wails desktop app, which was barely usable on
Windows. One click connects you to the fastest relay; switches between local proxy
mode and full-device TUN mode; lives in the system tray.

## Architecture

```
app/    C# / WinUI 3 GUI (net8.0-windows, unpackaged, self-contained)
core/   Go sidecar openrung-core.exe — wraps connectcore + bundled sing-box,
        exposing a small authenticated loopback JSON/SSE API (see API-CONTRACT.md)
```

The GUI never touches the system proxy or the routing table itself; everything
goes through the core engine, so crash recovery (proxy snapshot restore),
direct-first WSS fallback, punching and failover behave exactly like the
official clients.

- **Proxy mode** (default): loopback mixed HTTP/SOCKS on a stable per-install
  port, system proxy pointed at it. No admin rights needed.
- **TUN mode**: full-device capture through the bundled sing-box engine
  (wintun, no driver install). Needs Administrator — the app asks once via UAC
  and restarts only the core process elevated.

## Build

Prerequisites: Go ≥ 1.25 (from the openrung tree) and .NET SDK 8 on Windows.

```powershell
# Full build (core + app), outputs a portable dist\ folder:
powershell -ExecutionPolicy Bypass -File scripts\build.ps1

# Or on WSL with the dotnet SDK installed on the Windows host:
bash scripts/build.sh
```

The resulting `dist/` directory (`OpenRung.WinUI.exe` + `core/openrung-core.exe`)
is fully portable: copy it anywhere and run `OpenRung.WinUI.exe`.

## Screenshots

![主页](docs/screenshots/home.png)
![服务器](docs/screenshots/servers.png)
![设置](docs/screenshots/settings.png)

## Run

Double-click `OpenRung.WinUI.exe`. The big button connects; the mode switch
chooses 代理 / TUN. The icon sits in the system tray.

## License

GPL-3.0. This client is a fork of the upstream
[openrung/openrung](https://github.com/openrung/openrung) project (also GPL-3.0),
maintained by [@SakurakaiCat](https://github.com/SakurakaiCat); the
`libs/` directory carries those first-party modules under their original
license.
