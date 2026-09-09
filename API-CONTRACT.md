# OpenRung WinUI3 Core ↔ UI API Contract (v1)

This file is the single source of truth for the contract between
`core/openrung-core.exe` (Go) and `app/OpenRung.WinUI` (C# WinUI3).
Both sides MUST implement exactly this. No deviation without updating this file.

## Process model

- The C# app spawns `openrung-core.exe serve --heartbeat-timeout 45s` (hidden window,
  `UseShellExecute=false`, working directory = exe directory). For TUN mode it respawns
  it elevated via `Verb = "runas"`.
- On start the core binds `127.0.0.1` on an ephemeral port (or `--port N`), generates a
  random 32-byte hex token, and atomically writes an endpoint file to
  `%LOCALAPPDATA%\OpenRung\core-endpoint.json`:
  `{"port": 19432, "token": "hex64", "pid": 1234, "startedAt": "<RFC3339>"}`
  (on Linux/macOS `os.UserConfigDir()/openrung/core-endpoint.json`).
  The file is deleted on graceful exit. Last-writer wins: only one core instance is
  intended to run; a second core refuses to start if the file describes a live,
  token-matching process.
- Every request (except none — all of them) requires header `X-OpenRung-Token: <token>`.
  Missing/wrong token → 401 `{"error":"unauthorized"}`.

## Endpoints

All success bodies are JSON. All error bodies are `{"error":"human readable","code":"optional_machine_code"}`.

### GET /api/version
```json
{"core":"1.0.0","engine":"sing-box 1.x.y ...","os":"windows","arch":"amd64","elevated":true}
```

### GET /api/state → StateSnapshot
```json
{
  "status": "disconnected|preparing|connecting|connected|disconnecting|failed",
  "relayLabel": "Germany #3" | null,
  "lastError": "..." | null,
  "mode": "proxy|tun",
  "proxy": {"host":"127.0.0.1","port":17890},
  "connection": {
    "relayId":"...", "label":"...", "countryCode":"DE",
    "transport":"direct|punch|wss", "frontId":"" ,
    "startedAt":"<RFC3339>"
  } | null,
  "recents":[{"countryCode":"DE","label":"Germany #3","latitude":52.5,"longitude":13.4}],
  "elevated": true,
  "coreVersion":"1.0.0"
}
```

### GET /api/events (Server-Sent Events)
`Content-Type: text/event-stream`. Long-lived. Events:
- `event: state\ndata: <StateSnapshot JSON>\n\n` — sent immediately on connect and on every state change.
- `event: log\ndata: {"time":"<RFC3339>","line":"..."}\n\n` — on connect, last 200 buffered log lines are replayed first, then live lines.
- comment `:ping` every 15 s as keepalive.

### POST /api/connect
Body: `{"brokerUrl":"" , "relayId":"" , "country":""}` (all optional; empty = auto-select).
Responses:
- `202 {"ok":true}` — connect dispatched; completion arrives via events.
- `409 {"error":"...","code":"already_connecting"}` etc.
- `428 {"error":"...","code":"elevation_required"}` — TUN mode on Windows without an
  elevated core; the UI must offer to restart the core elevated.

### POST /api/disconnect → 200 `{"ok":true}` (idempotent)

### POST /api/mode
Body: `{"mode":"proxy"|"tun"}`.
- `200 {"ok":true,"mode":"tun"}`
- `409 {"error":"disconnect before changing the capture mode","code":"connected"}`
- `428 {...,"code":"elevation_required"}` when mode=tun and the core is not elevated (Windows).
The mode persists across core restarts (settings.json in the openrung config dir).

### GET /api/relays?ranked=1&broker=
Returns ranked relay directory (TCP latency probed, same ranking the connect ladder uses):
```json
{"serverTime":"<RFC3339>","relays":[
  {"id":"...","label":"...","country":"Germany","countryCode":"DE","city":"Frankfurt",
   "nodeClass":"foundation|volunteer","punchCapable":true,"maxMbps":1000,"latencyMs":42|null}
]}
```
`latencyMs` null when not probed or probe failed. Errors: 502 `{"error":...}`.

### GET /api/logs?tail=200 → `{"logs":[{"time":"...","line":"..."}]}`

### POST /api/heartbeat → 204
UI sends every 5 s while running. If `--heartbeat-timeout` was given and no request
(any endpoint counts) arrives within the timeout, the core gracefully disconnects the
engine (restores system proxy), deletes the endpoint file, and exits 0.
This replaces the orphan-tunnel problem for a GUI-owned core.

### POST /api/shutdown → 200 `{"ok":true}` then the core exits gracefully (engine.Stop,
endpoint file removed). The UI sends this on exit and before relaunching elevated.

## Behavior notes

- TUN/connect operation on Linux also requires privileges; the core reports
  `elevated` truthfully on every platform so the UI can gate the mode toggle.
- Engine startup runs crash recovery: a leftover system-proxy snapshot from a previous
  crashed core is restored automatically (existing connectcore behavior).
- The core identifies to the broker as the desktop platform (default), same wire
  behavior as the Wails app.
