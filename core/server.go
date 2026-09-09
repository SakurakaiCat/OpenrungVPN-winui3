package main

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"runtime"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/openrung/openrung/connectcore"
)

// exitError lets serve report a specific process exit code to run.
type exitError struct {
	code int
	err  error
}

func (e *exitError) Error() string { return e.err.Error() }
func (e *exitError) Unwrap() error { return e.err }

// core is the sidecar's runtime: the connectcore engine behind the loopback
// API, the event hub the SSE stream reads, and the small bits of process
// state (auth token, connect timestamp, shutdown request) the handlers share.
type core struct {
	engine *connectcore.Engine
	host   *engineHost
	hub    *eventHub
	token  string
	broker string

	// shutdownReq is written by POST /api/shutdown and the heartbeat
	// watchdog; serve's select turns it into a graceful exit.
	shutdownReq chan struct{}

	mu          sync.Mutex
	connectedAt time.Time // when status last transitioned to connected

	lastActivityNanos atomic.Int64 // updated by every authenticated request
}

// endpointFile is the JSON discovery document the WinUI app reads to find the
// core, exactly as API-CONTRACT.md pins it.
type endpointFile struct {
	Port      int       `json:"port"`
	Token     string    `json:"token"`
	PID       int       `json:"pid"`
	StartedAt time.Time `json:"startedAt"`
}

func serve(cfg serveConfig) error {
	token := cfg.token
	if token == "" {
		var raw [32]byte
		if _, err := rand.Read(raw[:]); err != nil {
			return fmt.Errorf("generate API token: %w", err)
		}
		token = hex.EncodeToString(raw[:])
	}

	ln, err := net.Listen("tcp", fmt.Sprintf("127.0.0.1:%d", cfg.port))
	if err != nil {
		return &exitError{code: 2, err: fmt.Errorf("bind loopback API: %w", err)}
	}
	port := ln.Addr().(*net.TCPAddr).Port

	// Last-writer-wins with a liveness gate: a live, token-matching core means
	// a second instance, which refuses rather than fighting over the engine.
	if err := checkExistingEndpoint(port); err != nil {
		ln.Close()
		return err
	}

	c := &core{
		token:       token,
		broker:      cfg.brokerURL,
		hub:         newEventHub(),
		shutdownReq: make(chan struct{}, 2),
	}
	c.lastActivityNanos.Store(time.Now().UnixNano())

	host, err := newEngineHost(coreSink{core: c})
	if err != nil {
		ln.Close()
		return &exitError{code: 1, err: err}
	}
	c.host = host
	c.engine = host.engine
	c.restoreMode()
	c.engine.Start()

	ep := endpointFile{Port: port, Token: token, PID: os.Getpid(), StartedAt: time.Now().UTC()}
	if err := writeEndpointFile(ep); err != nil {
		ln.Close()
		return &exitError{code: 2, err: fmt.Errorf("write endpoint file: %w", err)}
	}

	mux := http.NewServeMux()
	c.registerRoutes(mux)
	// Every authenticated request counts as activity for the heartbeat
	// watchdog; a stranger poking the loopback port does not keep us alive.
	srv := &http.Server{Handler: c.authAndActivity(mux)}

	serveErr := make(chan error, 1)
	go func() { serveErr <- srv.Serve(ln) }()

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	go c.broadcastLoop(ctx)
	if cfg.heartbeatTimeout > 0 {
		go c.heartbeatWatchdog(ctx, cfg.heartbeatTimeout)
	}

	var runErr error
	select {
	case <-ctx.Done():
	case <-c.shutdownReq:
	case err := <-serveErr:
		runErr = err
	}

	stop()

	// Graceful order: stop the HTTP server (drops SSE clients), stop the
	// engine (restores the system proxy), remove the endpoint file.
	shutdownCtx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	_ = srv.Shutdown(shutdownCtx)
	cancel()
	c.engine.Stop()
	_ = removeEndpointFile()

	if runErr != nil {
		return &exitError{code: 1, err: fmt.Errorf("serve: %w", runErr)}
	}
	return nil
}

