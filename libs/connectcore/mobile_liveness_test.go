package connectcore

import (
	"context"
	"errors"
	"net"
	"net/http"
	"net/http/httptest"
	"reflect"
	"sync/atomic"
	"testing"
	"time"

	"fmt"
	"github.com/openrung/openrung/brokerapi"
	"github.com/openrung/openrung/connectcore/discovery"
	"strings"
)

func TestMobileLivenessAllowsBlockedBrokerWithReachableNeutralEndpoint(t *testing.T) {
	want := []string{"https://www.gstatic.com/generate_204", "https://cp.cloudflare.com/generate_204"}
	if got := New().mobileLivenessEndpoints(); !reflect.DeepEqual(got, want) {
		t.Fatalf("neutral endpoints drifted: %v", got)
	}
	neutral := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodHead {
			t.Errorf("method = %s", r.Method)
		}
		for key := range r.Header {
			if key != "Cache-Control" && key != "Connection" {
				t.Errorf("unexpected identifying header: %s", key)
			}
		}
		w.WriteHeader(http.StatusServiceUnavailable) // Any response proves connectivity.
	}))
	defer neutral.Close()
	s := New()
	s.physicalLivenessURLs = []string{neutral.URL}
	s.Mobile = &MobileHost{}
	protector := &recordingProtector{}
	s.SetSocketProtector(protector)
	// A blocked broker is deliberately not a prerequisite for re-laddering.
	if !s.networkAlive(context.Background(), []string{"127.0.0.1:1"}) {
		t.Fatal("neutral response did not permit recovery")
	}
	if protector.count() == 0 {
		t.Fatal("physical probe socket was not protected")
	}
}

func TestMobileLivenessDoesNotFollowRedirects(t *testing.T) {
	var redirected atomic.Int32
	target := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { redirected.Add(1) }))
	defer target.Close()
	redirect := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Redirect(w, r, target.URL, http.StatusFound)
	}))
	defer redirect.Close()
	s := New()
	s.physicalLivenessURLs = []string{redirect.URL}
	if !s.physicalNetworkAlive(context.Background()) {
		t.Fatal("verified endpoint redirect should permit the ladder")
	}
	if redirected.Load() != 0 {
		t.Fatal("followed a physical probe redirect")
	}
}

func TestMobileLivenessCancellationAndProtectionRefusal(t *testing.T) {
	entered := make(chan struct{})
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		close(entered)
		<-r.Context().Done()
	}))
	defer server.Close()
	ctx, cancel := context.WithCancel(context.Background())
	result := make(chan bool, 1)
	s := New()
	s.physicalLivenessURLs = []string{server.URL}
	go func() { result <- s.physicalNetworkAlive(ctx) }()
	<-entered
	cancel()
	select {
	case alive := <-result:
		if alive {
			t.Fatal("cancelled probe proved liveness")
		}
	case <-time.After(time.Second):
		t.Fatal("probe did not join on cancellation")
	}
	s.SetSocketProtector(&recordingProtector{refuse: true})
	if !s.physicalNetworkAlive(context.Background()) {
		t.Fatal("protection refusal must let the ladder report local failure")
	}
	if s.physicalNetworkAlive(ctx) {
		t.Fatal("cancelled probe must not dial")
	}
	// The protected resolver must preserve the same local-failure decision.
	s.SetDNSServers([]string{"127.0.0.1"})
	s.physicalLivenessURLs = []string{"https://neutral-test.invalid/"}
	if !s.physicalNetworkAlive(context.Background()) {
		t.Fatal("DNS protection refusal held recovery")
	}
}

func TestMobileLocationDetailsNeverFallBackToOperatorName(t *testing.T) {
	for _, tc := range []struct{ city, country, want string }{
		{" Tokyo ", " Japan ", "Tokyo, Japan"}, {"Tokyo", "", "Tokyo"}, {"", "Japan", "Japan"}, {"", "", ""},
	} {
		s := New()
		s.Mobile = &MobileHost{}
		s.core.status = StatusConnected
		s.conn = &connection{active: &candidateResult{relay: brokerapi.RelayDescriptor{
			ID: "relay_ab12", Label: "\u202eevil", RelayGeoLocation: brokerapi.RelayGeoLocation{City: tc.city, Country: tc.country},
		}}}
		details := s.detailsLocked()
		if details.LocationLabel != tc.want {
			t.Fatalf("location = %q, want %q", details.LocationLabel, tc.want)
		}
		if details.RelayName != "\u202eevil" {
			t.Fatal("operator name must remain separate for native sanitization")
		}
		s.core.status = StatusConnecting
		details = s.detailsLocked()
		if !reflect.DeepEqual(details, &StateDetails{}) {
			t.Fatalf("recovery retained relay/location details: %+v", details)
		}
	}
}

