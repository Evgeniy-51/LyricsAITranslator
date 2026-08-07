#pragma once
#include "plugin_config.h"

struct track_info_t {
	pfc::string8 artist;
	pfc::string8 title;
	pfc::string8 album;
	double duration_sec = 0;
};

namespace worker_launcher {
	const DWORD EXIT_LLM_JSON = 21;
	const DWORD EXIT_LLM_USER = 22;

	inline bool is_session_exit_code(DWORD code) {
		return code == EXIT_LLM_JSON || code == EXIT_LLM_USER;
	}

	bool is_busy();
	bool is_running_for(const char* cache_path);
	void refresh_process_state();
	DWORD last_exit_code();
	bool launch(const plugin_settings_t& settings, const track_info_t& track, const char* cache_path);
	const char* last_launch_error();
	const char* exit_code_message(DWORD code);
}
