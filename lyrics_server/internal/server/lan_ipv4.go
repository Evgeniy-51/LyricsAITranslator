//go:build !windows

package server

func primaryLanIPv4(ips []string) string {
	return fallbackLanIPv4(ips)
}
