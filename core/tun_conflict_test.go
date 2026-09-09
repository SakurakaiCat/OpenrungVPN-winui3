package main

import (
	"net"
	"testing"
)

func TestTunConflictName(t *testing.T) {
	mk := func(names ...string) []net.Interface {
		out := make([]net.Interface, 0, len(names))
		for _, n := range names {
			out = append(out, net.Interface{Name: n})
		}
		return out
	}

	tests := []struct {
		name    string
		ifaces  []net.Interface
		wantHit string
	}{
		{"empty", nil, ""},
		{"normal interfaces", mk("eth0", "wlan0", "lo", "Loopback Pseudo-Interface 1"), ""},
		{"sing-tun leftover", mk("eth0", "tun0"), "tun0"},
		{"second index", mk("tun1"), "tun1"},
		{
			"prefix lookalikes are not TUN devices",
			mk("tunnelbear", "tunx", "tun", "tun10x", "virtual_tun2", "tun0.bak"),
			"",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := tunConflictName(tt.ifaces); got != tt.wantHit {
				t.Fatalf("tunConflictName(%v) = %q, want %q", tt.ifaces, got, tt.wantHit)
			}
		})
	}
}
