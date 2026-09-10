package connectcore

import "testing"

func TestSetTunnelDNSURLoundTrip(t *testing.T) {
	var engine Engine

	// Zero value: defaults, IPv6 enabled.
	servers, ipv6 := engine.TunnelDNS()
	if servers != nil || !ipv6 {
		t.Fatalf("fresh engine should report defaults with IPv6 enabled, got %v ipv6=%t", servers, ipv6)
	}

	if err := engine.SetTunnelDNS([]string{"9.9.9.9", "149.112.112.112"}, false); err != nil {
		t.Fatalf("SetTunnelDNS: %v", err)
	}
	servers, ipv6 = engine.TunnelDNS()
	if len(servers) != 2 || servers[0] != "9.9.9.9" || ipv6 {
		t.Fatalf("round trip lost the config, got %v ipv6=%t", servers, ipv6)
	}

	// Empty list restores the defaults and re-enables IPv6.
	if err := engine.SetTunnelDNS(nil, true); err != nil {
		t.Fatalf("SetTunnelDNS: %v", err)
	}
	servers, ipv6 = engine.TunnelDNS()
	if servers != nil || !ipv6 {
		t.Fatalf("clearing should restore defaults, got %v ipv6=%t", servers, ipv6)
	}
}

func TestSetTunnelDNSValidation(t *testing.T) {
	var engine Engine
	if err := engine.SetTunnelDNS([]string{"dns.google"}, true); err == nil {
		t.Fatal("a hostname server has no bootstrap resolver and must be rejected")
	}
	if err := engine.SetTunnelDNS([]string{"9.9.9.9", "149.112.112.112", "1.1.1.1", "8.8.8.8", "1.0.0.1"}, true); err == nil {
		t.Fatal("too many servers must be rejected")
	}
	if _, ipv6 := engine.TunnelDNS(); !ipv6 {
		t.Fatal("rejected calls must not mutate the stored config")
	}
}

func TestSetTunnelDNSRefusedWhileConnected(t *testing.T) {
	// The refusal is the /api/dns 409's source: a live session keeps the DNS
	// it started with.
	engine := Engine{conn: &connection{}}
	if err := engine.SetTunnelDNS([]string{"9.9.9.9"}, true); err == nil {
		t.Fatal("SetTunnelDNS must refuse while a connection is live")
	}
	servers, ipv6 := engine.TunnelDNS()
	if servers != nil || !ipv6 {
		t.Fatalf("refused call must not mutate the config, got %v ipv6=%t", servers, ipv6)
	}
}
