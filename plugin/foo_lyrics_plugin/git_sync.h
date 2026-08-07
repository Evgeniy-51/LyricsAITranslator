#pragma once
#include "plugin_config.h"

namespace git_sync {
	enum class operation_t { pull, push };

	bool is_busy();
	void refresh_process_state();
	operation_t current_operation();
	const char* last_status_message();
	DWORD last_exit_code();

	void schedule_startup_pull(const plugin_settings_t& settings);
	bool start_pull(const plugin_settings_t& settings);
	bool start_push(const plugin_settings_t& settings);

	void notify_window_status(const char* text);
}
