package config

import "strings"

// UserMessageForLoadError returns an English message for config validation failures.
func UserMessageForLoadError(err error) string {
	if err == nil {
		return ""
	}
	msg := err.Error()
	switch {
	case strings.Contains(msg, "llm.apiKey"):
		return "Translation is enabled but llm.apiKey is missing in config.json."
	case strings.Contains(msg, "proxy"):
		return "Proxy settings are invalid. Check proxy section in config.json."
	case strings.Contains(msg, "track.album"):
		return "Album tag is required for this track."
	case strings.Contains(msg, "track.artist"), strings.Contains(msg, "track.title"):
		return "Track metadata is incomplete."
	default:
		return "Configuration error: " + msg
	}
}
