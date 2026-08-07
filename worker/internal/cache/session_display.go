package cache

import (
	"encoding/json"
	"os"
	"path/filepath"
)

const SessionDisplayFileName = "run-display.json"

type sessionDisplay struct {
	CachePath string `json:"cachePath"`
	Lyrics    []Line `json:"lyrics"`
}

// WriteSessionDisplay stores lyrics for the plugin UI only (not permanent cache).
func WriteSessionDisplay(dir, cachePath string, lines []Line) error {
	payload := sessionDisplay{CachePath: cachePath, Lyrics: lines}
	data, err := json.MarshalIndent(payload, "", "  ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	tmp := filepath.Join(dir, SessionDisplayFileName+".tmp")
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, filepath.Join(dir, SessionDisplayFileName))
}

func RemoveFile(path string) error {
	err := os.Remove(path)
	if os.IsNotExist(err) {
		return nil
	}
	return err
}
