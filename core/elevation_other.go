//go:build !windows

package main

import (
	"fmt"
	"os"
	"path/filepath"
)

// tunModeAvailable reports whether this process may create a TUN device and
// install routes. The check is deliberately the effective uid rather than an
// inspection of capabilities: sudo is the documented way to run the core in
// TUN mode (same policy as cmd/client), and wrongly admitting a
// CAP_NET_ADMIN-only process would surface as an opaque sing-box failure
// after the ladder had already dialed relays.
func tunModeAvailable() error {
	if os.Geteuid() == 0 {
		return nil
	}
	return fmt.Errorf(
		"TUN mode needs root privileges to create the tunnel device: rerun as `sudo %s serve`, or stay in proxy mode (no privileges needed): %w",
		filepath.Base(os.Args[0]), ErrElevationRequired,
	)
}
