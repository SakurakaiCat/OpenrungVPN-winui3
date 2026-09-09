//go:build windows

package main

import (
	"fmt"

	"golang.org/x/sys/windows"
)

// tunModeAvailable reports whether this process may create a TUN device and
// install routes: on Windows that is an elevated token — creating the wintun
// adapter needs Administrator. Unlike the CLI there is no "rerun elevated"
// hint: the WinUI app drives the restart itself (Verb=runas respawn), so the
// refusal just tells it that elevation is what is missing.
//
// Stopping the tunnel is safe here because the core pins the bundled
// sing-box runtime, whose stdin-close stop protocol is the graceful-stop
// channel that works on Windows (see engineHost).
func tunModeAvailable() error {
	if windows.GetCurrentProcessToken().IsElevated() {
		return nil
	}
	return fmt.Errorf(
		"TUN mode needs Administrator privileges to create the tunnel device: %w; the app must restart the core elevated",
		ErrElevationRequired,
	)
}
