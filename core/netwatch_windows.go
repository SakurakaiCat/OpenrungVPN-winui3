//go:build windows

package main

import (
	"fmt"
	"net"
	"syscall"
	"unsafe"

	"golang.org/x/sys/windows"
)

func init() { gatherPhysicalAdapters = windowsPhysicalAdapters }

// windowsPhysicalAdapters projects one GetAdaptersAddresses snapshot into
// the monitor's netAdapter set: the only Windows API surface netwatch.go
// needs. GetAdaptersAddresses is a single IP Helper call (no handles, no
// callbacks), so the one-per-second poll in netwatch.go stays negligible;
// the event-driven NotifyRouteChange2 alternative would drag a C callback
// into a Go goroutine for nothing a one-second poll cannot see.
func windowsPhysicalAdapters() ([]netAdapter, error) {
	const flags = windows.GAA_FLAG_SKIP_ANYCAST | windows.GAA_FLAG_SKIP_MULTICAST |
		windows.GAA_FLAG_SKIP_DNS_SERVER | windows.GAA_FLAG_INCLUDE_GATEWAYS

	size := uint32(16 * 1024)
	var adapters []netAdapter
	for attempt := 0; attempt < 2; attempt++ {
		buf := make([]byte, size)
		aa := (*windows.IpAdapterAddresses)(unsafe.Pointer(&buf[0]))
		err := windows.GetAdaptersAddresses(windows.AF_UNSPEC, flags, 0, aa, &size)
		if err == windows.ERROR_BUFFER_OVERFLOW && attempt == 0 {
			continue // size now carries the required length; retry with it
		}
		if err == windows.ERROR_NO_DATA {
			return nil, nil // no adapters at all
		}
		if err != nil {
			return nil, err
		}
		for ; aa != nil; aa = aa.Next {
			a := netAdapter{
				name:   windows.UTF16PtrToString(aa.FriendlyName),
				desc:   windows.UTF16PtrToString(aa.Description),
				ifType: aa.IfType,
				up:     aa.OperStatus == windows.IfOperStatusUp,
				metric: adapterMetric(aa),
			}
			for ua := aa.FirstUnicastAddress; ua != nil; ua = ua.Next {
				if ip := sockaddrIP(ua.Address.Sockaddr); ip != "" {
					a.addrs = append(a.addrs, ip)
				}
			}
			for ga := aa.FirstGatewayAddress; ga != nil; ga = ga.Next {
				if ip := sockaddrIP(ga.Address.Sockaddr); ip != "" {
					a.gateways = append(a.gateways, ip)
				}
			}
			adapters = append(adapters, a)
		}
		return adapters, nil
	}
	return nil, fmt.Errorf("GetAdaptersAddresses: buffer size kept growing past %d bytes", size)
}

// adapterMetric returns the interface metric for best-path ranking, IPv4
// first and IPv6 as the fallback for IPv6-only links. The unset sentinel
// (0xFFFFFFFF) is treated as unknown.
func adapterMetric(aa *windows.IpAdapterAddresses) uint32 {
	if aa.Ipv4Metric != 0 && aa.Ipv4Metric != 0xFFFFFFFF {
		return aa.Ipv4Metric
	}
	return aa.Ipv6Metric
}

// sockaddrIP renders a raw socket address as an IP string, zone included
// for scoped IPv6. GetAdaptersAddresses hands back raw bytes (the struct's
// Sockaddr field), so the family dispatch below is the whole parse.
func sockaddrIP(sa *syscall.RawSockaddrAny) string {
	switch sa.Addr.Family {
	case syscall.AF_INET:
		a := (*syscall.RawSockaddrInet4)(unsafe.Pointer(sa))
		return net.IP(a.Addr[:]).String()
	case syscall.AF_INET6:
		a := (*syscall.RawSockaddrInet6)(unsafe.Pointer(sa))
		ip := net.IP(a.Addr[:]).String()
		if a.Scope_id != 0 {
			return fmt.Sprintf("%s%%%d", ip, a.Scope_id)
		}
		return ip
	}
	return ""
}
