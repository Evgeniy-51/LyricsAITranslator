package lyrics

import (
	"regexp"
	"strconv"
	"strings"
)

type Line struct {
	Index  int
	TimeMs int
	Text   string
}

var lrcTimeRe = regexp.MustCompile(`^\[(\d+):(\d+(?:\.\d+)?)\]\s*(.*)$`)

func ParseSynced(synced string) []Line {
	var out []Line
	for _, raw := range strings.Split(synced, "\n") {
		raw = strings.TrimSpace(raw)
		if raw == "" {
			continue
		}
		m := lrcTimeRe.FindStringSubmatch(raw)
		if m == nil {
			continue
		}
		minutes, _ := strconv.Atoi(m[1])
		seconds, _ := strconv.ParseFloat(m[2], 64)
		text := strings.TrimSpace(m[3])
		timeMs := int(float64(minutes*60)*1000 + seconds*1000)
		out = append(out, Line{
			Index:  len(out),
			TimeMs: timeMs,
			Text:   text,
		})
	}
	return out
}

func ParsePlain(plain string) []Line {
	var out []Line
	for _, raw := range strings.Split(plain, "\n") {
		text := strings.TrimSpace(raw)
		if text == "" {
			continue
		}
		out = append(out, Line{
			Index: len(out),
			Text:  text,
		})
	}
	return out
}
