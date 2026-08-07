package llm

import (
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"strings"
)

// UserError is shown in the lyrics window (English).
type UserError struct {
	Message string
}

func (e *UserError) Error() string { return e.Message }

func AsUserError(err error) *UserError {
	var ue *UserError
	if errors.As(err, &ue) {
		return ue
	}
	return nil
}

func userErr(msg string) error {
	return &UserError{Message: msg}
}

type apiErrorBody struct {
	Error *struct {
		Message string `json:"message"`
		Type    string `json:"type"`
		Code    string `json:"code"`
	} `json:"error"`
}

func messageFromHTTP(status int, body string) string {
	lower := strings.ToLower(body)
	var parsed apiErrorBody
	_ = json.Unmarshal([]byte(body), &parsed)
	apiMsg := ""
	apiType := ""
	if parsed.Error != nil {
		apiMsg = strings.TrimSpace(parsed.Error.Message)
		apiType = strings.ToLower(strings.TrimSpace(parsed.Error.Type))
		if parsed.Error.Code != "" {
			apiType = strings.ToLower(parsed.Error.Code)
		}
	}

	switch status {
	case 401, 403:
		return "Invalid or expired API key. Check llm.apiKey in config.json."
	case 402:
		return "Payment required or insufficient balance on your LLM account."
	case 429:
		if strings.Contains(apiType, "insufficient_quota") || strings.Contains(lower, "insufficient_quota") ||
			strings.Contains(lower, "quota") || strings.Contains(lower, "billing") {
			return "LLM quota exceeded or out of credits. Check your provider account."
		}
		return "Too many requests or rate limit exceeded. Try again later."
	case 500, 502, 503, 504:
		return "Translation service is temporarily unavailable. Try again later."
	}

	if strings.Contains(apiType, "invalid_api_key") || strings.Contains(lower, "invalid_api_key") {
		return "Invalid API key. Check llm.apiKey in config.json."
	}
	if strings.Contains(apiType, "insufficient_quota") || strings.Contains(lower, "insufficient_quota") {
		return "LLM quota exceeded or out of credits. Check your provider account."
	}
	if apiMsg != "" {
		return fmt.Sprintf("Translation service error: %s", truncate(apiMsg, 200))
	}
	return fmt.Sprintf("Translation service returned HTTP %d.", status)
}

func classifyHTTP(status int, body string) error {
	return userErr(messageFromHTTP(status, body))
}

func classifyRequestError(err error) error {
	if err == nil {
		return nil
	}
	var netErr net.Error
	if errors.As(err, &netErr) {
		if netErr.Timeout() {
			return userErr("Translation request timed out. Check proxy and network.")
		}
	}
	msg := strings.ToLower(err.Error())
	if strings.Contains(msg, "connection refused") || strings.Contains(msg, "no such host") {
		return userErr("Cannot reach the translation API. Check llm.baseUrl and proxy settings.")
	}
	return userErr("Network error while contacting the translation service.")
}
