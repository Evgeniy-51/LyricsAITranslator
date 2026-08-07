#pragma once

struct plugin_settings_t {
	pfc::string8 cache_dir;
	pfc::string8 llm_base_url;
	pfc::string8 llm_model;
	pfc::string8 llm_api_key;
	bool proxy_enabled = true;
	pfc::string8 proxy_type = "socks5";
	pfc::string8 proxy_url;
	pfc::string8 proxy_port;
	pfc::string8 proxy_user;
	pfc::string8 proxy_pass;
	pfc::string8 target_lang = "Русский";
	bool enable_translation = true;
	bool sync_enabled = false;
	pfc::string8 sync_repo_dir;
	pfc::string8 sync_remote_url;
	pfc::string8 sync_pat;
	pfc::string8 sync_branch = "main";
	pfc::string8 sync_git_path = "git";
	bool sync_pull_on_startup = true;
	unsigned sync_startup_delay_ms = 3000;
	bool sync_auto_setup = true;
	int timeout_sec = 120;
	bool web_enabled = true;
	pfc::string8 web_host = "0.0.0.0";
	unsigned web_port = 8765;
	pfc::string8 web_auth_token;
	unsigned web_update_interval_ms = 500;
};

namespace plugin_config {
	bool get_module_dir(pfc::string8& out);
	bool get_config_path(pfc::string8& out);
	bool load(plugin_settings_t& out);
	bool save(const plugin_settings_t& settings);
	void apply_defaults(plugin_settings_t& s, const char* config_dir);
	bool ensure_cache_tree(const char* cache_file_path);
}
