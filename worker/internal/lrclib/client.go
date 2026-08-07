package lrclib

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"

	"lyrics-plugin/worker/internal/config"
	"lyrics-plugin/worker/internal/lyrics"
)

const (
	getURL         = "https://lrclib.net/api/get"
	searchURL      = "https://lrclib.net/api/search"
	getTimeout     = 50 * time.Second
	searchTimeout  = 15 * time.Second
	maxGetAttempts = 2
)

var (
	bracketSuffixRe = regexp.MustCompile(`(?i)\s*[\(\[]\s*(?:\d{4}\s+)?(?:remaster(?:ed)?|remix|radio edit|single version|album version|mono|stereo|live|bonus track|explicit|clean)[^\)\]]*[\)\]]\s*$`)
	featRe          = regexp.MustCompile(`(?i)\s+(?:feat\.?|featuring|ft\.?)\s+.+$`)
	spaceRe         = regexp.MustCompile(`\s+`)
)

type Response struct {
	ID           int64   `json:"id"`
	TrackName    string  `json:"trackName"`
	ArtistName   string  `json:"artistName"`
	AlbumName    string  `json:"albumName"`
	Duration     float64 `json:"duration"`
	SyncedLyrics string  `json:"syncedLyrics"`
	PlainLyrics  string  `json:"plainLyrics"`
	Instrumental bool    `json:"instrumental"`
}

type Result struct {
	ProviderID int64
	AlbumName  string
	Duration   float64
	Lines      []lyrics.Line
}

func Fetch(client *http.Client, track config.Track) (*Result, error) {
	normalized := normalizeTrack(track)
	getAttempts := buildGetAttempts(track, normalized)
	var sawNotFound bool
	for _, tr := range getAttempts {
		res, err := fetchGet(client, tr)
		if err == nil {
			return res, nil
		}
		if isNotFound(err) {
			sawNotFound = true
			continue
		}
		return nil, err
	}

	searchTrack := getAttempts[0]
	res, err := fetchSearch(client, searchTrack)
	if err == nil {
		return res, nil
	}
	if isNotFound(err) {
		sawNotFound = true
	} else {
		return nil, err
	}
	if sawNotFound {
		return nil, fmt.Errorf("lrclib: track not found")
	}
	return nil, fmt.Errorf("lrclib: track not found")
}

func buildGetAttempts(original, normalized config.Track) []config.Track {
	var out []config.Track
	add := func(tr config.Track) {
		tr.Artist = compactSpaces(tr.Artist)
		tr.Title = compactSpaces(tr.Title)
		tr.Album = compactSpaces(tr.Album)
		if tr.Artist == "" || tr.Title == "" {
			return
		}
		if tr.DurationSec <= 0 && original.DurationSec > 0 {
			tr.DurationSec = original.DurationSec
		}
		for _, existing := range out {
			if tracksEqual(existing, tr) {
				return
			}
		}
		if len(out) >= maxGetAttempts {
			return
		}
		out = append(out, tr)
	}

	add(normalized)
	if strings.TrimSpace(normalized.Album) != "" {
		noAlbum := normalized
		noAlbum.Album = ""
		add(noAlbum)
	}
	return out
}

func tracksEqual(a, b config.Track) bool {
	return strings.EqualFold(a.Artist, b.Artist) &&
		strings.EqualFold(a.Title, b.Title) &&
		strings.EqualFold(a.Album, b.Album) &&
		formatDuration(a.DurationSec) == formatDuration(b.DurationSec)
}

func fetchGet(client *http.Client, track config.Track) (*Result, error) {
	q := url.Values{}
	q.Set("track_name", track.Title)
	q.Set("artist_name", track.Artist)
	if s := strings.TrimSpace(track.Album); s != "" {
		q.Set("album_name", s)
	}
	if track.DurationSec > 0 {
		q.Set("duration", formatDuration(track.DurationSec))
	}
	reqURL := getURL + "?" + q.Encode()

	data, err := fetchOne(client, reqURL, getTimeout)
	if err != nil {
		return nil, err
	}
	return resultFromResponse(data)
}

func fetchSearch(client *http.Client, track config.Track) (*Result, error) {
	q := url.Values{}
	q.Set("track_name", track.Title)
	q.Set("artist_name", track.Artist)
	if s := strings.TrimSpace(track.Album); s != "" {
		q.Set("album_name", s)
	}
	reqURL := searchURL + "?" + q.Encode()

	data, err := fetchSearchResults(client, reqURL)
	if err != nil {
		return nil, err
	}
	best, ok := bestSearchResult(data, track)
	if !ok {
		return nil, fmt.Errorf("lrclib: track not found")
	}
	return resultFromResponse(best)
}

