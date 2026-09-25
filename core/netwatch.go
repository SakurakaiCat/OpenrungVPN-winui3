package main

import (
	"context"
	"fmt"
	"sort"
	"strings"
	"time"

	"github.com/openrung/openrung/connectcore"
)

// ---- Windows physical-network epoch monitor (the ADR-003 A2 seam) ---------
//
// connectcore's Engine consumes platform network signals through
// UpdateNetworkState (libs/connectcore/network.go). Mobile hosts feed their
// OS connectivity callbacks into that seam; the upstream desktop CLI and TUI
// never do, so on Windows a physical change (Wi-Fi <-> Ethernet, sleep
// resume, DHCP renewal) is only noticed after the engine's own probes fail —
// a health-check timeout plus the 5-second recovery-gate poll before a
// fresh ladder even starts, and a live WSS session lingers until its socket
// dies.
//
// This monitor is the Windows adapter for that seam. It snapshots the
// physical (non-tunnel) adapters once per second and feeds every
// observation to the engine, which deduplicates: only a changed fingerprint
// advances the epoch, which
//
//   - retires a live WSS session at once (the mobile invariant: a WSS
//     socket belongs to one physical epoch) and recovers with a fresh
//     direct-first ladder;
//   - triggers an immediate health sweep on a direct or punched session
//     instead of waiting out the probe interval;
//   - releases waitForNetworkRecovery's gate instantly (its netNotify
//     path) instead of waiting out the poll.
//
// Semantics follow the mobile monitors, which network.go documents as the
// spec: change detection is by fingerprint of the physical path set, the
// first observation is the engine's silent baseline, and the engine's own
// end-to-end probes stay authoritative — Up only shapes epoch identity and
// never suppresses desktop probing (physicalNetworkKnownDown is
// mobile-only, so a misjudged Up can never stall a desktop session).

// physicalNetPollInterval bounds detection latency. The engine's recovery
// gate polls at 5s and its health checks slower still, so one cheap
// GetAdaptersAddresses call per second turns every one of those waits into
// at most one poll interval of latency.
const physicalNetPollInterval = time.Second

// gatherPhysicalAdapters snapshots the host's adapters. It is installed by
// netwatch_windows.go; nil (non-Windows builds) disables the monitor.
var gatherPhysicalAdapters func() ([]netAdapter, error)

// netAdapter is the platform-independent projection of one adapter, sized
// to what the fingerprint needs: identity (friendly name, description),
// liveness (operStatus), interface metric for best-path selection, and the
// unicast and gateway address sets.
type netAdapter struct {
	name     string
	desc     string
	ifType   uint32
	up       bool
	metric   uint32
	addrs    []string
	gateways []string
}

const ifTypeSoftwareLoopback = 24

// isTunnelAdapter reports whether an adapter belongs to a tunnel stack. The
// engine's own TUN adapter (sing-tun creates wintun devices named tun<N>)
// must never feed the fingerprint — otherwise every connect and disconnect
// would fire a self-inflicted epoch boundary right after promote. Other
// VPN clients' adapters (TAP/TUN of any name) are left in: on Windows their
// appearance or disappearance is a real routing change, and TUN mode
// already refuses to start while one is up (tun_conflict.go).
func isTunnelAdapter(a netAdapter) bool {
	if tunNamePattern().MatchString(a.name) {
		return true
	}
	desc := strings.ToLower(a.desc)
	return strings.Contains(desc, "wintun") ||
		strings.Contains(desc, "sing-box") ||
		strings.Contains(desc, "openrung")
}

// physicalNetworkState folds one adapter snapshot into the engine's
// NetworkState. Up mirrors the mobile capability rule — an up, addressed,
// physical adapter exists — without judging internet validation (the
// engine's probes own that verdict). The fingerprint carries the best
// adapter's identity, metric, address set, and gateways: the Windows
// analogue of Android's best-matching-network properties. A gatewayed
// adapter (an actual internet path) beats a merely addressed one, and
// within each pool the lowest interface metric wins, name-broken ties.
//
// Adapter DNS is deliberately excluded from the fingerprint: sing-tun
// repoints the system resolver when a TUN session starts, and a repoint
// reflected here would retire a freshly promoted WSS session on the very
// connect that caused it. A real network switch changes addresses too, so
// nothing observable is lost.
func physicalNetworkState(adapters []netAdapter) connectcore.NetworkState {
	var physical []netAdapter
	for _, a := range adapters {
		if a.ifType == ifTypeSoftwareLoopback || isTunnelAdapter(a) || !a.up || len(a.addrs) == 0 {
			continue
		}
		physical = append(physical, a)
	}

	var gatewayed []netAdapter
	for _, a := range physical {
		if len(a.gateways) > 0 {
			gatewayed = append(gatewayed, a)
		}
	}

	var best *netAdapter
	for _, pool := range [][]netAdapter{gatewayed, physical} {
		if len(pool) == 0 {
			continue
		}
		sort.Slice(pool, func(i, j int) bool {
			if pool[i].metric != pool[j].metric {
				return pool[i].metric < pool[j].metric
			}
			return pool[i].name < pool[j].name
		})
		best = &pool[0]
		break
	}

	state := connectcore.NetworkState{}
	if best == nil {
		return state // Up=false, fingerprint "" — the mobile "no physical network" shape
	}
	addrs := append([]string(nil), best.addrs...)
	sort.Strings(addrs)
	gateways := append([]string(nil), best.gateways...)
	sort.Strings(gateways)
	state.Up = true
	state.Fingerprint = fmt.Sprintf("%s|%d|%s|%s", best.name, best.metric,
		strings.Join(addrs, ","), strings.Join(gateways, ","))
	return state
}

// watchPhysicalNetwork feeds the engine a physical-network snapshot every
// poll interval. The engine deduplicates identical observations, so the
// steady state is one snapshot call per second with zero goroutine churn;
// a persistently failing snapshot logs once per minute at worst.
func (c *core) watchPhysicalNetwork(ctx context.Context) {
	if gatherPhysicalAdapters == nil {
		return // platform without the monitor (non-Windows builds)
	}
	if adapters, err := gatherPhysicalAdapters(); err == nil {
		// The engine records the first observation as its silent baseline.
		c.engine.UpdateNetworkState(physicalNetworkState(adapters))
	}
	failures := 0
	ticker := time.NewTicker(physicalNetPollInterval)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			adapters, err := gatherPhysicalAdapters()
			if err != nil {
				failures++
				if failures%60 == 1 {
					c.apiLog("physical network snapshot failed: %v", err)
				}
				continue
			}
			failures = 0
			c.engine.UpdateNetworkState(physicalNetworkState(adapters))
		}
	}
}
