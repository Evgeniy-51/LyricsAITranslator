#pragma once
#include "plugin_config.h"

namespace web_server_launcher {
	bool ensure_running(const plugin_settings_t& settings);
	void stop();
	bool is_running();
}
