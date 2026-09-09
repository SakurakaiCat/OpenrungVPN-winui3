package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"time"

	"github.com/openrung/openrung/brokerapi"
	"github.com/openrung/openrung/connectcore"
	"github.com/openrung/openrung/connectcore/client"

	"openrung/internal/singboxruntime"
)

// handlers.go — the loopback HTTP API from API-CONTRACT.md. Handlers are
// registered on a plain ServeMux; auth and heartbeat stamping wrap the mux
// in server.go. The engine is single-threaded through its own locks, so
// handlers can interleave freely.

func (c *core) registerRoutes(mux *http.ServeMux) {
	mux.HandleFunc("/api/version", c.handleVersion)
	mux.HandleFunc("/api/state", c.handleState)
	mux.HandleFunc("/api/events", c.handleEvents)
	mux.HandleFunc("/api/connect", c.handleConnect)
	mux.HandleFunc("/api/disconnect", c.handleDisconnect)
	mux.HandleFunc("/api/mode", c.handleMode)
	mux.HandleFunc("/api/proxy", c.handleProxy)
	mux.HandleFunc("/api/relays", c.handleRelays)
	mux.HandleFunc("/api/logs", c.handleLogs)
	mux.HandleFunc("/api/heartbeat", c.handleHeartbeat)
	mux.HandleFunc("/api/shutdown", c.handleShutdown)
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	data, err := json.Marshal(v)
	if err == nil {
		_, _ = w.Write(data)
	}
}

// writeError is the contract's {"error":..., "code":...} envelope.
func writeError(w http.ResponseWriter, status int, code, msg string) {
	body := map[string]string{"error": msg}
	if code != "" {
		body["code"] = code
	}
	writeJSON(w, status, body)
}

func methodOnly(w http.ResponseWriter, r *http.Request, method string) bool {
	if r.Method != method {
		w.Header().Set("Allow", method)
		writeError(w, http.StatusMethodNotAllowed, "", "method not allowed")
		return false
	}
	return true
}

// apiLog records an API-surface event in the hub's log ring, so the logs page
// shows the full chain for every request: the user's action (app side), the
// API handling incl. preflight results (these lines), then the engine ladder.
// Heartbeats are deliberately excluded: at one every few seconds they would
// drown the interesting lines.
func (c *core) apiLog(format string, args ...any) {
	c.hub.appendLog(logEntry{Time: time.Now().UTC(), Line: "api: " + fmt.Sprintf(format, args...)})
}

// ---- GET /api/version -----------------------------------------------------

func (c *core) handleVersion(w http.ResponseWriter, r *http.Request) {
	if !methodOnly(w, r, http.MethodGet) {
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"core":     client.AppVersion(),
		"engine":   singboxruntime.VersionLine(),
		"os":       runtime.GOOS,
		"arch":     runtime.GOARCH,
		"elevated": elevated(),
	})
}

// ---- State snapshot -------------------------------------------------------

type stateSnapshot struct {
	Status        string              `json:"status"`
	RelayLabel    *string             `json:"relayLabel"`
	LastError     *string             `json:"lastError"`
	Mode          string              `json:"mode"`
	Proxy         *proxyEndpoint      `json:"proxy"`
	Connection    *connectionSnapshot `json:"connection"`
	Recents       []recentNode        `json:"recents"`
	Elevated      bool                `json:"elevated"`
	CoreVersion   string              `json:"coreVersion"`
	SystemProxy   string              `json:"systemProxy"`
}

type proxyEndpoint struct {
	Host string `json:"host"`
	Port int    `json:"port"`
}

type connectionSnapshot struct {
	RelayID     string    `json:"relayId"`
	Label       string    `json:"label"`
	CountryCode string    `json:"countryCode"`
	Transport   string    `json:"transport"`
	FrontID     string    `json:"frontId"`
	StartedAt   time.Time `json:"startedAt"`
}

type recentNode struct {
	CountryCode string  `json:"countryCode"`
	Label       string  `json:"label"`
	Latitude    float64 `json:"latitude"`
	Longitude   float64 `json:"longitude"`
}

