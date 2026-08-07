package cachepath

import (
	"path/filepath"
	"testing"
)

func TestStep0_LegacyPreservesCase(t *testing.T) {
	if got := SanitizeSegmentLegacy("NIRVANA"); got != "NIRVANA" {
		t.Fatalf("legacy case: got %q", got)
	}
}

func TestStep1_Lowercase(t *testing.T) {
	if got := SanitizeArtist("NIRVANA"); got != "nirvana" {
		t.Fatalf("got %q", got)
	}
}

func TestStep2_MetadataAlbum(t *testing.T) {
	cases := []struct{ in, want string }{
		{"Nevermind (Remastered)", "nevermind"},
		{"MTV Unplugged [Disc 2]", "mtv unplugged"},
		{"Disc 2 - In Utero", "in utero"},
	}
	for _, tc := range cases {
		got := SanitizeAlbum(tc.in)
		if got != tc.want {
			t.Fatalf("SanitizeAlbum(%q) = %q want %q", tc.in, got, tc.want)
		}
	}
}

func TestStep3_PrimaryArtist(t *testing.T) {
	if got := SanitizeArtist("Pearl Jam; Eddie Vedder"); got != "pearl jam" {
		t.Fatalf("got %q", got)
	}
}

func TestPathNormalizedLayout(t *testing.T) {
	got := Path("C:/cache", "NIRVANA", "Nevermind (Remaster)", "Smells Like Teen Spirit")
	want := filepath.Join("C:/cache", "nirvana", "nevermind", "smells like teen spirit.json")
	if filepath.ToSlash(filepath.Clean(got)) != filepath.ToSlash(filepath.Clean(want)) {
		t.Fatalf("got %q want %q", got, want)
	}
}

func TestPathLegacyDiffersFromNormalized(t *testing.T) {
	leg := PathLegacy("C:/cache", "NIRVANA", "Nevermind (Remaster)", "One")
	norm := Path("C:/cache", "NIRVANA", "Nevermind (Remaster)", "One")
	if leg == norm {
		t.Fatalf("legacy and normalized should differ")
	}
}
