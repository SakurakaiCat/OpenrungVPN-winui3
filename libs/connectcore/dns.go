package connectcore

import (
	"errors"
	"net"
)

// maxTunnelDNSServers caps the user-configured resolver list. Four is already
// far past any useful failover depth; a longer list would only bloat every
// candidate config the ladder generates.
const maxTunnelDNSServers = 4

// SetTunnelDNS stores the tunnel DNS configuration subsequent connects use:
// server IP literals for the DNS block (empty = the config builder's
// defaults, 1.1.1.1/8.8.8.8) and the IPv6 switch. Disabling IPv6 drops the
// TUN inbound's v6 address and pins the DNS strategy to ipv4_only so apps
// never learn AAAA addresses to dial — otherwise v6-destined traffic would
// bypass the tunnel on the physical network.
//
// Like SetMode it refuses while a connection is live — the running tunnel
// keeps the DNS it started with — under connectMu so the check cannot land
// in ConnectTarget's teardown-then-install window, where s.conn is
// momentarily nil even though a connect is already under way.
func (s *Engine) SetTunnelDNS(servers []string, ipv6 bool) error {
	if len(servers) > maxTunnelDNSServers {
		return errors.New("too many DNS servers")
	}
	for _, server := range servers {
		if net.ParseIP(server) == nil {
			return errors.New("DNS server " + server + " is not an IP literal; a hostname server has no bootstrap resolver")
		}
	}

	s.connectMu.Lock()
	defer s.connectMu.Unlock()

	s.mu.Lock()
	defer s.mu.Unlock()
	if s.conn != nil {
		return errors.New("disconnect before changing the tunnel DNS")
	}
	if len(servers) == 0 {
		s.tunnelDNSServers = nil
	} else {
		s.tunnelDNSServers = append([]string(nil), servers...)
	}
	s.tunnelDNSIPv6Disabled = !ipv6
	return nil
}

// TunnelDNS returns the tunnel DNS configuration subsequent connects use:
// the configured server list (nil = defaults) and the IPv6 switch.
func (s *Engine) TunnelDNS() (servers []string, ipv6 bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.tunnelDNSServers == nil {
		return nil, !s.tunnelDNSIPv6Disabled
	}
	return append([]string(nil), s.tunnelDNSServers...), !s.tunnelDNSIPv6Disabled
}
