package config

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

type Track struct {
	Artist      string  `json:"artist"`
	Title       string  `json:"title"`
	Album       string  `json:"album"`
	DurationSec float64 `json:"durationSec"`
}

type LLM struct {
	BaseURL string `json:"baseUrl"`
	Model   string `json:"model"`
	APIKey  string `json:"apiKey"`
}

type Proxy struct {
	Enabled  bool   `json:"enabled"`
	Type     string `json:"type"`
	URL      string `json:"url"`
	Port     string `json:"port"`
	User     string `json:"user"`
	Password string `json:"password"`
}

type Config struct {
	Track             Track  `json:"track"`
	CacheDir          string `json:"cacheDir"`
	LLM               LLM    `json:"llm"`
	Proxy             Proxy  `json:"proxy"`
	TargetLang        string `json:"targetLang"`
	TargetLangCustom  string `json:"targetLangCustom,omitempty"` // legacy; merged into TargetLang on load
	EnableTranslation bool   `json:"enableTranslation"`
	TimeoutSec        int    `json:"timeoutSec"`
}

func Load(path string) (*Config, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var cfg Config
	if err := json.Unmarshal(data, &cfg); err != nil {
		return nil, err
	}
	configDir := "."
	if abs, err := filepath.Abs(path); err == nil {
		configDir = filepath.Dir(abs)
	}
	cfg.applyDefaults(configDir)
	if err := cfg.validate(); err != nil {
		return nil, err
	}
	return &cfg, nil
}

func (c *Config) applyDefaults(configDir string) {
	if strings.TrimSpace(c.CacheDir) == "" {
		c.CacheDir = filepath.Join(configDir, "temp")
	} else if !filepath.IsAbs(c.CacheDir) {
		c.CacheDir = filepath.Join(configDir, c.CacheDir)
	}
	c.CacheDir = filepath.Clean(c.CacheDir)
	if strings.TrimSpace(c.LLM.BaseURL) == "" {
		c.LLM.BaseURL = "https://api.openai.com/v1"
	}
	if strings.TrimSpace(c.LLM.Model) == "" {
		c.LLM.Model = "gpt-4o-mini"
	}
	if c.TimeoutSec <= 0 {
		c.TimeoutSec = 120
	}
	c.TargetLang = NormalizeTargetLang(c.TargetLang, c.TargetLangCustom)
	c.TargetLangCustom = ""
}

func (c *Config) validate() error {
	if strings.TrimSpace(c.Track.Artist) == "" || strings.TrimSpace(c.Track.Title) == "" {
		return fmt.Errorf("track.artist and track.title are required")
	}
	if err := c.Proxy.validateProxy(); err != nil {
		return err
	}
	if c.EnableTranslation && strings.TrimSpace(c.LLM.APIKey) == "" {
		return fmt.Errorf("enableTranslation requires llm.apiKey")
	}
	return nil
}

// TargetLanguageLabel returns the language name for the LLM prompt (same as targetLang in config).
func (c *Config) TargetLanguageLabel() string {
	if s := strings.TrimSpace(c.TargetLang); s != "" {
		return s
	}
	return "Русский"
}
