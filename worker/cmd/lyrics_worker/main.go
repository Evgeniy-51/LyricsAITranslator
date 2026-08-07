package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"
	"time"

	"lyrics-plugin/worker/internal/cache"
	"lyrics-plugin/worker/internal/config"
	"lyrics-plugin/worker/internal/httpclient"
	"lyrics-plugin/worker/internal/llm"
	"lyrics-plugin/worker/internal/lrclib"
)

const (
	exitOK           = 0
	exitProxyInvalid = 10
	exitNetwork      = 11
	exitLRCLibMiss   = 12
	exitLLMJSON      = 21
	exitLLMUser      = 22
	exitCacheWrite   = 30
)

const msgInvalidJSONResponse = "Invalid translation response from the AI. Showing original lyrics only."

func main() {
	log.SetFlags(log.LstdFlags)
	configPath := flag.String("config", "config.json", "path to config JSON")
	flag.Parse()

	absConfig, err := filepath.Abs(*configPath)
	if err != nil {
		log.Fatalf("config path: %v", err)
	}
	sessionDir := filepath.Dir(absConfig)

	cfg, err := config.Load(*configPath)
	if err != nil {
		log.Printf("config: %v", err)
		_ = cache.WriteSessionError(sessionDir, "", config.UserMessageForLoadError(err))
		os.Exit(exitLLMUser)
	}

	if cfg.Proxy.Enabled {
		proxyAddr, err := cfg.Proxy.HostPort()
		if err != nil {
			log.Printf("proxy: %v", err)
			_ = cache.WriteSessionError(sessionDir, "", err.Error())
			os.Exit(exitProxyInvalid)
		}
		log.Printf("using %s proxy %s", cfg.Proxy.NormalizedType(), proxyAddr)
	} else {
		log.Printf("proxy disabled; using direct connections")
	}

	client, err := httpclient.New(cfg.Proxy, time.Duration(cfg.TimeoutSec)*time.Second)
	if err != nil {
		log.Printf("http client: %v", err)
		_ = cache.WriteSessionError(sessionDir, "", err.Error())
		os.Exit(exitProxyInvalid)
	}

	cachePath := cache.Path(cfg.CacheDir, cfg.Track)
	log.Printf("cache dir: %s", cfg.CacheDir)
	log.Printf("cache file: %s", cachePath)
	ready, existing, err := cache.IsReady(cachePath)
	if err != nil {
		log.Fatalf("cache read: %v", err)
	}
	if ready {
		log.Printf("cache ready: %s", cachePath)
		os.Exit(exitOK)
	}

	var cf *cache.File

	if existing != nil && existing.Status == "original_ready" && len(existing.Lyrics) > 0 {
		cf = existing
		log.Printf("using existing original_ready cache: %s", cachePath)
	} else {
		log.Printf("fetching lrclib: %s - %s", cfg.Track.Artist, cfg.Track.Title)
		res, err := lrclib.Fetch(client, cfg.Track)
		if err != nil {
			log.Printf("lrclib: %v", err)
			if isNotFound(err) {
				_ = cache.WriteSessionError(sessionDir, cachePath, "No synced lyrics found on LRCLib for this track.")
				os.Exit(exitLRCLibMiss)
			}
			_ = cache.WriteSessionError(sessionDir, cachePath, fmt.Sprintf("Network error: %v", err))
			os.Exit(exitNetwork)
		}
		cf = cache.NewFromLRCLib(cfg.Track, res.ProviderID, res.Lines, "original_ready")
		if res.Duration > 0 {
			cf.Track.DurationSec = res.Duration
		}
		if !cfg.EnableTranslation {
			cf.MarkReadyOriginalOnly()
			if err := cache.Save(cachePath, cf); err != nil {
				log.Printf("cache write: %v", err)
				_ = cache.WriteSessionError(sessionDir, cachePath, fmt.Sprintf("Could not write cache file: %v", err))
				os.Exit(exitCacheWrite)
			}
			log.Printf("cache ready (no translation): %s", cachePath)
			os.Exit(exitOK)
		}
		log.Printf("fetched lrclib: %d lines (cache write deferred until translation)", len(cf.Lyrics))
	}

	if !cfg.EnableTranslation {
		os.Exit(exitOK)
	}

	log.Printf("translating to %s via %s", cfg.TargetLanguageLabel(), cfg.LLM.Model)

	tr, err := llm.Translate(client, cfg, cf.Lyrics)
	if err != nil {
		log.Printf("llm: %v", err)
		if isJSONErr(err) {
			handleTranslationSessionFailure(cachePath, sessionDir, cf, msgInvalidJSONResponse)
			os.Exit(exitLLMJSON)
		}
		userMsg := "Translation failed."
		if ue := llm.AsUserError(err); ue != nil {
			userMsg = ue.Message
		}
		handleTranslationSessionFailure(cachePath, sessionDir, cf, userMsg)
		os.Exit(exitLLMUser)
	}

	cf.ApplyTranslations(tr.AlreadyInTargetLanguage, tr.Translations)
	if err := cache.Save(cachePath, cf); err != nil {
		log.Printf("cache write: %v", err)
		_ = cache.WriteSessionError(sessionDir, cachePath, fmt.Sprintf("Could not write cache file: %v", err))
		os.Exit(exitCacheWrite)
	}

	log.Printf("cache ready: %s (alreadyInTargetLanguage=%v)", cachePath, cf.AlreadyInTargetLanguage)
	os.Exit(exitOK)
}

func isNotFound(err error) bool {
	return err != nil && strings.Contains(err.Error(), "not found")
}

func isJSONErr(err error) bool {
	if err == nil {
		return false
	}
	msg := err.Error()
	return strings.Contains(msg, "invalid JSON") ||
		strings.Contains(msg, "missing translation") ||
		strings.Contains(msg, "no translation lines")
}

func handleTranslationSessionFailure(cachePath, sessionDir string, cf *cache.File, userMessage string) {
	if err := cache.RemoveFile(cachePath); err != nil {
		log.Printf("cache remove: %v", err)
	}
	if cf != nil && len(cf.Lyrics) > 0 {
		if err := cache.WriteSessionDisplay(sessionDir, cachePath, cf.Lyrics); err != nil {
			log.Printf("session display: %v", err)
		}
	}
	if err := cache.WriteSessionError(sessionDir, cachePath, userMessage); err != nil {
		log.Printf("session error: %v", err)
	} else {
		log.Printf("session error written: %s", userMessage)
	}
}