// snapshot builds the contract StateSnapshot from the live engine. It also
// maintains connectedAt: SSE coalescing means a burst of states collapses to
// the latest, so the timestamp follows the newest snapshot's status rather
// than sink callbacks.
func (c *core) snapshot() stateSnapshot {
	st := c.engine.State()

	c.mu.Lock()
	if st.Status == connectcore.StatusConnected {
		if c.connectedAt.IsZero() {
			c.connectedAt = time.Now().UTC()
		}
	} else {
		c.connectedAt = time.Time{}
	}
	connectedAt := c.connectedAt
	c.mu.Unlock()

	recents := make([]recentNode, 0, len(st.Recents))
	for _, r := range st.Recents {
		recents = append(recents, recentNode{
			CountryCode: r.CountryCode,
			Label:       r.Label,
			Latitude:    r.Latitude,
			Longitude:   r.Longitude,
		})
	}

	// The proxy row describes the loopback endpoint the UI can point apps at;
	// resolve it lazily so a port-allocation hiccup can't kill the snapshot.
	var proxy *proxyEndpoint
	if port, err := c.engine.LocalProxyPort(); err == nil && port > 0 {
		proxy = &proxyEndpoint{Host: "127.0.0.1", Port: port}
	}

	var conn *connectionSnapshot
	if info, ok := c.engine.ActiveConnectionInfo(); ok {
		conn = &connectionSnapshot{
			RelayID:     info.Relay.ID,
			Label:       info.Relay.Label,
			CountryCode: info.Relay.CountryCode,
			Transport:   info.Transport,
			FrontID:     info.FrontID,
			StartedAt:   connectedAt,
		}
	}

	return stateSnapshot{
		Status:      string(st.Status),
		RelayLabel:  st.RelayLabel,
		LastError:   st.LastError,
		Mode:        c.engine.Mode().String(),
		Proxy:       proxy,
		Connection:  conn,
		Recents:     recents,
		Elevated:    elevated(),
		CoreVersion: client.AppVersion(),
		SystemProxy: c.systemProxy(),
	}
}

// systemProxy reports the OS system proxy currently in effect, or "" when
// none. While connected in proxy mode this is the endpoint the core itself
// set; otherwise it is a pre-existing third-party proxy (e.g. another proxy
// client the user runs), which the UI can surface and clear.
func (c *core) systemProxy() string {
	if c.host == nil || c.host.proxy == nil || !c.host.proxy.Supported() {
		return ""
	}
	return c.host.proxy.Describe()
}

func (c *core) handleState(w http.ResponseWriter, r *http.Request) {
	if !methodOnly(w, r, http.MethodGet) {
		return
	}
	writeJSON(w, http.StatusOK, c.snapshot())
}

// ---- GET /api/events (SSE) ------------------------------------------------

func (c *core) handleEvents(w http.ResponseWriter, r *http.Request) {
	if !methodOnly(w, r, http.MethodGet) {
		return
	}
	flusher, ok := w.(http.Flusher)
	if !ok {
		writeError(w, http.StatusInternalServerError, "", "streaming unsupported")
		return
	}

	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.WriteHeader(http.StatusOK)

	ch, replay := c.hub.subscribe()
	defer c.hub.unsubscribe(ch)

	// Contract order on connect: current state, then the last 200 log lines,
	// then live traffic.
	if data, err := json.Marshal(c.snapshot()); err == nil {
		_, _ = w.Write(sseFrame(sseStateEvent, data))
	}
	for _, entry := range replay {
		if data, err := json.Marshal(entry); err == nil {
			_, _ = w.Write(sseFrame(sseLogEvent, data))
		}
	}
	flusher.Flush()

	keepalive := time.NewTicker(15 * time.Second)
	defer keepalive.Stop()

	for {
		select {
		case <-r.Context().Done():
			return
		case frame := <-ch:
			_, _ = w.Write(frame)
			flusher.Flush()
		case <-keepalive.C:
			_, _ = fmt.Fprintf(w, "%s\n\n", ssePing)
			flusher.Flush()
		}
	}
}

// ---- POST /api/connect, /api/disconnect -----------------------------------

