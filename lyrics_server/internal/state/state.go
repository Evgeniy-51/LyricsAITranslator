package state

import (
	"sync"
	"time"
)

type Track struct {
	Artist      string  `json:"artist"`
	Album       string  `json:"album"`
	Title       string  `json:"title"`
	DurationSec float64 `json:"durationSec"`
}

type Playback struct {
	IsPlaying   bool    `json:"isPlaying"`
	IsPaused    bool    `json:"isPaused"`
	PositionSec float64 `json:"positionSec"`
}

type Line struct {
	Index       int    `json:"index"`
	TimeMs      int    `json:"timeMs,omitempty"`
	Original    string `json:"original"`
	Translation string `json:"translation,omitempty"`
}

type Lyrics struct {
	HasSync          bool   `json:"hasSync"`
	HighlightEnabled bool   `json:"highlightEnabled"`
	ActiveLine       int    `json:"activeLine"`
	Lines            []Line `json:"lines"`
}

type Snapshot struct {
	Track           Track    `json:"track"`
	Playback        Playback `json:"playback"`
	Lyrics          Lyrics   `json:"lyrics"`
	Status          string   `json:"status"`
	UpdatedAtUnixMs int64    `json:"updatedAtUnixMs"`
	Message         string   `json:"message,omitempty"`
}

type Store struct {
	mu                   sync.RWMutex
	current              Snapshot
	hasData              bool
	subs                 map[chan Snapshot]struct{}
	highlightPending     bool
	highlightPendingVal  bool
	playerCommandPending string
}

func NewStore() *Store {
	return &Store{subs: make(map[chan Snapshot]struct{})}
}

func (s *Store) Update(snap Snapshot) {
	s.mu.Lock()
	snap.UpdatedAtUnixMs = time.Now().UnixMilli()
	s.current = snap
	s.hasData = true
	s.highlightPending = false
	subs := make([]chan Snapshot, 0, len(s.subs))
	for ch := range s.subs {
		subs = append(subs, ch)
	}
	s.mu.Unlock()
	for _, ch := range subs {
		select {
		case ch <- snap:
		default:
		}
	}
}

func (s *Store) Get() (Snapshot, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.current, s.hasData
}

func (s *Store) Subscribe(buf int) chan Snapshot {
	ch := make(chan Snapshot, buf)
	s.mu.Lock()
	s.subs[ch] = struct{}{}
	s.mu.Unlock()
	return ch
}

func (s *Store) Unsubscribe(ch chan Snapshot) {
	s.mu.Lock()
	delete(s.subs, ch)
	s.mu.Unlock()
	close(ch)
}

func (s *Store) SetHighlightFromWeb(enabled bool) Snapshot {
	s.mu.Lock()
	s.highlightPending = true
	s.highlightPendingVal = enabled
	s.current.Lyrics.HighlightEnabled = enabled
	s.current.UpdatedAtUnixMs = time.Now().UnixMilli()
	s.hasData = true
	snap := s.current
	subs := make([]chan Snapshot, 0, len(s.subs))
	for ch := range s.subs {
		subs = append(subs, ch)
	}
	s.mu.Unlock()
	for _, ch := range subs {
		select {
		case ch <- snap:
		default:
		}
	}
	return snap
}

func (s *Store) ConsumeHighlightPending() (pending bool, enabled bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if !s.highlightPending {
		return false, false
	}
	s.highlightPending = false
	return true, s.highlightPendingVal
}

func (s *Store) SetPlayerCommandFromWeb(command string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.playerCommandPending = command
}

func (s *Store) ConsumePlayerCommand() (pending bool, command string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.playerCommandPending == "" {
		return false, ""
	}
	command = s.playerCommandPending
	s.playerCommandPending = ""
	return true, command
}
