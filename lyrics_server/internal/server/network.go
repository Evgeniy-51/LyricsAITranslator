package server

import (
	"fmt"
	"net/http"
	"net/url"
	"strings"

	"github.com/skip2/go-qrcode"
)

type infoResponse struct {
	URL  string `json:"url"`
	Host string `json:"host"`
	Port int    `json:"port"`
}

func fallbackLanIPv4(ips []string) string {
	if len(ips) == 0 {
		return ""
	}
	for _, ip := range ips {
		if strings.HasPrefix(ip, "192.168.") {
			return ip
		}
	}
	return ips[0]
}

func (s *Server) primaryLanURL() string {
	host := primaryLanIPv4(lanIPv4s())
	return buildWebURL(host, s.cfg.Port, s.cfg.AuthToken)
}
func buildWebURL(host string, port int, token string) string {
	if host == "" {
		host = "<this-pc-ip>"
	}
	u := fmt.Sprintf("http://%s:%d/", host, port)
	if token == "" {
		return u
	}
	return u + "?token=" + url.QueryEscape(token)
}

func (s *Server) handleInfo(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if !isLoopback(r.RemoteAddr) {
		http.Error(w, "forbidden", http.StatusForbidden)
		return
	}
	ips := lanIPv4s()
	host := primaryLanIPv4(ips)
	writeJSON(w, infoResponse{
		URL:  s.primaryLanURL(),
		Host: host,
		Port: s.cfg.Port,
	})
}

func (s *Server) handleQR(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if !isLoopback(r.RemoteAddr) {
		http.Error(w, "forbidden", http.StatusForbidden)
		return
	}
	target := s.primaryLanURL()
	if strings.Contains(target, "<this-pc-ip>") {
		http.Error(w, "no lan ip", http.StatusServiceUnavailable)
		return
	}
	png, err := qrcode.Encode(target, qrcode.Medium, 256)
	if err != nil {
		http.Error(w, "qr failed", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "image/png")
	w.Header().Set("Cache-Control", "no-cache")
	_, _ = w.Write(png)
}