func TestMobileLivenessAllowsBlockedNeutralEndpointsWithReachableFront(t *testing.T) {
	front, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer front.Close()
	s := New()
	s.Mobile = &MobileHost{}
	s.physicalLivenessURLs = []string{"http://127.0.0.1:1", "http://127.0.0.1:1"}
	s.SetSocketProtector(&recordingProtector{})
	if !s.networkAlive(context.Background(), []string{front.Addr().String()}) {
		t.Fatal("reachable front was blocked by neutral failures")
	}
	if s.networkAlive(context.Background(), []string{"127.0.0.1:1"}) {
		t.Fatal("unreachable endpoints proved connectivity")
	}
}

func TestMobileLivenessAllowsSlowResponseAndRejectsUntrustedTLS(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		select {
		case <-time.After(3200 * time.Millisecond):
			w.WriteHeader(http.StatusNoContent)
		case <-r.Context().Done():
		}
	}))
	defer server.Close()
	s := New()
	s.physicalLivenessURLs = []string{server.URL}
	if !s.physicalNetworkAlive(context.Background()) {
		t.Fatal("usable response beyond three seconds rejected")
	}
	untrusted := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { w.WriteHeader(http.StatusFound) }))
	defer untrusted.Close()
	s.physicalLivenessURLs = []string{untrusted.URL}
	if s.physicalNetworkAlive(context.Background()) {
		t.Fatal("untrusted TLS response proved neutral liveness")
	}
}

func TestPhysicalTransportsNeverUseProcessProxy(t *testing.T) {
	s := New()
	s.SetSocketProtector(&recordingProtector{})
	for _, transport := range []*http.Transport{
		protectedTransport(s.currentProtector(), nil),
		s.geoHTTPClient().Transport.(*http.Transport),
		s.punchCoordinationClient().Transport.(*http.Transport),
	} {
		if transport.Proxy != nil {
			t.Fatal("physical transport inherited a process proxy")
		}
	}
}

func TestMobileDownStateSkipsAllPhysicalProbesAndHoldExpires(t *testing.T) {
	s := New()
	s.Mobile = &MobileHost{}
	s.networkRecoveryLimit = 25 * time.Millisecond
	s.networkRetryDelay = 2 * time.Millisecond
	s.UpdateNetworkState(NetworkState{Up: false, Fingerprint: "airplane"})
	var probes atomic.Int32
	s.checkNetworkAlive = func(context.Context, []string) bool { probes.Add(1); return false }
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	conn := &connection{netNotify: make(chan struct{}, 1)}
	if !s.waitForNetworkRecovery(ctx, conn, s.newRecoveryBudget()) {
		t.Fatal("down state held the post-teardown gate forever")
	}
	if probes.Load() != 0 {
		t.Fatal("known-down network performed physical probes")
	}
	if ctx.Err() != nil {
		t.Fatal("released only at parent deadline")
	}
}

func TestMobileBlockedProbesCannotHoldHealthOrRecoveryForever(t *testing.T) {
	for _, health := range []bool{false, true} {
		s := New()
		s.Mobile = &MobileHost{}
		s.healthTick = time.Millisecond
		s.networkRecoveryLimit = 25 * time.Millisecond
		s.networkRetryDelay = time.Millisecond
		s.checkNetworkAlive = func(context.Context, []string) bool { return false }
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		if health {
			failed := make(chan error, 1)
			done := make(chan struct{})
			go func() {
				defer close(done)
				s.healthLoopWithProbe(ctx, 0, nil, failed, nil,
					func(context.Context, int) error { return errors.New("remote path lost") }, nil)
			}()
			select {
			case <-failed:
			case <-ctx.Done():
				t.Fatal("health's network-down gate never permitted recovery")
			}
			cancel()
			<-done
		} else {
			if !s.waitForNetworkRecovery(ctx, &connection{netNotify: make(chan struct{}, 1)}, s.newRecoveryBudget()) {
				t.Fatal("post-teardown gate never permitted recovery")
			}
			cancel()
		}
	}
}

// Observe entry to the outage wait before delivering an up epoch. Without this
// barrier the test could pass by observing Up on its very first liveness check.
type recoveryWaitingSink struct {
	testSink
	ready chan struct{}
}

