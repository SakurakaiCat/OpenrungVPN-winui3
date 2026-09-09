package main

import (
	"context"
	"errors"

	"github.com/openrung/openrung/connectcore"
)

// ErrElevationRequired marks a refusal for missing privileges so the server
// can translate it into the contract's 428 {"code":"elevation_required"}
// response. Platform Elevate implementations wrap it.
var ErrElevationRequired = errors.New("elevated privileges required")

// elevation implements connectcore.Elevation for the sidecar. Like the CLI
// host, this process cannot acquire privileges it was not started with — the
// WinUI app respawns the core elevated instead — so Elevate verifies the
// platform check and refuses with guidance when it fails. Per-platform
// mechanics live in the tunModeAvailable implementations beside this file.
type elevation struct{}

var _ connectcore.Elevation = elevation{}

func (elevation) Elevate(context.Context) error { return tunModeAvailable() }

// elevated reports whether this process currently holds the privileges
// /api/version and /api/state advertise and the UI gates the TUN toggle on.
func elevated() bool { return tunModeAvailable() == nil }
