package httpclient

import (
	"context"
	"fmt"
	"net"
	"net/http"
	"time"

	"golang.org/x/net/proxy"

	"lyrics-plugin/worker/internal/config"
)

func New(cfg config.Proxy, timeout time.Duration) (*http.Client, error) {
	if !cfg.Enabled {
		return &http.Client{Timeout: timeout}, nil
	}

	switch cfg.NormalizedType() {
	case config.ProxyTypeHTTP:
		proxyURL, err := cfg.HTTPProxyURL()
		if err != nil {
			return nil, err
		}
		return &http.Client{
			Timeout: timeout,
			Transport: &http.Transport{
				Proxy: http.ProxyURL(proxyURL),
			},
		}, nil
	case config.ProxyTypeSOCKS5:
		return newSOCKS5Client(cfg, timeout)
	default:
		return nil, fmt.Errorf("unsupported proxy type %q", cfg.Type)
	}
}

func newSOCKS5Client(cfg config.Proxy, timeout time.Duration) (*http.Client, error) {
	host, err := cfg.HostPort()
	if err != nil {
		return nil, err
	}
	var auth *proxy.Auth
	if cfg.User != "" || cfg.Password != "" {
		auth = &proxy.Auth{User: cfg.User, Password: cfg.Password}
	}
	socksDialer, err := proxy.SOCKS5("tcp", host, auth, proxy.Direct)
	if err != nil {
		return nil, fmt.Errorf("socks5 dialer: %w", err)
	}
	dialTimeout := 20 * time.Second
	transport := &http.Transport{
		DialContext: func(ctx context.Context, network, addr string) (net.Conn, error) {
			if err := ctx.Err(); err != nil {
				return nil, err
			}
			type dialResult struct {
				conn net.Conn
				err  error
			}
			ch := make(chan dialResult, 1)
			go func() {
				conn, dialErr := socksDialer.Dial(network, addr)
				ch <- dialResult{conn, dialErr}
			}()
			timer := time.NewTimer(dialTimeout)
			defer timer.Stop()
			select {
			case <-ctx.Done():
				return nil, ctx.Err()
			case <-timer.C:
				return nil, fmt.Errorf("socks5 dial timeout")
			case r := <-ch:
				return r.conn, r.err
			}
		},
	}
	return &http.Client{Timeout: timeout, Transport: transport}, nil
}
