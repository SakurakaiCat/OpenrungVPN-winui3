package connectcore

import (
	"context"
	"net/http"
)

// mobileLivenessEndpoints returns a fresh default slice. Test engines may supply
// their own endpoints without mutating process-wide configuration.
func (s *Engine) mobileLivenessEndpoints() []string {
	if s.physicalLivenessURLs != nil {
		return s.physicalLivenessURLs
	}
	// Independent of OpenRung infrastructure and through-tunnel InternetProbeURLs.
	return []string{"https://www.gstatic.com/generate_204", "https://cp.cloudflare.com/generate_204"}
}

func (s *Engine) physicalNetworkAlive(ctx context.Context) bool {
	transport := protectedTransport(s.currentProtector(), s.currentDNSServers())
	transport.DisableKeepAlives = true
	defer transport.CloseIdleConnections()
	client := &http.Client{
		Transport:     transport,
		Timeout:       RelayTCPTimeout,
		CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse },
	}
	for _, endpoint := range s.mobileLivenessEndpoints() {
		req, err := http.NewRequestWithContext(ctx, http.MethodHead, endpoint, nil)
		if err != nil {
			return false
		}
		req.Header.Set("Cache-Control", "no-cache")
		req.Header.Set("User-Agent", "") // No application identity or broker headers.
		response, err := client.Do(req)
		if err == nil {
			response.Body.Close()
			// Any TLS-verified response from a neutral endpoint permits recovery.
			// It is not evidence of tunnel health; redirects are never followed.
			return true
		}
		if isSocketProtectionFailure(err) {
			// The gate cannot measure a refused socket. Let the ladder surface
			// the terminal local failure instead of waiting for internet forever.
			return true
		}
	}
	return false
}