// heartbeatWatchdog exits the process gracefully when the owning UI has not
// touched any endpoint within the negotiated window.
func (c *core) heartbeatWatchdog(ctx context.Context, timeout time.Duration) {
	t := time.NewTicker(time.Second)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case now := <-t.C:
			last := time.Unix(0, c.lastActivityNanos.Load())
			if now.Sub(last) > timeout {
				select {
				case c.shutdownReq <- struct{}{}:
				default:
				}
				return
			}
		}
	}
}

// broadcastLoop drains the hub's coalesced signals and turns them into SSE
// frames: state snapshots are rebuilt from the engine at drain time so a
// burst of engine states collapses to the newest.
func (c *core) broadcastLoop(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case <-c.hub.stateCh:
			data, err := json.Marshal(c.snapshot())
			if err == nil {
				c.hub.publish(sseFrame(sseStateEvent, data))
			}
		case e := <-c.hub.logCh:
			data, err := json.Marshal(e)
			if err == nil {
				c.hub.publish(sseFrame(sseLogEvent, data))
			}
		}
	}
}

// authAndActivity requires the shared token on every request and stamps
// activity, per the contract's 401 {"error":"unauthorized"}.
func (c *core) authAndActivity(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("X-OpenRung-Token") != c.token {
			writeJSON(w, http.StatusUnauthorized, map[string]string{"error": "unauthorized"})
			return
		}
		c.lastActivityNanos.Store(time.Now().UnixNano())
		next.ServeHTTP(w, r)
	})
}

// endpointPath locates the discovery file: %LOCALAPPDATA%\OpenRung on
// Windows, the platform config dir elsewhere (contract spelling preserved).
func endpointPath() (string, error) {
	if runtime.GOOS == "windows" {
		base := os.Getenv("LOCALAPPDATA")
		if base == "" {
			return "", errors.New("LOCALAPPDATA is not set")
		}
		return filepath.Join(base, "OpenRung", "core-endpoint.json"), nil
	}
	base, err := os.UserConfigDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(base, "openrung", "core-endpoint.json"), nil
}

func writeEndpointFile(ep endpointFile) error {
	path, err := endpointPath()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return err
	}
	data, err := json.MarshalIndent(ep, "", "  ")
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

func removeEndpointFile() error {
	path, err := endpointPath()
	if err != nil {
		return err
	}
	err = os.Remove(path)
	if errors.Is(err, os.ErrNotExist) {
		return nil
	}
	return err
}

// checkExistingEndpoint refuses to start when the endpoint file describes a
// live core: the probe must both connect and authenticate with the recorded
// token, so a stale file from a crashed core never blocks a fresh start.
func checkExistingEndpoint(newPort int) error {
	path, err := endpointPath()
	if err != nil {
		return nil // cannot locate the file: nothing to conflict with
	}
	data, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return nil
	}
	if err != nil {
		return nil // unreadable file: take over
	}
	var old endpointFile
	if json.Unmarshal(data, &old) != nil || old.Port == 0 || old.Token == "" {
		return nil
	}
	if old.Port == newPort {
		return nil // we are binding the port it advertises: it must be gone
	}
	ctx, cancel := context.WithTimeout(context.Background(), 1500*time.Millisecond)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet,
		fmt.Sprintf("http://127.0.0.1:%d/api/version", old.Port), nil)
	if err != nil {
		return nil
	}
	req.Header.Set("X-OpenRung-Token", old.Token)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return nil // old core gone
	}
	resp.Body.Close()
	if resp.StatusCode == http.StatusOK {
		return &exitError{code: 2, err: fmt.Errorf(
			"another openrung-core instance is running (pid %d, port %d)", old.PID, old.Port)}
	}
	return nil
}
