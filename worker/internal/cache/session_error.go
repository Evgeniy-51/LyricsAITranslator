package cache

import (
	"encoding/json"
	"os"
	"path/filepath"
)

const SessionErrorFileName = "run-error.json"

type sessionError struct {
	CachePath   string `json:"cachePath"`
	UserMessage string `json:"userMessage"`
}

func WriteSessionError(dir, cachePath, userMessage string) error {
	payload := sessionError{CachePath: cachePath, UserMessage: userMessage}
	data, err := json.MarshalIndent(payload, "", "  ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	tmp := filepath.Join(dir, SessionErrorFileName+".tmp")
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, filepath.Join(dir, SessionErrorFileName))
}
