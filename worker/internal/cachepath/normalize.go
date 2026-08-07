package cachepath

import (
	"os"
	"path/filepath"
	"regexp"
	"strings"
)

const maxMetadataPasses = 8

var (
	spaceRe         = regexp.MustCompile(`\s+`)
	featRe          = regexp.MustCompile(`(?i)\s+(?:feat\.?|featuring|ft\.?)\s+.+$`)
	bracketSuffixRe = regexp.MustCompile(
		`(?i)\s*[\(\[]\s*(?:\d{4}\s+)?(?:disc\s*\d+|cd\s*\d+|disk\s*\d+|remaster(?:ed)?|deluxe|expanded|bonus(?:\s+tracks?)?|remix|radio edit|single version|album version|mono|stereo|live|explicit|clean)[^\)\]]*[\)\]]\s*$`,
	)
	bracketPrefixRe = regexp.MustCompile(
		`(?i)^\s*(?:[\(\[]\s*)?(?:disc\s*\d+|cd\s*\d+|disk\s*\d+)(?:\s*[\)\]])?\s*[-:–—]?\s*`,
	)
	invalidPathChars = strings.NewReplacer(
		`<`, "_", `>`, "_", `:`, "_", `"`, "_", `/`, "_", `\`, "_", `|`, "_",
		`?`, "_", `*`, "_",
	)
)

// Path is the canonical on-disk path for new cache files (normalized layout).
func Path(cacheDir, artist, album, title string) string {
	return filepath.Join(
		cacheDir,
		SanitizeArtist(artist),
		SanitizeAlbum(album),
		SanitizeTitle(title)+".json",
	)
}

// PathLegacy is the pre-normalization layout (trim + illegal path chars only).
func PathLegacy(cacheDir, artist, album, title string) string {
	return filepath.Join(
		cacheDir,
		SanitizeSegmentLegacy(artist),
		SanitizeSegmentLegacy(album),
		SanitizeSegmentLegacy(title)+".json",
	)
}

// ResolvePath returns normalized path if present, otherwise legacy path if present,
// otherwise normalized (target for new writes).
func ResolvePath(cacheDir, artist, album, title string) string {
	norm := Path(cacheDir, artist, album, title)
	if fileExists(norm) {
		return norm
	}
	leg := PathLegacy(cacheDir, artist, album, title)
	if fileExists(leg) {
		return leg
	}
	return norm
}

func SanitizeArtist(s string) string {
	return finalizeSegment(s, segmentOpts{primaryArtist: true, cleanMetadata: true, lowercase: true})
}

func SanitizeAlbum(s string) string {
	return finalizeSegment(s, segmentOpts{cleanMetadata: true, lowercase: true})
}

func SanitizeTitle(s string) string {
	return finalizeSegment(s, segmentOpts{cleanMetadata: true, lowercase: true})
}

// SanitizeSegmentLegacy — step 0 only (compatibility).
func SanitizeSegmentLegacy(s string) string {
	s = strings.TrimSpace(s)
	if s == "" {
		return "_unknown"
	}
	s = invalidPathChars.Replace(s)
	s = strings.TrimSpace(s)
	if s == "" || s == "." || s == ".." {
		return "_"
	}
	return s
}

type segmentOpts struct {
	primaryArtist bool
	cleanMetadata bool
	lowercase     bool
}

func finalizeSegment(s string, opts segmentOpts) string {
	s = compactSpaces(s)
	if opts.cleanMetadata {
		s = cleanPathMetadata(s)
	}
	if opts.primaryArtist {
		s = primaryArtist(s)
	}
	s = compactSpaces(s)
	if s == "" {
		return "_unknown"
	}
	s = invalidPathChars.Replace(s)
	s = compactSpaces(s)
	if opts.lowercase {
		s = strings.ToLower(s)
	}
	s = strings.TrimSpace(s)
	if s == "" || s == "." || s == ".." {
		return "_"
	}
	return s
}

func cleanPathMetadata(s string) string {
	s = compactSpaces(s)
	for pass := 0; pass < maxMetadataPasses; pass++ {
		next := bracketSuffixRe.ReplaceAllString(s, "")
		next = bracketPrefixRe.ReplaceAllString(next, "")
		next = compactSpaces(featRe.ReplaceAllString(next, ""))
		if next == s {
			return compactSpaces(next)
		}
		s = next
	}
	return compactSpaces(s)
}

func primaryArtist(s string) string {
	s = compactSpaces(s)
	for _, sep := range []string{";", " / ", " & "} {
		if i := strings.Index(s, sep); i >= 0 {
			return compactSpaces(s[:i])
		}
	}
	return s
}

func compactSpaces(s string) string {
	return spaceRe.ReplaceAllString(strings.TrimSpace(s), " ")
}

func fileExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}
