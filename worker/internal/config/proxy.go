package config

import (
	"fmt"
	"net"
	"net/url"
	"strings"
)

const (
	ProxyTypeSOCKS5 = "socks5"
	ProxyTypeHTTP   = "http"
)

// NormalizedType returns socks5 or http. Empty/unknown defaults to socks5 unless URL scheme is http(s).
func (p Proxy) NormalizedType() string {
	t := strings.ToLower(strings.TrimSpace(p.Type))
	switch t {
	case ProxyTypeHTTP, "https":
		return ProxyTypeHTTP
	case ProxyTypeSOCKS5, "socks", "socks5h":
		return ProxyTypeSOCKS5
	case "":
		raw := strings.ToLower(strings.TrimSpace(p.URL))
		if strings.HasPrefix(raw, "http://") || strings.HasPrefix(raw, "https://") {
			return ProxyTypeHTTP
		}
		return ProxyTypeSOCKS5
	default:
		return t
	}
}

// HostPort returns host:port for dialing the proxy.
func (p Proxy) HostPort() (string, error) {
	if !p.Enabled {
		return "", fmt.Errorf("proxy is disabled")
	}
	raw := strings.TrimSpace(p.URL)
	if raw == "" {
		return "", fmt.Errorf("proxy.url is empty")
	}

	if strings.Contains(raw, "://") {
		u, err := url.Parse(raw)
		if err != nil {
			return "", fmt.Errorf("parse proxy url: %w", err)
		}
		if h := strings.TrimSpace(u.Host); h != "" {
			return h, nil
		}
		return "", fmt.Errorf("proxy url has no host")
	}

	if strings.Contains(raw, ":") {
		host, port, err := net.SplitHostPort(raw)
		if err == nil && host != "" && port != "" {
			return net.JoinHostPort(host, port), nil
		}
	}

	port := strings.TrimSpace(p.Port)
	if port == "" {
		return "", fmt.Errorf("proxy.port is required when proxy.url is host only")
	}
	return net.JoinHostPort(raw, port), nil
}

// SOCKS5Address is an alias of HostPort (legacy name used by older call sites/tests).
func (p Proxy) SOCKS5Address() (string, error) {
	return p.HostPort()
}

// HTTPProxyURL builds an http/https proxy URL for http.Transport.Proxy.
func (p Proxy) HTTPProxyURL() (*url.URL, error) {
	host, err := p.HostPort()
	if err != nil {
		return nil, err
	}
	scheme := "http"
	raw := strings.ToLower(strings.TrimSpace(p.URL))
	if strings.HasPrefix(raw, "https://") {
		scheme = "https"
	}
	u := &url.URL{Scheme: scheme, Host: host}
	if p.User != "" || p.Password != "" {
		u.User = url.UserPassword(p.User, p.Password)
	}
	return u, nil
}

func (p Proxy) validateProxy() error {
	if !p.Enabled {
		return nil
	}
	t := p.NormalizedType()
	if t != ProxyTypeSOCKS5 && t != ProxyTypeHTTP {
		return fmt.Errorf("proxy.type must be socks5 or http, got %q", p.Type)
	}
	if _, err := p.HostPort(); err != nil {
		return err
	}
	return nil
}
