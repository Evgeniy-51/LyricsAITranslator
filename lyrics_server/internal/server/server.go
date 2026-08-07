package server

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"strings"
	"time"

	"lyrics-plugin/lyrics_server/internal/state"
	"lyrics-plugin/lyrics_server/internal/webui"
)

type Config struct {
	Host      string
	Port      int
	AuthToken string
}

type Server struct {
	cfg   Config
	store *state.Store
	http  *http.Server
}

func New(cfg Config) *Server {
	return &Server{
		cfg:   cfg,
		store: state.NewStore(),
	}
}

func (s *Server) ListenAndServe() error {
	mux := http.NewServeMux()
	mux.HandleFunc("/", s.handleIndex)
	mux.HandleFunc("/health", s.handleHealth)
	mux.HandleFunc("/api/state", s.handleState)
	mux.HandleFunc("/api/highlight", s.handleHighlight)
	mux.HandleFunc("/api/player", s.handlePlayer)
	mux.HandleFunc("/api/info", s.handleInfo)
	mux.HandleFunc("/api/qr.png", s.handleQR)
	mux.HandleFunc("/events", s.handleEvents)

	addr := fmt.Sprintf("%s:%d", s.cfg.Host, s.cfg.Port)
	s.http = &http.Server{
		Addr:              addr,
		Handler:           mux,
		ReadHeaderTimeout: 5 * time.Second,
	}
	log.Printf("lyrics_server listening on http://%s", addr)
	for _, ip := range lanIPv4s() {
		log.Printf("Lyrics Web (LAN): http://%s:%d/", ip, s.cfg.Port)
	}
	if host := primaryLanIPv4(lanIPv4s()); host != "" {
		log.Printf("Lyrics Web (gateway subnet): http://%s:%d/", host, s.cfg.Port)
	}
	return s.http.ListenAndServe()
}

func (s *Server) handleHealth(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	_, _ = w.Write([]byte(`{"ok":true}`))
}

func (s *Server) handleIndex(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}
	if !s.checkBrowserAuth(r) {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_, _ = w.Write(webui.IndexHTML)
}

func (s *Server) handleState(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		if !s.checkBrowserAuth(r) {
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		snap, ok := s.store.Get()
		if !ok {
			writeJSON(w, state.Snapshot{Message: "No recent playback data"})
			return
		}
		writeJSON(w, snap)
	case http.MethodPost:
		if !isLoopback(r.RemoteAddr) {
			http.Error(w, "forbidden", http.StatusForbidden)
			return
		}
		body, err := io.ReadAll(io.LimitReader(r.Body, 4<<20))
		if err != nil {
			http.Error(w, "bad request", http.StatusBadRequest)
			return
		}
		var snap state.Snapshot
		if err := json.Unmarshal(body, &snap); err != nil {
			http.Error(w, "invalid json", http.StatusBadRequest)
			return
		}
		s.store.Update(snap)
		w.WriteHeader(http.StatusNoContent)
	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

type highlightRequest struct {
	Enabled bool `json:"enabled"`
}

type highlightPendingResponse struct {
	Pending bool `json:"pending"`
	Enabled bool `json:"enabled"`
}

func (s *Server) handleHighlight(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		if !isLoopback(r.RemoteAddr) {
			http.Error(w, "forbidden", http.StatusForbidden)
			return
		}
		pending, enabled := s.store.ConsumeHighlightPending()
		writeJSON(w, highlightPendingResponse{Pending: pending, Enabled: enabled})
	case http.MethodPost:
		if !s.checkBrowserAuth(r) {
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		body, err := io.ReadAll(io.LimitReader(r.Body, 4096))
		if err != nil {
			http.Error(w, "bad request", http.StatusBadRequest)
			return
		}
		var req highlightRequest
		if err := json.Unmarshal(body, &req); err != nil {
			http.Error(w, "invalid json", http.StatusBadRequest)
			return
		}
		snap, ok := s.store.Get()
		if ok && !snap.Lyrics.HasSync {
			http.Error(w, "no sync", http.StatusConflict)
			return
		}
		s.store.SetHighlightFromWeb(req.Enabled)
		w.WriteHeader(http.StatusNoContent)
	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

type playerCommandRequest struct {
	Command string `json:"command"`
}

type playerCommandPendingResponse struct {
	Pending bool   `json:"pending"`
	Command string `json:"command,omitempty"`
}

func validPlayerCommand(command string) bool {
	switch command {
	case "prev", "playPause", "next", "seekBack", "seekForward":
		return true
	default:
		return false
	}
}

func (s *Server) handlePlayer(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		if !isLoopback(r.RemoteAddr) {
			http.Error(w, "forbidden", http.StatusForbidden)
			return
		}
		pending, command := s.store.ConsumePlayerCommand()
		writeJSON(w, playerCommandPendingResponse{Pending: pending, Command: command})
	case http.MethodPost:
		if !s.checkBrowserAuth(r) {
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		body, err := io.ReadAll(io.LimitReader(r.Body, 4096))
		if err != nil {
			http.Error(w, "bad request", http.StatusBadRequest)
			return
		}
		var req playerCommandRequest
		if err := json.Unmarshal(body, &req); err != nil {
			http.Error(w, "invalid json", http.StatusBadRequest)
			return
		}
		if !validPlayerCommand(req.Command) {
			http.Error(w, "invalid command", http.StatusBadRequest)
			return
		}
		s.store.SetPlayerCommandFromWeb(req.Command)
		w.WriteHeader(http.StatusNoContent)
	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

func (s *Server) handleEvents(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if !s.checkBrowserAuth(r) {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}
	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "streaming unsupported", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")

	ch := s.store.Subscribe(4)
	defer s.store.Unsubscribe(ch)

	if snap, ok := s.store.Get(); ok {
		writeSSE(w, flusher, snap)
	}
	ticker := time.NewTicker(15 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-r.Context().Done():
			return
		case snap := <-ch:
			writeSSE(w, flusher, snap)
		case <-ticker.C:
			fmt.Fprintf(w, ": keepalive\n\n")
			flusher.Flush()
		}
	}
}

func writeSSE(w http.ResponseWriter, flusher http.Flusher, snap state.Snapshot) {
	data, _ := json.Marshal(snap)
	fmt.Fprintf(w, "data: %s\n\n", data)
	flusher.Flush()
}

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	enc := json.NewEncoder(w)
	enc.SetIndent("", "  ")
	_ = enc.Encode(v)
}

func (s *Server) checkBrowserAuth(r *http.Request) bool {
	if s.cfg.AuthToken == "" {
		return true
	}
	if r.URL.Query().Get("token") == s.cfg.AuthToken {
		return true
	}
	if c, err := r.Cookie("lyrics_token"); err == nil && c.Value == s.cfg.AuthToken {
		return true
	}
	return false
}

func isLoopback(remote string) bool {
	host, _, err := net.SplitHostPort(remote)
	if err != nil {
		host = remote
	}
	host = strings.Trim(host, "[]")
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}

func lanIPv4s() []string {
	var out []string
	ifaces, err := net.Interfaces()
	if err != nil {
		return out
	}
	for _, iface := range ifaces {
		if iface.Flags&net.FlagUp == 0 || iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}
		for _, a := range addrs {
			ipnet, ok := a.(*net.IPNet)
			if !ok || ipnet.IP.To4() == nil {
				continue
			}
			ip := ipnet.IP.To4().String()
			if strings.HasPrefix(ip, "169.254.") {
				continue
			}
			out = append(out, ip)
		}
	}
	return out
}
