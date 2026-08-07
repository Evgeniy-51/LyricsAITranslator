package cache

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"lyrics-plugin/worker/internal/cachepath"
	"lyrics-plugin/worker/internal/config"
	"lyrics-plugin/worker/internal/lyrics"
)

const SchemaVersion = 1

type Source struct {
	Provider  string `json:"provider"`
	ID        int64  `json:"id,omitempty"`
	FetchedAt string `json:"fetchedAt"`
}

type Line struct {
	Index       int    `json:"index"`
	TimeMs      int    `json:"timeMs,omitempty"`
	Original    string `json:"original"`
	Translation string `json:"translation,omitempty"`
}

type File struct {
	SchemaVersion           int      `json:"schemaVersion"`
	Status                  string   `json:"status"`
	Track                   config.Track `json:"track"`
	Source                  *Source  `json:"source,omitempty"`
	Lyrics                  []Line   `json:"lyrics"`
	AlreadyInTargetLanguage bool     `json:"alreadyInTargetLanguage,omitempty"`
	Errors                  []string `json:"errors"`
}

func Path(cacheDir string, track config.Track) string {
	return cachepath.Path(cacheDir, track.Artist, track.Album, track.Title)
}

func Load(path string) (*File, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var f File
	if err := json.Unmarshal(data, &f); err != nil {
		return nil, err
	}
	return &f, nil
}

func Save(path string, f *File) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	f.SchemaVersion = SchemaVersion
	data, err := json.MarshalIndent(f, "", "  ")
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

func NewFromLRCLib(track config.Track, providerID int64, parsed []lyrics.Line, status string) *File {
	lines := make([]Line, len(parsed))
	for i, l := range parsed {
		lines[i] = Line{
			Index:    l.Index,
			TimeMs:   l.TimeMs,
			Original: l.Text,
		}
	}
	var src *Source
	if providerID > 0 {
		src = &Source{
			Provider:  "lrclib",
			ID:        providerID,
			FetchedAt: time.Now().UTC().Format(time.RFC3339),
		}
	}
	return &File{
		Status: status,
		Track:  track,
		Source: src,
		Lyrics: lines,
		Errors: []string{},
	}
}

func (f *File) ApplyTranslations(already bool, translated map[int]string) {
	f.AlreadyInTargetLanguage = already
	for i := range f.Lyrics {
		if already {
			f.Lyrics[i].Translation = ""
			continue
		}
		idx := f.Lyrics[i].Index
		t, ok := translated[idx]
		if !ok {
			f.Lyrics[i].Translation = ""
			continue
		}
		t = strings.TrimSpace(t)
		if t == "" || t == strings.TrimSpace(f.Lyrics[i].Original) {
			f.Lyrics[i].Translation = ""
			continue
		}
		f.Lyrics[i].Translation = t
	}
	f.Status = "ready"
}

// MarkReadyOriginalOnly finishes without translation fields (no duplicate originals).
func (f *File) MarkReadyOriginalOnly() {
	f.AlreadyInTargetLanguage = false
	for i := range f.Lyrics {
		f.Lyrics[i].Translation = ""
	}
	f.Status = "ready"
}

func (f *File) RecordError(msg string) {
	f.Errors = append(f.Errors, msg)
	f.Status = "failed"
}

func IsReady(path string) (bool, *File, error) {
	f, err := Load(path)
	if err != nil {
		if os.IsNotExist(err) {
			return false, nil, nil
		}
		return false, nil, err
	}
	return f.Status == "ready", f, nil
}

func ValidateReady(f *File) error {
	if f == nil {
		return fmt.Errorf("cache file is nil")
	}
	if len(f.Lyrics) == 0 {
		return fmt.Errorf("cache has no lyrics lines")
	}
	return nil
}