func (s *recoveryWaitingSink) Log(entry LogEntry) {
	s.testSink.Log(entry)
	if entry.Line == "the tunnel stopped while the local network is down; waiting for connectivity" {
		s.ready <- struct{}{}
	}
}

func TestMobileRecoveryBackoffAndNetworkSignal(t *testing.T) {
	delay := networkRecoveryPollInterval
	var got []time.Duration
	for i := 0; i < 7; i++ {
		got = append(got, delay)
		delay = nextMobileRecoveryDelay(delay)
	}
	want := []time.Duration{5 * time.Second, 10 * time.Second, 20 * time.Second, 40 * time.Second, 60 * time.Second, 60 * time.Second, 60 * time.Second}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("retry delays = %v", got)
	}
	s := New()
	s.Mobile = &MobileHost{}
	s.networkRetryDelay = time.Hour // Only a network signal may wake this test's timer.
	waiting := &recoveryWaitingSink{ready: make(chan struct{}, 1)}
	s.Sink = waiting
	s.UpdateNetworkState(NetworkState{Up: false, Fingerprint: "airplane"})
	var probes atomic.Int32
	s.checkNetworkAlive = func(context.Context, []string) bool { probes.Add(1); return true }
	conn := &connection{netNotify: make(chan struct{}, 1)}
	s.conn = conn
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	result := make(chan bool, 1)
	go func() { result <- s.waitForNetworkRecovery(ctx, conn, s.newRecoveryBudget()) }()
	select {
	case <-waiting.ready:
	case <-ctx.Done():
		t.Fatal("recovery never entered the outage wait")
	}
	s.UpdateNetworkState(NetworkState{Up: true, Fingerprint: "cellular"})
	if !<-result {
		t.Fatal("up signal failed to wake recovery")
	}
	if probes.Load() != 1 {
		t.Fatalf("physical checks = %d", probes.Load())
	}
	cancel()
	if s.networkAliveBefore(ctx, nil, &networkRecoveryBudget{deadline: time.Now().Add(-time.Second)}) {
		t.Fatal("expired hold defeated cancellation")
	}
}

func TestMobileRecoveryBudgetCancelsAnInFlightProbe(t *testing.T) {
	s := New()
	s.Mobile = &MobileHost{}
	s.checkNetworkAlive = func(ctx context.Context, _ []string) bool {
		<-ctx.Done()
		return false
	}
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	if !s.networkAliveBefore(ctx, nil, &networkRecoveryBudget{deadline: time.Now().Add(25 * time.Millisecond)}) || ctx.Err() != nil {
		t.Fatal("probe outlived the mobile budget or consumed the parent cancellation")
	}
}

func TestMobilePromoteKeepsAllLocationSurfacesGeographic(t *testing.T) {
	for _, mobile := range []bool{false, true} {
		for _, tc := range []struct{ city, label, desktop string }{
			{"", "\u202eevil", "\u202eevil"},
			{"Tokyo", "\u202eevil", "\u202eevil"},
			{"", "", "relay relay_ab12"},
			{"Tokyo", "", "relay relay_ab12"},
		} {
			s := New()
			s.SetMode(ModeTUN) // No process-global proxy settings in this state projection test.
			if mobile {
				s.Mobile = &MobileHost{}
			}
			sink := &testSink{}
			s.Sink = sink
			r := usableRelay("relay_ab12", "JP", tc.city, "")
			r.Label = tc.label
			conn := &connection{}
			s.conn = conn
			if !s.promote(context.Background(), conn, &candidateResult{relay: r}, 0, false) {
				t.Fatal("promotion rejected")
			}
			state := sink.last()
			if mobile && tc.city == "" && strings.Contains(sink.logLines(), "connected via ") {
				t.Fatal("empty location left an incomplete connected log")
			}
			want := tc.desktop // Desktop deliberately keeps city-only fallback behavior.
			if mobile {
				want = tc.city
			}
			if mobile && want == "" {
				if state.RelayLabel != nil {
					t.Fatalf("missing mobile geo must emit null, got %q", *state.RelayLabel)
				}
			} else if state.RelayLabel == nil || *state.RelayLabel != want {
				t.Fatalf("mobile=%v label=%v want=%q", mobile, state.RelayLabel, want)
			}
			if len(state.Recents) != 1 || state.Recents[0].Label != want {
				t.Fatalf("recent = %+v", state.Recents)
			}
			if mobile && (state.Details.LocationLabel != want || state.Recents[0].RelayID != r.ID) {
				t.Fatalf("mobile metadata = %+v", state)
			}
		}
	}
}

