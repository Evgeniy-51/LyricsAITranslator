//go:build windows

package server

import (
	"net"
	"unsafe"

	"golang.org/x/sys/windows"
)

const gaFlagIncludeGateways = 0x0080

type unicastIPv4 struct {
	ip   net.IP
	mask net.IPMask
}

func primaryLanIPv4(ips []string) string {
	if ip := localIPv4OnDefaultGatewaySubnet(); ip != "" {
		return ip
	}
	return fallbackLanIPv4(ips)
}

// localIPv4OnDefaultGatewaySubnet returns the IPv4 on the same subnet as this
// adapter's default gateway (lowest route metric wins).
func localIPv4OnDefaultGatewaySubnet() string {
	size := uint32(15000)
	var buf []byte
	for {
		buf = make([]byte, size)
		err := windows.GetAdaptersAddresses(
			windows.AF_INET,
			gaFlagIncludeGateways,
			0,
			(*windows.IpAdapterAddresses)(unsafe.Pointer(&buf[0])),
			&size,
		)
		if err == windows.ERROR_BUFFER_OVERFLOW {
			continue
		}
		if err != nil {
			return ""
		}
		break
	}

	var bestIP string
	bestMetric := uint32(^uint32(0))

	for aa := (*windows.IpAdapterAddresses)(unsafe.Pointer(&buf[0])); aa != nil; aa = aa.Next {
		if aa.OperStatus != windows.IfOperStatusUp {
			continue
		}

		metric := aa.Ipv4Metric
		if metric == 0 {
			metric = 1
		}

		unicast := collectUnicastIPv4(aa)
		if len(unicast) == 0 {
			continue
		}

		for gw := aa.FirstGatewayAddress; gw != nil; gw = gw.Next {
			gwIP := socketAddressToIPv4(&gw.Address)
			if gwIP == nil || gwIP.IsUnspecified() {
				continue
			}

			for _, ua := range unicast {
				if !sameIPv4Subnet(ua.ip, gwIP, ua.mask) {
					continue
				}
				ipStr := ua.ip.String()
				if metric < bestMetric {
					bestMetric = metric
					bestIP = ipStr
				}
			}
		}
	}

	return bestIP
}

func collectUnicastIPv4(aa *windows.IpAdapterAddresses) []unicastIPv4 {
	var out []unicastIPv4
	for ua := aa.FirstUnicastAddress; ua != nil; ua = ua.Next {
		ip := socketAddressToIPv4(&ua.Address)
		if ip == nil || ip.IsLoopback() || ip.IsLinkLocalUnicast() {
			continue
		}
		mask := net.CIDRMask(int(ua.OnLinkPrefixLength), 32)
		if mask == nil {
			continue
		}
		out = append(out, unicastIPv4{ip: ip, mask: mask})
	}
	return out
}

func socketAddressToIPv4(sa *windows.SocketAddress) net.IP {
	if sa == nil {
		return nil
	}
	ip := sa.IP()
	if ip4 := ip.To4(); ip4 != nil {
		return ip4
	}
	return nil
}

func sameIPv4Subnet(a, b net.IP, mask net.IPMask) bool {
	a4 := a.To4()
	b4 := b.To4()
	if a4 == nil || b4 == nil || len(mask) != net.IPv4len {
		return false
	}
	for i := range mask {
		if (a4[i] & mask[i]) != (b4[i] & mask[i]) {
			return false
		}
	}
	return true
}