func fetchOne(client *http.Client, reqURL string, timeout time.Duration) (Response, error) {
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, reqURL, nil)
	if err != nil {
		return Response{}, err
	}
	req.Header.Set("User-Agent", "lyrics-plugin-worker/1.0 (foobar-lyrics-ai-translator)")

	resp, err := client.Do(req)
	if err != nil {
		return Response{}, err
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return Response{}, err
	}
	if resp.StatusCode == http.StatusNotFound {
		return Response{}, fmt.Errorf("lrclib: track not found")
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return Response{}, fmt.Errorf("lrclib: HTTP %d: %s", resp.StatusCode, truncate(string(body), 200))
	}

	var data Response
	if err := json.Unmarshal(body, &data); err != nil {
		return Response{}, fmt.Errorf("lrclib: decode: %w", err)
	}
	return data, nil
}

func fetchSearchResults(client *http.Client, reqURL string) ([]Response, error) {
	ctx, cancel := context.WithTimeout(context.Background(), searchTimeout)
	defer cancel()

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, reqURL, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", "lyrics-plugin-worker/1.0 (foobar-lyrics-ai-translator)")

	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}
	if resp.StatusCode == http.StatusNotFound {
		return nil, fmt.Errorf("lrclib: track not found")
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil, fmt.Errorf("lrclib: HTTP %d: %s", resp.StatusCode, truncate(string(body), 200))
	}

	var results []Response
	if err := json.Unmarshal(body, &results); err != nil {
		return nil, fmt.Errorf("lrclib: search decode: %w", err)
	}
	return results, nil
}

func resultFromResponse(data Response) (*Result, error) {
	var lines []lyrics.Line
	if strings.TrimSpace(data.SyncedLyrics) != "" {
		lines = lyrics.ParseSynced(data.SyncedLyrics)
	}
	if len(lines) == 0 && strings.TrimSpace(data.PlainLyrics) != "" {
		lines = lyrics.ParsePlain(data.PlainLyrics)
	}
	if len(lines) == 0 {
		return nil, fmt.Errorf("lrclib: track not found (empty lyrics)")
	}

	return &Result{
		ProviderID: data.ID,
		AlbumName:  strings.TrimSpace(data.AlbumName),
		Duration:   data.Duration,
		Lines:      lines,
	}, nil
}

func normalizeTrack(track config.Track) config.Track {
	track.Artist = primaryArtist(track.Artist)
	track.Title = cleanTitle(track.Title)
	track.Album = cleanTitle(track.Album)
	return track
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

func cleanTitle(s string) string {
	s = compactSpaces(s)
	for {
		next := bracketSuffixRe.ReplaceAllString(s, "")
		next = compactSpaces(featRe.ReplaceAllString(next, ""))
		if next == s {
			return next
		}
		s = next
	}
}

func compactSpaces(s string) string {
	return spaceRe.ReplaceAllString(strings.TrimSpace(s), " ")
}

func formatDuration(sec float64) string {
	if sec <= 0 {
		return ""
	}
	return strconv.FormatFloat(sec, 'f', 0, 64)
}

func bestSearchResult(results []Response, track config.Track) (Response, bool) {
	type candidate struct {
		response Response
		score    int
	}
	var candidates []candidate
	for _, r := range results {
		if strings.TrimSpace(r.SyncedLyrics) == "" && strings.TrimSpace(r.PlainLyrics) == "" {
			continue
		}
		score := matchScore(r, track)
		if score <= 0 {
			continue
		}
		candidates = append(candidates, candidate{response: r, score: score})
	}
	if len(candidates) == 0 {
		return Response{}, false
	}
	sort.SliceStable(candidates, func(i, j int) bool {
		return candidates[i].score > candidates[j].score
	})
	return candidates[0].response, true
}

func matchScore(r Response, track config.Track) int {
	score := 0
	if normalizedText(r.TrackName) == normalizedText(track.Title) {
		score += 6
	} else if strings.Contains(normalizedText(r.TrackName), normalizedText(track.Title)) ||
		strings.Contains(normalizedText(track.Title), normalizedText(r.TrackName)) {
		score += 2
	}
	if normalizedText(r.ArtistName) == normalizedText(track.Artist) {
		score += 5
	} else if strings.Contains(normalizedText(r.ArtistName), normalizedText(track.Artist)) ||
		strings.Contains(normalizedText(track.Artist), normalizedText(r.ArtistName)) {
		score += 2
	}
	if track.DurationSec > 0 && r.Duration > 0 {
		diff := r.Duration - track.DurationSec
		if diff < 0 {
			diff = -diff
		}
		if diff <= 2 {
			score += 6
		} else if diff <= 5 {
			score += 2
		} else {
			score -= 4
		}
	}
	if strings.TrimSpace(r.SyncedLyrics) != "" {
		score += 2
	}
	return score
}

func normalizedText(s string) string {
	return strings.ToLower(cleanTitle(s))
}

func isNotFound(err error) bool {
	return err != nil && strings.Contains(err.Error(), "not found")
}

func truncate(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n] + "..."
}
