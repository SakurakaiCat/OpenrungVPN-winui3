package main

import (
	"errors"

	"github.com/openrung/openrung/connectcore"

	"openrung/internal/clientstate"
	"openrung/internal/enginepunch"
	"openrung/internal/proxymode"
	"openrung/internal/singboxruntime"
)

// engineHost bundles the connection engine with the host wiring the sidecar
// needs: the bundled sing-box runtime as the tunnel binary, per-OS system
// proxy control, and internal/clientstate persistence (recents, crash-recovery
// proxy snapshot, and the stable proxy port — the same on-disk state the other
// desktop clients read), and the TUN elevation gate. It is the WinUI3
// analogue of cmd/client's engineHost, pinned to the bundled sing-box
// runtime.
type engineHost struct {
	engine    *connectcore.Engine
	elevation elevation
}

func newEngineHost(sink connectcore.EventSink) (*engineHost, error) {
	// The bundled runtime is required, not just default: on Windows the
	// stdin-close stop protocol it speaks is the only graceful-stop channel,
	// so the engine's TUN safety model assumes it.
	singBoxPath, err := singboxruntime.SelfPath()
	if err != nil {
		return nil, err
	}
	engine := connectcore.New()
	engine.Sink = sink
	engine.SingBoxPath = singBoxPath
	engine.SingBoxStopsOnStdinClose = true
	engine.PunchEstablisher = enginepunch.Establish
	engine.OSProxy = osProxyAdapter{ctrl: proxymode.New()}
	engine.Elevation = elevation{}
	// Platform is left at the engine default so the core identifies to the
	// broker exactly like the desktop GUI (connectcore.PlatformDesktop).
	if store, err := clientstate.New(); err == nil {
		engine.Persistence = storeAdapter{store: store}
	}
	return &engineHost{engine: engine, elevation: elevation{}}, nil
}

// The adapters below copy cmd/client/host.go: they bridge connectcore's
// Persistence/OSProxy interfaces onto the root module's internal/clientstate
// and internal/proxymode, which the fetchable connectcore module must not
// depend on.

// storeAdapter implements connectcore.Persistence over internal/clientstate.
// The engine treats the proxy snapshot as opaque; this adapter is where it
// regains its proxymode shape.
type storeAdapter struct{ store *clientstate.Store }

func (a storeAdapter) LoadRecents() []connectcore.RecentNode {
	stored := a.store.LoadRecents()
	out := make([]connectcore.RecentNode, 0, len(stored))
	for _, r := range stored {
		out = append(out, connectcore.RecentNode(r))
	}
	return out
}

func (a storeAdapter) SaveRecents(recents []connectcore.RecentNode) error {
	stored := make([]clientstate.RecentNode, 0, len(recents))
	for _, r := range recents {
		stored = append(stored, clientstate.RecentNode(r))
	}
	return a.store.SaveRecents(stored)
}

func (a storeAdapter) LoadProxyPort() (int, bool) { return a.store.LoadProxyPort() }

func (a storeAdapter) LoadOrSaveProxyPort(candidate int) (int, error) {
	return a.store.LoadOrSaveProxyPort(candidate)
}

func (a storeAdapter) SaveProxySnapshot(snap connectcore.OSProxySnapshot) error {
	typed, ok := snap.(proxymode.Snapshot)
	if !ok {
		return errors.New("proxy snapshot is not a proxymode snapshot")
	}
	return a.store.SaveProxySnapshot(typed)
}

func (a storeAdapter) LoadProxySnapshot() (connectcore.OSProxySnapshot, bool) {
	snap, ok := a.store.LoadProxySnapshot()
	if !ok {
		return nil, false
	}
	return snap, true
}

func (a storeAdapter) ClearProxySnapshot() error {
	return a.store.ClearProxySnapshot()
}

// osProxyAdapter implements connectcore.OSProxy over the per-OS controllers in
// internal/proxymode.
type osProxyAdapter struct{ ctrl proxymode.Controller }

func (a osProxyAdapter) Supported() bool { return a.ctrl.Supported() }

func (a osProxyAdapter) Snapshot() (connectcore.OSProxySnapshot, error) {
	return a.ctrl.Snapshot()
}

func (a osProxyAdapter) Set(host string, port int) error {
	return a.ctrl.Set(host, port)
}

func (a osProxyAdapter) Restore(snap connectcore.OSProxySnapshot) error {
	typed, ok := snap.(proxymode.Snapshot)
	if !ok {
		return errors.New("proxy snapshot is not a proxymode snapshot")
	}
	return a.ctrl.Restore(typed)
}
