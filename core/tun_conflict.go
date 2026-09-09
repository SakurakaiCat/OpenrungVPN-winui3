package main

import (
	"fmt"
	"net"
	"regexp"
)

// tunDeviceConflict refuses a TUN connect when a TUN device already exists on
// the machine. sing-tun names its device tun0/tun1/… (CalculateInterfaceName
// scans net.Interfaces for the next free "tun"+index on Windows and Linux), so
// any tun<N> interface predates this connect:
//
//   - another VPN/proxy client owns a TUN: our auto_route would fight its
//     routes and blackhole the network, surfacing only as an opaque sing-box
//     route failure or a "connected but no internet" verdict that blames the
//     relay;
//   - a crashed previous core left one behind: its stale routes point at a
//     dead adapter and poison the new tunnel the same way.
//
// Either way the user must act (disconnect the other client / remove the
// stale adapter) or stay in proxy mode, so the check runs as a /api/connect
// pre-flight and the UI surfaces the contract's 409 tun_conflict.
func tunDeviceConflict() error {
	ifaces, err := net.Interfaces()
	if err != nil {
		// Enumeration is advisory: a failure here must not block TUN
		// connects, the tunnel start itself still reports real failures.
		return nil
	}
	if name := tunConflictName(ifaces); name != "" {
		return fmt.Errorf(
			"TUN device %s already exists (another VPN/proxy client, or a crashed core left it behind); disconnect it or remove the adapter, or switch to proxy mode which needs no privileges",
			name)
	}
	return nil
}

// tunConflictName returns the name of the first tun<N> interface, or "" when
// none exists. Split out for unit testing without a real TUN device.
func tunConflictName(ifaces []net.Interface) string {
	re := tunNamePattern()
	for _, iface := range ifaces {
		if re.MatchString(iface.Name) {
			return iface.Name
		}
	}
	return ""
}

func tunNamePattern() *regexp.Regexp {
	return regexp.MustCompile(`^tun[0-9]+$`)
}
