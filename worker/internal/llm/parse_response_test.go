package llm

import (
	"testing"

	"lyrics-plugin/worker/internal/cache"
)

func TestParseTranslationPayload_markdown(t *testing.T) {
	raw := "```json\n{\"alreadyInTargetLanguage\":false,\"lines\":[{\"index\":0,\"translation\":\"a\"},{\"index\":1,\"translation\":\"b\"}]}\n```"
	already, lines, err := parseTranslationPayload(raw)
	if err != nil || already || len(lines) != 2 {
		t.Fatalf("parse: already=%v lines=%d err=%v", already, len(lines), err)
	}
}

func TestResolveTranslations_oneBased(t *testing.T) {
	lyrics := []cache.Line{
		{Index: 0, Original: "one"},
		{Index: 1, Original: "two"},
	}
	flex := []flexLine{
		{Index: 1, Text: "один"},
		{Index: 2, Text: "два"},
	}
	m, err := resolveTranslations(lyrics, flex)
	if err != nil {
		t.Fatal(err)
	}
	if m[0] != "один" || m[1] != "два" {
		t.Fatalf("got %#v", m)
	}
}

func TestResolveTranslations_positional(t *testing.T) {
	lyrics := []cache.Line{
		{Index: 0, Original: "a"},
		{Index: 1, Original: "b"},
		{Index: 2, Original: "c"},
	}
	flex := []flexLine{
		{Index: 99, Text: "x"},
		{Index: 100, Text: "y"},
		{Index: 101, Text: "z"},
	}
	m, err := resolveTranslations(lyrics, flex)
	if err != nil {
		t.Fatal(err)
	}
	if m[2] != "z" {
		t.Fatalf("got %#v", m)
	}
}
