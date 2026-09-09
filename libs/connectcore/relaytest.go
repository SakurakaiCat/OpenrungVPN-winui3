package connectcore

import (
	"context"
	"fmt"
	"net/http"
	"sync"
	"time"

	"github.com/openrung/openrung/brokerapi"
)

// Relay latency testing, v2rayN-style: TCPing (pure TCP handshake to the
// relay's public endpoint) and real delay (an HTTP generate_204 that provably
// traverses the tunnel). Both are measurement-only: they never touch the
// engine's session state, the OS proxy, or the telemetry session.

// TcpingSamples / tcpingConcurrency shape the tcping fan-out: each relay gets
// TcpingSamples handshake attempts (the average is reported, losses counted)
// and at most tcpingRelayConcurrency relays are probed at once, so the whole
// directory costs roughly samples × one probe timeout regardless of size.
const (
	// TcpingSamples mirrors v2rayN's per-node sample count.
	TcpingSamples = 3
	// tcpingRelayConcurrency bounds the parallel dials.
	tcpingRelayConcurrency = 8
	// RelayTestTimeout bounds one relay's full real-delay rung (TCP dial +
	// tunnel start + internet probe + a punch coordination budget).
	RelayTestTimeout = 25 * time.Second
)

// TcpingResult is one relay's TCP handshake measurement. AvgMS is nil when
// every sample failed (unreachable / timed out); Loss counts failed samples.
type TcpingResult struct {
	RelayID string `json:"relayId"`
	Host    string `json:"host"`
	Port    int    `json:"port"`
	AvgMS   *int64 `json:"avgMs"`
	Loss    int    `json:"loss"`
	Samples int    `json:"samples"`
}

// RealDelayResult is one relay's through-tunnel HTTP latency, or the error
// that stopped its rung. MS is nil when the rung failed.
type RealDelayResult struct {
	RelayID string `json:"relayId"`
	MS      *int64 `json:"ms"`
	Error   string `json:"error,omitempty"`
}

// TcpingRelays measures TCP handshake latency to relays' public endpoints.
// ids narrows the probe set (empty: every usable relay). Reuses the connect
// ladder's own dialer so a socket-protection refusal reads the same as it
// would while connecting.
func (s *Engine) TcpingRelays(ctx context.Context, ids []string, samples int) ([]TcpingResult, error) {
	if samples < 1 {
		samples = TcpingSamples
	}
	if samples > 5 {
		samples = 5
	}
	resp, err := s.ListRelaysForDirectory()
	if err != nil {
		return nil, fmt.Errorf("relay directory: %w", err)
	}
	cands := usableRelays(resp)
	if len(ids) > 0 {
		want := make(map[string]bool, len(ids))
		for _, id := range ids {
			want[id] = true
		}
		filtered := make([]brokerapi.RelayDescriptor, 0, len(want))
		for _, cand := range cands {
			if want[cand.ID] {
				filtered = append(filtered, cand)
			}
		}
		cands = filtered
	}

	probe := s.relayDialer()
	results := make([]TcpingResult, len(cands))
	sem := make(chan struct{}, tcpingRelayConcurrency)
	var wg sync.WaitGroup
	for i, cand := range cands {
		wg.Add(1)
		go func() {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()

			res := TcpingResult{
				RelayID: cand.ID, Host: cand.PublicHost, Port: cand.PublicPort,
				Samples: samples,
			}
			var total int64
			var ok int
			for j := 0; j < samples; j++ {
				if ctx.Err() != nil {
					break
				}
				pctx, cancel := context.WithTimeout(ctx, RelayRankProbeTimeout)
				ms, err := probe(pctx, cand.PublicHost, cand.PublicPort)
				cancel()
				if err != nil {
					res.Loss++
					continue
				}
				total += ms
				ok++
			}
			if ok > 0 {
				avg := total / int64(ok)
				res.AvgMS = &avg
			}
			results[i] = res
		}()
	}
	wg.Wait()
	return results, nil
}

// TestRelayDelay runs a throwaway tunnel through one relay — the connect
// ladder's own attempt + internet-probe machinery, minus promotion: no engine
// state change, no OS proxy, no telemetry session (a nil-manager synthetic
// connection keeps every telemetry call a no-op). Returns the ladder's
// internet-probe milliseconds. Refuses while a session is live (the probe
// would race the active tunnel for the local port) and in TUN mode (a probe
// inbound would fight the device-wide capture).
func (s *Engine) TestRelayDelay(ctx context.Context, relayID string) (int64, error) {
	if st := s.State().Status; st != StatusDisconnected {
		return 0, fmt.Errorf("core is %s; disconnect before testing relays", st)
	}
	if s.Mode() == ModeTUN {
		return 0, fmt.Errorf("TUN 模式下无法逐节点测试；请先切回代理模式")
	}
	resp, err := s.ListRelaysForDirectory()
	if err != nil {
		return 0, fmt.Errorf("relay directory: %w", err)
	}
	var cand *brokerapi.RelayDescriptor
	for _, r := range usableRelays(resp) {
		if r.ID == relayID {
			c := r
			cand = &c
			break
		}
	}
	if cand == nil {
		return 0, fmt.Errorf("relay %s not found in directory", relayID)
	}

	port, err := s.LocalProxyPort()
	if err != nil {
		return 0, err
	}
	if err := EnsureProxyPortAvailable(port); err != nil {
		return 0, err
	}

	// Synthetic connection: nil telemetry manager (Record is nil-safe), zero
	// punch breaker (unknown relays are allowed through). The rung publishes
	// nothing and its teardown releases every socket it opened.
	conn := &connection{}
	tctx, cancel := context.WithTimeout(ctx, RelayTestTimeout)
	defer cancel()
	res, err := s.attemptCandidate(tctx, conn, *cand, port, 1)
	if err != nil {
		return 0, err
	}
	ms := res.probeMS
	res.teardown()
	if ms <= 0 {
		return 0, fmt.Errorf("probe produced no timing")
	}
	return ms, nil
}

// ProbeThroughTunnel times one generate_204 through the live mixed inbound on
// proxyPort — the v2rayN real-ping shape: two samples, the faster counts.
func ProbeThroughTunnel(ctx context.Context, proxyPort int) (int64, error) {
	client := proxyProbeClient(proxyPort)
	defer closeIdle(client)
	return tunnelProbeTiming(ctx, client)
}

// ProbeDirect times one generate_204 on the default network. Used for the
// active relay in TUN mode, where the device-wide capture already routes all
// traffic through the tunnel and no local inbound exists.
func ProbeDirect(ctx context.Context) (int64, error) {
	client := &http.Client{Timeout: InternetProbeRequestTimeout}
	return tunnelProbeTiming(ctx, client)
}

func tunnelProbeTiming(ctx context.Context, client *http.Client) (int64, error) {
	var best int64 = -1
	var lastErr error
	for i := 0; i < 2; i++ {
		start := time.Now()
		if err := probeOnce(ctx, client, InternetProbeURLs[0]); err != nil {
			lastErr = err
			continue
		}
		ms := time.Since(start).Milliseconds()
		if best < 0 || ms < best {
			best = ms
		}
	}
	if best < 0 {
		if lastErr != nil {
			return 0, lastErr
		}
		return 0, fmt.Errorf("probe failed")
	}
	return best, nil
}