func (c *core) handleConnect(w http.ResponseWriter, r *http.Request) {
	if !methodOnly(w, r, http.MethodPost) {
		return
	}
	var body struct {
		BrokerURL string `json:"brokerUrl"`
		RelayID   string `json:"relayId"`
		Country   string `json:"country"`
	}
	// A missing or empty body is the default "auto-select" connect.
	if r.Body != nil {
		data, _ := io.ReadAll(io.LimitReader(r.Body, 1<<16))
		if len(bytes.TrimSpace(data)) > 0 {
			if err := json.Unmarshal(data, &body); err != nil {
				writeError(w, http.StatusBadRequest, "", "invalid connect body: "+err.Error())
				return
			}
		}
	}
	brokerURL := body.BrokerURL
	if brokerURL == "" {
		brokerURL = c.broker
	}
	target := body.RelayID
	if target == "" {
		if body.Country != "" {
			target = "country:" + body.Country
		} else {
			target = "auto"
		}
	}
	c.apiLog("POST /api/connect: target=%s mode=%s broker=%s", target, c.engine.Mode(), brokerURL)

	// TUN without privileges refuses before any dialing happens, so the UI
	// gets the contract's 428 and offers the elevated restart.
	if c.engine.Mode() == connectcore.ModeTUN {
		if err := tunModeAvailable(); err != nil {
			c.apiLog("POST /api/connect refused 428 elevation_required: %v", err)
			writeError(w, 428, "elevation_required", err.Error())
			return
		}
		// An existing TUN device means auto_route would fight its routes and
		// blackhole the machine — refuse before the ladder dials anything.
		// Only when idle though: a connected/connecting engine owns its own
		// tun device, which must not be mistaken for a foreign one.
		switch st := c.engine.State().Status; st {
		case connectcore.StatusDisconnected, connectcore.StatusFailed:
			if err := tunDeviceConflict(); err != nil {
				c.apiLog("POST /api/connect refused 409 tun_conflict: %v", err)
				writeError(w, http.StatusConflict, "tun_conflict", err.Error())
				return
			}
		}
	}

	if err := c.engine.Connect(brokerURL, body.Country, body.RelayID); err != nil {
		status := http.StatusConflict
		code := ""
		if errors.Is(err, ErrElevationRequired) {
			status = 428
			code = "elevation_required"
		}
		c.apiLog("POST /api/connect failed %d %s: %v", status, code, err)
		writeError(w, status, code, err.Error())
		return
	}
	c.apiLog("POST /api/connect dispatched; outcome arrives via events")
	writeJSON(w, http.StatusAccepted, map[string]any{"ok": true})
}

func (c *core) handleDisconnect(w http.ResponseWriter, r *http.Request) {
	if !methodOnly(w, r, http.MethodPost) {
		return
	}
	// Disconnect is idempotent — the engine tolerates disconnect-when-idle.
	c.apiLog("POST /api/disconnect (status=%s)", c.engine.State().Status)
	if err := c.engine.Disconnect(); err != nil {
		c.apiLog("POST /api/disconnect failed: %v", err)
		writeError(w, http.StatusInternalServerError, "", err.Error())
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
}

// ---- POST /api/mode --------------------------------------------------------

// modePath keeps the capture mode across core restarts. It lives next to the
// client state the engine persists (os.UserConfigDir()/openrung) as
// settings.json, per the contract. Written best-effort.
func modePath() (string, error) {
	base, err := os.UserConfigDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(base, "openrung", "settings.json"), nil
}

func (c *core) restoreMode() {
	path, err := modePath()
	if err != nil {
		return
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return
	}
	var settings struct {
		Mode string `json:"mode"`
	}
	if json.Unmarshal(data, &settings) != nil {
		return
	}
	if settings.Mode == "tun" {
		// Apply even unelevated: the state advertises it and connect
		// refuses with 428, letting the UI offer the elevated restart
		// without a separate mode call first.
		_ = c.engine.SetMode(connectcore.ModeTUN)
	}
}

func persistMode(mode string) {
	path, err := modePath()
	if err != nil {
		return
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return
	}
	data, err := json.Marshal(map[string]string{"mode": mode})
	if err != nil {
		return
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o600); err != nil {
		return
	}
	_ = os.Rename(tmp, path)
}

func (c *core) handleMode(w http.ResponseWriter, r *http.Request) {
	if !methodOnly(w, r, http.MethodPost) {
		return
	}
	var body struct {
		Mode string `json:"mode"`
	}
	if err := json.NewDecoder(io.LimitReader(r.Body, 1<<16)).Decode(&body); err != nil {
		writeError(w, http.StatusBadRequest, "", "invalid mode body: "+err.Error())
		return
	}
	var mode connectcore.Mode
	switch body.Mode {
	case "proxy":
		mode = connectcore.ModeProxy
	case "tun":
		mode = connectcore.ModeTUN
	default:
		writeError(w, http.StatusBadRequest, "", `mode must be "proxy" or "tun"`)
		return
	}

	// TUN selection without privileges fails fast so the UI can offer the
	// elevated restart instead of discovering it on connect.
	if mode == connectcore.ModeTUN {
		if err := tunModeAvailable(); err != nil {
			c.apiLog("POST /api/mode refused 428 elevation_required: %v", err)
			writeError(w, 428, "elevation_required", err.Error())
			return
		}
	}

	if err := c.engine.SetMode(mode); err != nil {
		c.apiLog("POST /api/mode refused 409: %v", err)
		writeError(w, http.StatusConflict, "connected", err.Error())
		return
	}
	persistMode(mode.String())
	c.apiLog("POST /api/mode: mode=%s (persisted)", mode)
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "mode": mode.String()})
}

// ---- POST /api/proxy --------------------------------------------------------

