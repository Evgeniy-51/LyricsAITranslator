package cache

import (
	"encoding/json"
	"path/filepath"
	"testing"

	"lyrics-plugin/worker/internal/config"
)

func TestPath_includesAlbum(t *testing.T) {
	p := Path("temp", config.Track{Artist: "U2", Album: "Achtung Baby", Title: "One"})
	want := "temp/u2/achtung baby/one.json"
	got := filepath.ToSlash(p)
	if got != want {
		t.Fatalf("path=%q want %q", got, want)
	}
}

func TestApplyTranslations_noDuplicateOriginal(t *testing.T) {
	f := &File{
		Lyrics: []Line{{Index: 0, Original: "Hello"}},
	}
	f.ApplyTranslations(true, nil)
	if f.Lyrics[0].Translation != "" {
		t.Fatalf("translation=%q", f.Lyrics[0].Translation)
	}
	data, _ := json.Marshal(f.Lyrics[0])
	if string(data) != `{"index":0,"original":"Hello"}` {
		t.Fatalf("json=%s", data)
	}
}

func TestMarkReadyOriginalOnly(t *testing.T) {
	f := &File{Lyrics: []Line{{Index: 0, Original: "Hi", Translation: "Hi"}}}
	f.MarkReadyOriginalOnly()
	if f.Lyrics[0].Translation != "" || f.Status != "ready" {
		t.Fatal("expected empty translation")
	}
}
