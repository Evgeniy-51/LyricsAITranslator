#pragma once

namespace web_state_publisher {
	void shutdown();
	void post_async(unsigned port, const char* json_body);
	bool poll_highlight_pending(unsigned port, bool* out_enabled);
	bool poll_player_command_pending(unsigned port, pfc::string8& out_command);
	bool fetch_server_url(unsigned port, pfc::string8& out_url);
	bool fetch_qr_png(unsigned port, pfc::array_t<t_uint8>& out_png);
}