// proxyRequest is the POST /api/proxy body. clear=true disables the OS system
// proxy outright (a pre-existing third-party one) instead of the default
// take-over-then-restore behavior proxy mode applies.
type proxyRequest struct {
	Clear bool `json:"clear"`
}

func (c *core) handleProxy(w http.ResponseWriter, r *http.Request) {
	if !methodOnly(w, r, http.MethodPost) {
		return
	}
	var req proxyRequest
	if err := json.NewDecoder(io.LimitReader(r.Body, 4096)).Decode(&req); err != nil {
		writeError(w, http.StatusBadRequest, "", "invalid body")
		return
	}
	if !req.Clear {
		writeError(w, http.StatusBadRequest, "", "unsupported action")
		return
	}
	ctrl := c.host.proxy
	if ctrl == nil || !ctrl.Supported() {
		writeError(w, http.StatusNotImplemented, "proxy_unsupported", "system proxy control is not supported on this platform")
		return
	}
	if removed := ctrl.Describe(); removed != "" {
		c.hub.appendLog(logEntry{Time: time.Now().UTC(), Line: "cleared system proxy " + removed + " at user request"})
	}
	if err := ctrl.Clear(); err != nil {
		writeError(w, http.StatusConflict, "proxy_clear_failed", err.Error())
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
}

// ---- GET /api/relays --------------------------------------------------------

func (c *core) handleRelays(w http.ResponseWriter, r *http.Request) {
	if !methodOnly(w, r, http.MethodGet) {
		return
	}
	broker := r.URL.Query().Get("broker")
	if broker == "" {
		broker = c.broker
	}

	// Relay probing is network-bound; bound it well above the engine's own
	// timeouts so the client gets the ranked list or a real error, never an
	// early cancel. The engine's directory cache rate-limits the fetch.
	ctx, cancel := context.WithTimeout(r.Context(), 60*time.Second)
	defer cancel()
	ranked, err := c.engine.RankedDirectory(ctx, broker)
	if err != nil {
		writeError(w, http.StatusBadGateway, "", "relay directory: "+err.Error())
		return
	}

	type relayJSON struct {
		ID           string `json:"id"`
		Label        string `json:"label"`
		Country      string `json:"country"`
		CountryCode  string `json:"countryCode"`
		City         string `json:"city"`
		NodeClass    string `json:"nodeClass"`
		PunchCapable bool   `json:"punchCapable"`
		MaxMbps      int    `json:"maxMbps"`
		LatencyMs    *int64 `json:"latencyMs"`
	}
	relays := make([]relayJSON, 0, len(ranked))
	for _, d := range ranked {
		relays = append(relays, relayJSON{
			ID:           d.Relay.ID,
			Label:        d.Relay.Label,
			Country:      d.Relay.Country,
			CountryCode:  d.Relay.CountryCode,
			City:         d.Relay.City,
			NodeClass:    brokerapi.EffectiveNodeClass(d.Relay.NodeClass),
			PunchCapable: d.Relay.PunchCapable,
			MaxMbps:      d.Relay.MaxMbps,
			LatencyMs:    d.ProbeMS,
		})
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"serverTime": time.Now().UTC().Format(time.RFC3339),
		"relays":     relays,
	})
}

// ---- GET /api/logs ----------------------------------------------------------

func (c *core) handleLogs(w http.ResponseWriter, r *http.Request) {
	if !methodOnly(w, r, http.MethodGet) {
		return
	}
	tail := 500
	if s := r.URL.Query().Get("tail"); s != "" {
		n, err := strconv.Atoi(s)
		if err != nil || n < 0 {
			writeError(w, http.StatusBadRequest, "", "invalid tail")
			return
		}
		tail = n
	}
	logs := c.hub.tailLogs(tail)
	writeJSON(w, http.StatusOK, map[string]any{"logs": logs})
}

// ---- POST /api/heartbeat, /api/shutdown -------------------------------------

// handleHeartbeat is a 204 no-op: the auth wrapper already stamped the
// activity the watchdog reads.
func (c *core) handleHeartbeat(w http.ResponseWriter, r *http.Request) {
	if !methodOnly(w, r, http.MethodPost) {
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func (c *core) handleShutdown(w http.ResponseWriter, r *http.Request) {
	if !methodOnly(w, r, http.MethodPost) {
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
	c.apiLog("POST /api/shutdown accepted; core exiting")
	// Respond before the process goes away: the request must complete for the
	// UI to treat its elevated restart as clean.
	go func() {
		time.Sleep(50 * time.Millisecond)
		select {
		case c.shutdownReq <- struct{}{}:
		default:
		}
	}()
}
