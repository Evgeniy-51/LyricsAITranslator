#pragma once

struct lyric_line_t {
	int index = 0;
	int time_ms = 0;
	pfc::string8 original;
	pfc::string8 translation;
};

struct lyrics_cache_t {
	pfc::string8 status;
	bool already_in_target_language = false;
	pfc::array_t<lyric_line_t> lines;
};

namespace lyrics_cache {
	// Normalized path segment (lowercase, metadata cleanup) — used for new cache files.
	pfc::string8 sanitize_path_component(const char* s);
	// Canonical write path (normalized layout).
	pfc::string8 cache_file_path(const char* cache_dir, const char* artist, const char* album, const char* title);
	// Pre-normalization layout (trim + illegal chars only).
	pfc::string8 cache_file_path_legacy(const char* cache_dir, const char* artist, const char* album, const char* title);
	// Prefer normalized file if present, else legacy, else normalized (new writes).
	pfc::string8 resolve_cache_file_path(const char* cache_dir, const char* artist, const char* album, const char* title);
	bool load_file(const char* path, lyrics_cache_t& out);
	bool is_ready(const char* path);
	bool delete_cache_file(const char* path);
	void clear_session_files(const char* module_dir, const char* expected_cache_path);
	// Ephemeral run-display.json from worker after LLM JSON failure (not cache).
	bool load_session_display(const char* module_dir, const char* expected_cache_path, lyrics_cache_t& out);
	bool load_session_error(const char* module_dir, const char* expected_cache_path, pfc::string8& out_message);
}
