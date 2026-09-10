# OpenRung WinUI3 Core ↔ UI API Contract (v1)

This file is the single source of truth for the contract between
`core/openrung-core.exe` (Go) and `app/OpenRung.WinUI.Cpp` (C++/WinRT WinUI3).
Both sides MUST implement exactly this. No deviation without updating this file.

## Process model

- The app spawns `openrung-core.exe serve --heartbeat-timeout 45s` (hidden window,
  inherited environment, working directory = exe directory). For TUN mode it respawns
  it elevated via `runas`.
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
  "coreVersion":"1.0.0",
  "systemProxy":"127.0.0.1:57777"
}
```
`systemProxy` is the OS system proxy currently in effect (`"host:port"` or a PAC
URL), `""` when none. While connected in proxy mode it is the endpoint the core
itself set; otherwise it is a pre-existing third-party proxy (another proxy
client the user runs), which the UI may surface and clear via `POST /api/proxy`.

### GET /api/events (Server-Sent Events)
`Content-Type: text/event-stream`. Long-lived. Events:
- `event: state\ndata: <StateSnapshot JSON>\n\n` — sent immediately on connect and on every state change.
- `event: log\ndata: {"time":"<RFC3339>","line":"..."}\n\n` — on connect, last 500 buffered log lines are replayed first, then live lines. The core keeps a 2000-line ring.
- comment `:ping` every 15 s as keepalive.

### POST /api/connect
Body: `{"brokerUrl":"" , "relayId":"" , "country":""}` (all optional; empty = auto-select).
Responses:
- `202 {"ok":true}` — connect dispatched; completion arrives via events.
- `409 {"error":"...","code":"already_connecting"}` etc.
- `409 {"error":"...","code":"tun_conflict"}` — TUN connect refused because a TUN device
  already exists on the machine (another VPN/proxy client, or a crashed core left one
  behind); disconnect it / remove the adapter, or switch to proxy mode. Checked only
  while the engine is idle, so the core's own connected TUN is never reported.
- `428 {"error":"...","code":"elevation_required"}` — TUN mode on Windows without an
  elevated core; the UI must offer to restart the core elevated.

### POST /api/disconnect → 200 `{"ok":true}` (idempotent)

### POST /api/mode
Body: `{"mode":"proxy"|"tun"}`.
- `200 {"ok":true,"mode":"tun"}`
- `409 {"error":"disconnect before changing the capture mode","code":"connected"}`
- `428 {...,"code":"elevation_required"}` when mode=tun and the core is not elevated (Windows).
The mode persists across core restarts (settings.json in the openrung config dir).

### GET+POST /api/dns
Tunnel DNS configuration: the resolver IP literals the config's DNS block emits
and the IPv6 switch. Both take effect on the next connect.

`GET → 200 {"servers":["1.1.1.1"],"ipv6":true}` — `servers` is the configured
list, `[]` meaning the defaults (1.1.1.1 / 8.8.8.8).

`POST` body: `{"servers":["9.9.9.9"],"ipv6":false}` (both fields required;
`servers` may be empty).
- `200 {"ok":true,"servers":[...],"ipv6":true}`
- `400` when `ipv6` is missing, a server is not an IP literal, or there are
  more than 4 servers
- `409 {"error":"disconnect before changing the tunnel DNS","code":"connected"}`
  while a session is live

With `ipv6:false` the TUN inbound carries IPv4 only (no v6 address, so no v6
default route) and the DNS strategy pins to `ipv4_only`, so apps never learn
AAAA addresses. Both settings persist across core restarts (settings.json:
`dnsServers`, `dnsIPv6`).

### POST /api/connect while connected (real-time switching)
`POST /api/connect` with a `relayId` different from the live session's relay
switches servers in place: the engine serializes under its connect mutex,
tears down the current session fully (including the OS-proxy restore in proxy
mode), and dials the new relay. The state stream shows
`disconnecting → connecting → connected` with the new relay; a brief direct
traffic gap during the teardown is expected. `relayId` equal to the live
relay is also accepted and re-connects it.

### POST /api/proxy
Body: `{"clear":true}` — disables the OS system proxy outright (manual proxy and
PAC URL), for removing a pre-existing third-party proxy before taking over.
- `200 {"ok":true}`
- `400` on a bad body or any action other than `clear`
- `501 {"error":...,"code":"proxy_unsupported"}` on platforms without OS proxy control
- `409 {"error":...,"code":"proxy_clear_failed"}` when the platform write failed

Default behavior without this endpoint: proxy mode captures the existing proxy
before pointing the OS at the local inbound and restores it on disconnect (or
crash recovery at next start). TUN mode never touches the OS proxy.

### GET /api/relays?ranked=1&broker=
Returns ranked relay directory (TCP latency probed, same ranking the connect ladder uses):
```json
{"serverTime":"<RFC3339>","relays":[
  {"id":"...","label":"...","country":"Germany","countryCode":"DE","city":"Frankfurt",
   "nodeClass":"foundation|volunteer","punchCapable":true,"maxMbps":1000,"latencyMs":42|null}
]}
```
`latencyMs` null when not probed or probe failed. Errors: 502 `{"error":...}`.

### POST /api/tcping
v2rayN-style TCP handshake test. Body (all optional): `{"relayIds":["..."],"samples":3}`
(empty `relayIds` = every usable relay; `samples` clamped to 1..5, default 3). Probes
each relay's public TCP endpoint in parallel (≤8 concurrent dials, engine dialer,
per-dial timeout = the ranker's 1.5 s), averages successful samples, counts losses:
```json
{"results":[{"relayId":"...","host":"...","port":443,"avgMs":42,"loss":0,"samples":3}]}
```
`avgMs` null when every sample failed. Errors: 502 `{"error":...}`. Measurement only —
no engine state change.

### POST /api/real-delay
v2rayN-style real connection latency — an HTTP `generate_204` that must traverse the
tunnel (2 samples, faster one counts; "real ping"):
- **Without body / empty `relayId`**: probes the live session through its mixed inbound
  (proxy mode) or the captured default network (TUN mode).
  → `{"relayId":"<active>","ms":123}`; 409 `{"error":...}` when not connected.
- **With `{"relayId":"..."}`**: runs a throwaway tunnel through that relay using the
  connect ladder's attempt+probe machinery, never promoted (no engine state change, no
  OS proxy, no telemetry session). Requires the core to be **disconnected** and **proxy
  mode** (TUN refuses 502). → `{"relayId":"...","ms":123}`; 502 `{"error":...}` when the
  rung fails (unreachable relay, WSS fallback exhausted, probe timeout).

### GET /api/logs?tail=500 → `{"logs":[{"time":"...","line":"..."}]}`
Default `tail` is 500; the ring holds 2000 lines.

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
- The core's own outbound traffic (broker discovery, WSS tickets/relay dials,
  telemetry, geo, punch) never uses the OS system proxy: it is the proxy itself,
  and routing its traffic through a third-party proxy (or its own loopback
  inbound) would loop. The UI's API client likewise bypasses the system proxy
  (WinHTTP `WINHTTP_ACCESS_TYPE_NO_PROXY`, plus `NO_PROXY=*` in the core's
  inherited environment) so UI→core calls are direct.
