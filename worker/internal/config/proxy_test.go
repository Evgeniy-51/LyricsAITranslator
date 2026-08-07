package config

import "testing"

func TestProxyHostPort_hostAndPort(t *testing.T) {
	p := Proxy{Enabled: true, URL: "200.10.40.184", Port: "9209"}
	addr, err := p.HostPort()
	if err != nil {
		t.Fatal(err)
	}
	if addr != "200.10.40.184:9209" {
		t.Fatalf("got %q", addr)
	}
}

func TestProxyHostPort_hostPortInURL(t *testing.T) {
	p := Proxy{Enabled: true, URL: "127.0.0.1:1080"}
	addr, err := p.HostPort()
	if err != nil {
		t.Fatal(err)
	}
	if addr != "127.0.0.1:1080" {
		t.Fatalf("got %q", addr)
	}
}

func TestProxyHostPort_fullURL(t *testing.T) {
	p := Proxy{Enabled: true, URL: "socks5h://127.0.0.1:1080"}
	addr, err := p.HostPort()
	if err != nil {
		t.Fatal(err)
	}
	if addr != "127.0.0.1:1080" {
		t.Fatalf("got %q", addr)
	}
}

func TestProxyHostPort_missingPort(t *testing.T) {
	p := Proxy{Enabled: true, URL: "10.0.0.1"}
	_, err := p.HostPort()
	if err == nil {
		t.Fatal("expected error")
	}
}

func TestNormalizedType_defaultSocks5(t *testing.T) {
	p := Proxy{}
	if p.NormalizedType() != ProxyTypeSOCKS5 {
		t.Fatalf("got %q", p.NormalizedType())
	}
}

func TestNormalizedType_inferHTTPFromURL(t *testing.T) {
	p := Proxy{URL: "http://127.0.0.1:8080"}
	if p.NormalizedType() != ProxyTypeHTTP {
		t.Fatalf("got %q", p.NormalizedType())
	}
}

func TestHTTPProxyURL_withAuth(t *testing.T) {
	p := Proxy{Enabled: true, Type: "http", URL: "10.0.0.1", Port: "8080", User: "u", Password: "p"}
	u, err := p.HTTPProxyURL()
	if err != nil {
		t.Fatal(err)
	}
	if u.Scheme != "http" || u.Host != "10.0.0.1:8080" {
		t.Fatalf("got %s", u.String())
	}
	if u.User.Username() != "u" {
		t.Fatalf("user %q", u.User.Username())
	}
}
