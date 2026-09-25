package main

import (
	"strings"
	"testing"

	"github.com/openrung/openrung/connectcore"
)

func physicalAdapter(name string, up bool, addrs, gateways []string) netAdapter {
	return netAdapter{name: name, desc: "test adapter", ifType: 6, up: up, metric: 25, addrs: addrs, gateways: gateways}
}

func TestPhysicalNetworkStateNoPhysicalNetwork(t *testing.T) {
	for name, adapters := range map[string][]netAdapter{
		"empty":         nil,
		"all down":      {physicalAdapter("Wi-Fi", false, []string{"192.168.1.5"}, []string{"192.168.1.1"})},
		"no address":    {physicalAdapter("Wi-Fi", true, nil, []string{"192.168.1.1"})},
		"loopback":      {{name: "Loopback", desc: "", ifType: ifTypeSoftwareLoopback, up: true, addrs: []string{"127.0.0.1"}}},
		"own tun":       {physicalAdapter("tun0", true, []string{"172.19.0.1"}, []string{"172.19.0.1"})},
		"wintun desc":   {netAdapter{name: "Local Area Connection", desc: "Wintun Userspace Tunnel", up: true, addrs: []string{"10.9.8.7"}}},
		"sing-box desc": {netAdapter{name: "Ethernet 2", desc: "sing-box tun", up: true, addrs: []string{"10.9.8.7"}}},
	} {
		state := physicalNetworkState(adapters)
		if state.Up {
			t.Fatalf("%s: expected no physical network, got %+v", name, state)
		}
		if state.Fingerprint != "" {
			t.Fatalf("%s: expected empty fingerprint, got %q", name, state.Fingerprint)
		}
	}
}

func TestPhysicalNetworkStateFingerprintOfBest(t *testing.T) {
	wifi := physicalAdapter("Wi-Fi", true, []string{"192.168.1.5"}, []string{"192.168.1.1"})
	wifi.metric = 50
	ethernet := physicalAdapter("Ethernet", true, []string{"10.0.0.5"}, []string{"10.0.0.1"})
	ethernet.metric = 25

	state := physicalNetworkState([]netAdapter{wifi, ethernet})
	if !state.Up {
		t.Fatalf("expected a physical network")
	}
	// The lower-metric gatewayed adapter wins, carrying its whole path set.
	if !strings.HasPrefix(state.Fingerprint, "Ethernet|25|10.0.0.5|10.0.0.1") {
		t.Fatalf("unexpected fingerprint %q", state.Fingerprint)
	}

	// A gatewayed adapter beats a merely addressed one even at a higher
	// metric: an internet path outranks a captive or link-local island.
	captive := physicalAdapter("Ethernet", true, []string{"169.254.7.7"}, nil)
	captive.metric = 10
	wifi2 := physicalAdapter("Wi-Fi", true, []string{"192.168.1.5"}, []string{"192.168.1.1"})
	wifi2.metric = 50
	state = physicalNetworkState([]netAdapter{captive, wifi2})
	if !strings.Contains(state.Fingerprint, "192.168.1.1") {
		t.Fatalf("gatewayed adapter should win, got %q", state.Fingerprint)
	}
}

func TestPhysicalNetworkStateChangesOnAddressAndName(t *testing.T) {
	before := physicalNetworkState([]netAdapter{
		{name: "Wi-Fi", desc: "", ifType: 6, up: true, metric: 50, addrs: []string{"192.168.1.5"}, gateways: []string{"192.168.1.1"}},
	})
	// A DHCP renewal that changes only the address is an epoch boundary.
	after := physicalNetworkState([]netAdapter{
		{name: "Wi-Fi", desc: "", ifType: 6, up: true, metric: 50, addrs: []string{"192.168.1.6"}, gateways: []string{"192.168.1.1"}},
	})
	if before == after {
		t.Fatalf("address change must change the state: %+v", after)
	}
	// So is losing the last physical adapter.
	if physicalNetworkState(nil).Up {
		t.Fatalf("losing the network must read Up=false")
	}
	// The engine's own TUN appearing must NOT be a change.
	tun := physicalNetworkState([]netAdapter{
		{name: "Wi-Fi", desc: "", ifType: 6, up: true, metric: 50, addrs: []string{"192.168.1.5"}, gateways: []string{"192.168.1.1"}},
		{name: "tun0", desc: "Wintun", ifType: 6, up: true, metric: 5, addrs: []string{"172.19.0.1"}, gateways: []string{"172.19.0.1"}},
	})
	if tun != before {
		t.Fatalf("tunnel adapter must not alter the fingerprint: %+v vs %+v", tun, before)
	}
}

// The engine's own seam contract: the first observation baselines silently,
// identical observations deduplicate, and only a changed fingerprint
// advances the epoch. This pins the monitor's output onto that contract so
// a connectcore upgrade that breaks the wiring fails here first.
type captureSink struct {
	connectcore.EventSink
	logs []string
}

func (s *captureSink) Log(entry connectcore.LogEntry) { s.logs = append(s.logs, entry.Line) }

func TestPhysicalNetworkStateFeedsEngineEpoch(t *testing.T) {
	eng := connectcore.New()
	sink := &captureSink{}
	eng.Sink = sink
	defer eng.Stop()

	down := physicalNetworkState(nil)
	up := physicalNetworkState([]netAdapter{
		{name: "Wi-Fi", desc: "", ifType: 6, up: true, metric: 50, addrs: []string{"192.168.1.5"}, gateways: []string{"192.168.1.1"}},
	})
	eng.UpdateNetworkState(down) // baseline: absorbed, no epoch log
	eng.UpdateNetworkState(up)   // physical network restored
	eng.UpdateNetworkState(up)   // duplicate observation: no second epoch

	want := []string{"physical network baseline recorded", "physical network restored"}
	if strings.Join(sink.logs, "\n") != strings.Join(want, "\n") {
		t.Fatalf("unexpected engine log sequence %q", sink.logs)
	}
}
