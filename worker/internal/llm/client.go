package llm

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"

	"lyrics-plugin/worker/internal/cache"
	"lyrics-plugin/worker/internal/config"
)

type inputLine struct {
	Index int    `json:"index"`
	Text  string `json:"text"`
}

type responsePayload struct {
	AlreadyInTargetLanguage bool `json:"alreadyInTargetLanguage"`
	Lines                   []struct {
		Index       int    `json:"index"`
		Translation string `json:"translation"`
	} `json:"lines"`
}

type Result struct {
	AlreadyInTargetLanguage bool
	Translations            map[int]string
}

func Translate(client *http.Client, cfg *config.Config, lines []cache.Line) (*Result, error) {
	target := cfg.TargetLanguageLabel()
	var payload []inputLine
	for _, l := range lines {
		if strings.TrimSpace(l.Original) == "" {
			continue
		}
		payload = append(payload, inputLine{Index: l.Index, Text: l.Original})
	}
	if len(payload) == 0 {
		return &Result{AlreadyInTargetLanguage: true, Translations: map[int]string{}}, nil
	}

	userJSON, err := json.Marshal(payload)
	if err != nil {
		return nil, err
	}

	system := strings.TrimSpace(fmt.Sprintf(`You translate song lyrics for display under the original lines.
Target language: %s.

Rules:
- Return ONLY one JSON object. No markdown, no code fences, no text before or after.
- Output must parse with a strict JSON parser.
- Keep the same line indices as input; do not add or remove lines.
- Do not change timestamps; only provide translation text.
- Preserve meaning and singable phrasing where possible.

If the lyrics are already in the target language, return exactly:
{"alreadyInTargetLanguage":true}

Otherwise return exactly this shape (one entry per input line, same index values):
{"alreadyInTargetLanguage":false,"lines":[{"index":0,"translation":"first line"},{"index":2,"translation":"third line"}]}

Examples (follow structure exactly; replace text with your translations):
{"alreadyInTargetLanguage":true}
{"alreadyInTargetLanguage":false,"lines":[{"index":0,"translation":"First line"},{"index":1,"translation":"Second line"}]}`, target))

	user := fmt.Sprintf("Translate these lyric lines:\n%s", string(userJSON))

	reqBody := map[string]any{
		"model": cfg.LLM.Model,
		"messages": []map[string]string{
			{"role": "system", "content": system},
			{"role": "user", "content": user},
		},
		"response_format": map[string]string{"type": "json_object"},
		"temperature":     0.3,
	}
	body, err := json.Marshal(reqBody)
	if err != nil {
		return nil, err
	}

	endpoint := strings.TrimRight(strings.TrimSpace(cfg.LLM.BaseURL), "/") + "/chat/completions"
	req, err := http.NewRequest(http.MethodPost, endpoint, bytes.NewReader(body))
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+cfg.LLM.APIKey)

	resp, err := client.Do(req)
	if err != nil {
		return nil, classifyRequestError(err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, classifyRequestError(err)
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil, classifyHTTP(resp.StatusCode, string(respBody))
	}

	var chat struct {
		Choices []struct {
			Message struct {
				Content string `json:"content"`
			} `json:"message"`
		} `json:"choices"`
	}
	if err := json.Unmarshal(respBody, &chat); err != nil {
		return nil, fmt.Errorf("LLM decode response: %w", err)
	}
	if len(chat.Choices) == 0 {
		return nil, fmt.Errorf("LLM: empty choices")
	}

	content := strings.TrimSpace(chat.Choices[0].Message.Content)
	result, err := buildResult(lines, content)
	if err != nil {
		logSnippet := truncate(content, 400)
		return nil, fmt.Errorf("%w (response: %s)", err, logSnippet)
	}
	return result, nil
}

func truncate(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n] + "..."
}
