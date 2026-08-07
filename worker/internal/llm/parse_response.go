package llm

import (
	"encoding/json"
	"fmt"
	"sort"
	"strings"

	"lyrics-plugin/worker/internal/cache"
)

type flexLine struct {
	Index int
	Text  string
}

func stripMarkdownJSON(s string) string {
	s = strings.TrimSpace(s)
	if strings.HasPrefix(s, "```") {
		if idx := strings.Index(s, "\n"); idx >= 0 {
			s = s[idx+1:]
		}
		if end := strings.LastIndex(s, "```"); end >= 0 {
			s = s[:end]
		}
	}
	return strings.TrimSpace(s)
}

func extractJSONObject(s string) string {
	s = stripMarkdownJSON(s)
	start := strings.Index(s, "{")
	end := strings.LastIndex(s, "}")
	if start >= 0 && end > start {
		return s[start : end+1]
	}
	return s
}

func parseTranslationPayload(content string) (already bool, lines []flexLine, err error) {
	raw := extractJSONObject(content)
	if raw == "" {
		return false, nil, fmt.Errorf("LLM: empty content")
	}

	var direct responsePayload
	if err := json.Unmarshal([]byte(raw), &direct); err == nil {
		if direct.AlreadyInTargetLanguage {
			return true, nil, nil
		}
		for _, l := range direct.Lines {
			lines = append(lines, flexLine{Index: l.Index, Text: strings.TrimSpace(l.Translation)})
		}
		if len(lines) > 0 {
			return false, lines, nil
		}
	}

	var root map[string]json.RawMessage
	if err := json.Unmarshal([]byte(raw), &root); err != nil {
		return false, nil, fmt.Errorf("LLM invalid JSON content: %w", err)
	}

	if v, ok := root["alreadyInTargetLanguage"]; ok {
		var flag bool
		if json.Unmarshal(v, &flag) == nil && flag {
			return true, nil, nil
		}
	}

	for _, key := range []string{"lines", "translations", "items", "data"} {
		if v, ok := root[key]; ok {
			if parsed := decodeFlexLines(v); len(parsed) > 0 {
				return false, parsed, nil
			}
		}
	}

	return false, nil, fmt.Errorf("LLM: no translation lines")
}

func decodeFlexLines(raw json.RawMessage) []flexLine {
	var arr []map[string]json.RawMessage
	if err := json.Unmarshal(raw, &arr); err != nil {
		return nil
	}
	var out []flexLine
	for _, obj := range arr {
		idx := intFromField(obj, "index", "line", "lineIndex", "i", "id")
		text := stringFromField(obj, "translation", "text", "t", "line", "content", "value")
		if text == "" {
			continue
		}
		out = append(out, flexLine{Index: idx, Text: text})
	}
	return out
}

func intFromField(obj map[string]json.RawMessage, keys ...string) int {
	for _, k := range keys {
		v, ok := obj[k]
		if !ok {
			continue
		}
		var i int
		if json.Unmarshal(v, &i) == nil {
			return i
		}
		var f float64
		if json.Unmarshal(v, &f) == nil {
			return int(f)
		}
	}
	return 0
}

func stringFromField(obj map[string]json.RawMessage, keys ...string) string {
	for _, k := range keys {
		v, ok := obj[k]
		if !ok {
			continue
		}
		var s string
		if json.Unmarshal(v, &s) == nil {
			return strings.TrimSpace(s)
		}
	}
	return ""
}

func wantedIndices(lines []cache.Line) []int {
	var want []int
	for _, l := range lines {
		if strings.TrimSpace(l.Original) != "" {
			want = append(want, l.Index)
		}
	}
	return want
}

func resolveTranslations(lines []cache.Line, flex []flexLine) (map[int]string, error) {
	want := wantedIndices(lines)
	if len(want) == 0 {
		return map[int]string{}, nil
	}
	if len(flex) == 0 {
		return nil, fmt.Errorf("missing translation for index %d", want[0])
	}

	try := func(m map[int]string) bool {
		for _, idx := range want {
			if strings.TrimSpace(m[idx]) == "" {
				return false
			}
		}
		return true
	}

	byIndex := map[int]string{}
	for _, fl := range flex {
		if fl.Text != "" {
			byIndex[fl.Index] = fl.Text
		}
	}
	if try(byIndex) {
		return byIndex, nil
	}

	// 1-based indices (1..N instead of 0..N-1)
	oneBased := map[int]string{}
	for _, fl := range flex {
		if fl.Text != "" {
			oneBased[fl.Index-1] = fl.Text
		}
	}
	if try(oneBased) {
		return oneBased, nil
	}

	// Positional: same count, assign in sorted index order to wanted lines.
	if len(flex) >= len(want) {
		sorted := append([]flexLine(nil), flex...)
		sort.Slice(sorted, func(i, j int) bool {
			if sorted[i].Index != sorted[j].Index {
				return sorted[i].Index < sorted[j].Index
			}
			return i < j
		})
		pos := map[int]string{}
		for i, idx := range want {
			if i < len(sorted) && sorted[i].Text != "" {
				pos[idx] = sorted[i].Text
			}
		}
		if try(pos) {
			return pos, nil
		}
	}

	for _, idx := range want {
		if strings.TrimSpace(byIndex[idx]) == "" {
			return nil, fmt.Errorf("missing translation for index %d", idx)
		}
	}
	return byIndex, nil
}

func buildResult(lines []cache.Line, content string) (*Result, error) {
	already, flex, err := parseTranslationPayload(content)
	if err != nil {
		return nil, err
	}
	if already {
		return &Result{AlreadyInTargetLanguage: true, Translations: map[int]string{}}, nil
	}
	trans, err := resolveTranslations(lines, flex)
	if err != nil {
		return nil, err
	}
	return &Result{AlreadyInTargetLanguage: false, Translations: trans}, nil
}
