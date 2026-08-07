package config

import "strings"

// NormalizeTargetLang upgrades legacy config values only (ISO codes, English preset labels).
// Menu and config use native language names as-is (Русский, English, 中文, …).
func NormalizeTargetLang(targetLang, targetLangCustom string) string {
	lang := strings.TrimSpace(targetLang)
	custom := strings.TrimSpace(targetLangCustom)
	if strings.EqualFold(lang, "other") && custom != "" {
		return custom
	}
	switch strings.ToLower(lang) {
	case "ru":
		return "Русский"
	case "en":
		return "English"
	case "zh":
		return "中文"
	case "es":
		return "Español"
	case "de":
		return "Deutsch"
	case "fr":
		return "Français"
	case "":
		return "Русский"
	}
	// Short English labels from an intermediate build
	switch lang {
	case "Russian":
		return "Русский"
	case "Chinese":
		return "中文"
	case "Spanish":
		return "Español"
	case "German":
		return "Deutsch"
	case "French":
		return "Français"
	}
	return lang
}