// Exercise the supervisor, not just its gate: an expired WSS wait used to mint
// another budget after every failed ladder, unlike direct and punched recovery.
func TestMobileRecoveryExpiryTerminatesEveryTransport(t *testing.T) {
	for _, transport := range []string{"direct", "punch", accessTransportWSS} {
		for _, health := range []bool{false, true} {
			for _, down := range []bool{false, true} {
				t.Run(fmt.Sprintf("%s/health=%v/down=%v", transport, health, down), func(t *testing.T) {
					s := New()
					s.Mobile = &MobileHost{}
					sink := &testSink{}
					s.Sink = sink
					s.SetMode(ModeTUN)
					s.networkRecoveryLimit = 20 * time.Millisecond
					s.networkRetryDelay = time.Millisecond
					s.healthTick = time.Millisecond
					s.checkNetworkAlive = func(context.Context, []string) bool { return false }
					if down {
						s.UpdateNetworkState(NetworkState{Up: false, Fingerprint: "offline"})
					}
					s.healthProbe = func(context.Context, int) error {
						if health {
							return errors.New("remote tunnel lost")
						}
						return nil
					}
					var attempts int
					outage := errors.New("broker unavailable during outage")
					s.fetchRelays = func(context.Context, string, int, string, string) (discovery.Fetch, error) {
						attempts++
						return discovery.Fetch{}, outage
					}
					ctx, cancel := context.WithTimeout(context.Background(), time.Second)
					defer cancel()
					runCtx, stopRun := context.WithCancel(ctx)
					defer stopRun()
					runDone := make(chan error, 1)
					cur := &candidateResult{relay: usableRelay("relay-test", "JP", "Tokyo", "Japan"), accessTransport: transport,
						ctx: runCtx, cancel: stopRun, runDone: runDone, transportErr: make(chan error, 1)}
					if !health {
						if transport == accessTransportWSS {
							cur.transportErr <- errors.New("WSS socket died")
						} else {
							runDone <- errors.New("direct path died")
						}
					}
					conn := &connection{active: cur, netNotify: make(chan struct{}, 1)}
					stage, err := s.supervise(ctx, conn, cur, 0, RelayTarget{})
					if health && strings.Contains(sink.logLines(), "the tunnel stopped while the local network is down") {
						t.Fatal("supervisor started a second outage hold after health exhausted its budget")
					}
					if ctx.Err() != nil || stage != "failover_exhausted" || !errors.Is(err, outage) || attempts != 1 {
						t.Fatalf("recovery: stage=%q err=%v attempts=%d context=%v", stage, err, attempts, ctx.Err())
					}
				})
			}
		}
	}
}

func TestMobileResumeRenewsBothOutageBudgets(t *testing.T) {
	for _, health := range []bool{false, true} {
		t.Run(fmt.Sprintf("health=%v", health), func(t *testing.T) {
			s := New()
			s.Mobile = &MobileHost{}
			s.networkRecoveryLimit = 30 * time.Millisecond
			s.networkRetryDelay = time.Millisecond
			s.healthTick = time.Millisecond
			entered := make(chan struct{})
			var probes atomic.Int32
			s.checkNetworkAlive = func(context.Context, []string) bool {
				if probes.Add(1) == 1 {
					s.Pause()
					close(entered)
					return false
				}
				return true // The physical path returned during suspension.
			}
			ctx, cancel := context.WithTimeout(context.Background(), time.Second)
			defer cancel()
			done := make(chan bool, 1)
			go func() {
				if health {
					failed := make(chan error, 1)
					s.healthLoopWithProbe(ctx, 0, nil, failed, nil, func(context.Context, int) error { return errors.New("tunnel still dead") }, nil)
					select {
					case <-failed:
						done <- true
					default:
						done <- false
					}
				} else {
					done <- s.waitForNetworkRecovery(ctx, &connection{netNotify: make(chan struct{}, 1)}, s.newRecoveryBudget())
				}
			}()
			select {
			case <-entered:
			case <-ctx.Done():
				t.Fatal("never entered outage hold")
			}
			time.Sleep(60 * time.Millisecond) // Exceed the old deadline while paused.
			s.Resume()
			if !<-done || probes.Load() < 2 {
				t.Fatalf("resume skipped physical liveness: probes=%d", probes.Load())
			}
		})
	}
}
