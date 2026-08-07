#include "stdafx.h"
#include "plugin_config.h"
#include "cache_reader.h"
#include "json_helpers.h"
#include <helpers/create_directory_helper.h>

namespace {

using json_helpers::find_string;

static void fix_overescaped_windows_path(pfc::string8& path) {
	if (path.is_empty() || path.length() < 3 || path[1] != ':') return;
	for (int pass = 0; pass < 8; ++pass) {
		pfc::string8 next;
		bool changed = false;
		for (t_size i = 0; i < path.length(); ++i) {
			if (path[i] == '\\' && i + 1 < path.length() && path[i + 1] == '\\') {
				next.add_byte('\\');
				++i;
				changed = true;
			} else {
				next.add_byte(path[i]);
			}
		}
		if (!changed) break;
		path = next;
	}
}

bool find_uint(const char* json, const char* key, unsigned& out) {
	pfc::string8 pattern;
	pattern << "\"" << key << "\"";
	const char* pos = strstr(json, pattern.get_ptr());
	if (!pos) return false;
	pos = strchr(pos + pattern.length(), ':');
	if (!pos) return false;
	++pos;
	while (*pos == ' ' || *pos == '\t') ++pos;
	out = (unsigned)strtoul(pos, NULL, 10);
	return true;
}

bool find_bool(const char* json, const char* key, bool& out) {
	pfc::string8 pattern;
	pattern << "\"" << key << "\"";
	const char* pos = strstr(json, pattern.get_ptr());
	if (!pos) return false;
	pos = strchr(pos + pattern.length(), ':');
	if (!pos) return false;
	++pos;
	while (*pos == ' ' || *pos == '\t') ++pos;
	if (strncmp(pos, "true", 4) == 0) { out = true; return true; }
	if (strncmp(pos, "false", 5) == 0) { out = false; return true; }
	return false;
}

pfc::string8 json_escape(const char* s) {
	pfc::string8 out;
	for (const char* p = s; *p; ++p) {
		switch (*p) {
		case '\\': out += "\\\\"; break;
		case '"': out += "\\\""; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default: out.add_byte(*p); break;
		}
	}
	return out;
}

static void normalize_target_lang(pfc::string8& lang, const pfc::string8& custom) {
	if (stricmp_utf8(lang.get_ptr(), "other") == 0 && !custom.is_empty()) {
		lang = custom;
		return;
	}
	if (stricmp_utf8(lang.get_ptr(), "ru") == 0) { lang = "Русский"; return; }
	if (stricmp_utf8(lang.get_ptr(), "en") == 0) { lang = "English"; return; }
	if (stricmp_utf8(lang.get_ptr(), "zh") == 0) { lang = "中文"; return; }
	if (stricmp_utf8(lang.get_ptr(), "es") == 0) { lang = "Español"; return; }
	if (stricmp_utf8(lang.get_ptr(), "de") == 0) { lang = "Deutsch"; return; }
	if (stricmp_utf8(lang.get_ptr(), "fr") == 0) { lang = "Français"; return; }
	if (stricmp_utf8(lang.get_ptr(), "Russian") == 0) { lang = "Русский"; return; }
	if (stricmp_utf8(lang.get_ptr(), "Chinese") == 0) { lang = "中文"; return; }
	if (stricmp_utf8(lang.get_ptr(), "Spanish") == 0) { lang = "Español"; return; }
	if (stricmp_utf8(lang.get_ptr(), "German") == 0) { lang = "Deutsch"; return; }
	if (stricmp_utf8(lang.get_ptr(), "French") == 0) { lang = "Français"; return; }
	if (lang.is_empty()) lang = "Русский";
}

static pfc::string8 path_relative_to_module(const pfc::string8& path) {
	pfc::string8 mod;
	if (!plugin_config::get_module_dir(mod)) return path;
	if (path.length() <= mod.length()) return path;
	pfc::string8 prefix = mod;
	prefix += "\\";
	if (strncmp(path.get_ptr(), prefix.get_ptr(), prefix.length()) == 0)
		return pfc::string8(path.get_ptr() + prefix.length());
	return path;
}

static pfc::string8 cache_dir_for_json(const plugin_settings_t& s) {
	return path_relative_to_module(s.cache_dir);
}

} // namespace

namespace plugin_config {

bool get_module_dir(pfc::string8& out) {
	const char* full = core_api::get_my_full_path();
	if (full == NULL || full[0] == '\0') return false;
	out = full;
	out.truncate(out.scan_filename());
	return !out.is_empty();
}

bool get_config_path(pfc::string8& out) {
	pfc::string8 dir;
	if (!get_module_dir(dir)) return false;
	out = dir;
	out.add_filename("config.json");
	return true;
}

void apply_defaults(plugin_settings_t& s, const char* config_dir) {
	if (s.cache_dir.is_empty()) {
		s.cache_dir = config_dir;
		s.cache_dir.add_filename("temp");
	} else if (strchr(s.cache_dir.get_ptr(), ':') == NULL && s.cache_dir[0] != '\\' && s.cache_dir[0] != '/') {
		pfc::string8 rel = s.cache_dir;
		s.cache_dir = config_dir;
		s.cache_dir.add_filename(rel);
	}
	if (s.llm_base_url.is_empty()) s.llm_base_url = "https://api.openai.com/v1";
	if (s.llm_model.is_empty()) s.llm_model = "gpt-4o-mini";
	if (s.target_lang.is_empty()) s.target_lang = "Русский";
	if (s.sync_repo_dir.is_empty()) {
		s.sync_repo_dir = s.cache_dir;
	} else if (strchr(s.sync_repo_dir.get_ptr(), ':') == NULL && s.sync_repo_dir[0] != '\\' && s.sync_repo_dir[0] != '/') {
		pfc::string8 rel = s.sync_repo_dir;
		s.sync_repo_dir = config_dir;
		s.sync_repo_dir.add_filename(rel);
	}
	if (s.sync_branch.is_empty()) s.sync_branch = "main";
	if (s.sync_git_path.is_empty()) s.sync_git_path = "git";
	if (s.sync_startup_delay_ms == 0) s.sync_startup_delay_ms = 3000;
	fix_overescaped_windows_path(s.cache_dir);
	fix_overescaped_windows_path(s.sync_repo_dir);
	if (s.timeout_sec <= 0) s.timeout_sec = 120;
	if (s.web_host.is_empty()) s.web_host = "0.0.0.0";
	if (s.web_port == 0) s.web_port = 8765;
	if (s.web_update_interval_ms == 0) s.web_update_interval_ms = 500;
	if (s.proxy_type.is_empty()
		|| (stricmp_utf8(s.proxy_type.get_ptr(), "socks5") != 0
			&& stricmp_utf8(s.proxy_type.get_ptr(), "http") != 0
			&& stricmp_utf8(s.proxy_type.get_ptr(), "https") != 0)) {
		s.proxy_type = "socks5";
	} else if (stricmp_utf8(s.proxy_type.get_ptr(), "https") == 0) {
		s.proxy_type = "http";
	}
}

bool load(plugin_settings_t& out) {
	out = plugin_settings_t{};
	pfc::string8 dir;
	get_module_dir(dir);

	pfc::string8 path;
	if (!get_config_path(path)) {
		apply_defaults(out, dir.get_ptr());
		return false;
	}

	pfc::array_t<t_uint8> data;
	try {
		service_ptr_t<file> f;
		filesystem::g_open_read(f, path, abort_callback_dummy());
		t_filesize size = f->get_size(abort_callback_dummy());
		if (size == foobar2000_io::filesize_invalid || size > 1024 * 1024) {
			apply_defaults(out, dir.get_ptr());
			return false;
		}
		data.set_size((t_size)size);
		f->read_object(data.get_ptr(), size, abort_callback_dummy());
	} catch (...) {
		apply_defaults(out, dir.get_ptr());
		return false;
	}

	pfc::string8 json;
	json.set_string(reinterpret_cast<const char*>(data.get_ptr()), data.get_size());

	find_string(json.get_ptr(), "cacheDir", out.cache_dir);
	find_bool(json.get_ptr(), "enableTranslation", out.enable_translation);

	const char* llm = strstr(json.get_ptr(), "\"llm\"");
	if (llm) {
		find_string(llm, "baseUrl", out.llm_base_url);
		find_string(llm, "model", out.llm_model);
		find_string(llm, "apiKey", out.llm_api_key);
	}
	const char* proxy = strstr(json.get_ptr(), "\"proxy\"");
	if (proxy) {
		find_bool(proxy, "enabled", out.proxy_enabled);
		find_string(proxy, "type", out.proxy_type);
		find_string(proxy, "url", out.proxy_url);
		find_string(proxy, "port", out.proxy_port);
		find_string(proxy, "user", out.proxy_user);
		find_string(proxy, "password", out.proxy_pass);
	}
	pfc::string8 custom;
	find_string(json.get_ptr(), "targetLang", out.target_lang);
	find_string(json.get_ptr(), "targetLangCustom", custom);
	{
		unsigned tsec = 0;
		if (find_uint(json.get_ptr(), "timeoutSec", tsec)) out.timeout_sec = (int)tsec;
	}

	const char* sync = strstr(json.get_ptr(), "\"sync\"");
	if (sync) {
		find_bool(sync, "enabled", out.sync_enabled);
		find_bool(sync, "autoSetup", out.sync_auto_setup);
		find_bool(sync, "pullOnStartup", out.sync_pull_on_startup);
		find_string(sync, "repoDir", out.sync_repo_dir);
		find_string(sync, "remoteUrl", out.sync_remote_url);
		find_string(sync, "pat", out.sync_pat);
		find_string(sync, "branch", out.sync_branch);
		find_string(sync, "gitPath", out.sync_git_path);
		find_uint(sync, "startupDelayMs", out.sync_startup_delay_ms);
	}

	const char* web = strstr(json.get_ptr(), "\"web\"");
	if (web) {
		find_bool(web, "enabled", out.web_enabled);
		find_string(web, "host", out.web_host);
		find_uint(web, "port", out.web_port);
		find_string(web, "authToken", out.web_auth_token);
		find_uint(web, "updateIntervalMs", out.web_update_interval_ms);
	}

	apply_defaults(out, dir.get_ptr());
	normalize_target_lang(out.target_lang, custom);
	return true;
}

bool save(const plugin_settings_t& in) {
	pfc::string8 path;
	if (!get_config_path(path)) return false;

	plugin_settings_t s = in;
	pfc::string8 dir;
	get_module_dir(dir);
	apply_defaults(s, dir.get_ptr());

	const pfc::string8 cache_json = cache_dir_for_json(s);
	pfc::string_formatter json;
	json << "{\n"
		<< "  \"cacheDir\": \"" << json_escape(cache_json.get_ptr()) << "\",\n"
		<< "  \"enableTranslation\": " << (s.enable_translation ? "true" : "false") << ",\n"
		<< "  \"llm\": {\n"
		<< "    \"baseUrl\": \"" << json_escape(s.llm_base_url.get_ptr()) << "\",\n"
		<< "    \"model\": \"" << json_escape(s.llm_model.get_ptr()) << "\",\n"
		<< "    \"apiKey\": \"" << json_escape(s.llm_api_key.get_ptr()) << "\"\n"
		<< "  },\n"
		<< "  \"proxy\": {\n"
		<< "    \"enabled\": " << (s.proxy_enabled ? "true" : "false") << ",\n"
		<< "    \"type\": \"" << json_escape(s.proxy_type.get_ptr()) << "\",\n"
		<< "    \"url\": \"" << json_escape(s.proxy_url.get_ptr()) << "\",\n"
		<< "    \"port\": \"" << json_escape(s.proxy_port.get_ptr()) << "\",\n"
		<< "    \"user\": \"" << json_escape(s.proxy_user.get_ptr()) << "\",\n"
		<< "    \"password\": \"" << json_escape(s.proxy_pass.get_ptr()) << "\"\n"
		<< "  },\n"
		<< "  \"targetLang\": \"" << json_escape(s.target_lang.get_ptr()) << "\",\n"
		<< "  \"timeoutSec\": " << s.timeout_sec;
	{
		const pfc::string8 sync_repo = path_relative_to_module(s.sync_repo_dir);
		json << ",\n  \"sync\": {\n"
			<< "    \"enabled\": " << (s.sync_enabled ? "true" : "false") << ",\n"
			<< "    \"autoSetup\": " << (s.sync_auto_setup ? "true" : "false") << ",\n"
			<< "    \"pullOnStartup\": " << (s.sync_pull_on_startup ? "true" : "false") << ",\n"
			<< "    \"startupDelayMs\": " << s.sync_startup_delay_ms << ",\n"
			<< "    \"repoDir\": \"" << json_escape(sync_repo.get_ptr()) << "\",\n"
			<< "    \"remoteUrl\": \"" << json_escape(s.sync_remote_url.get_ptr()) << "\",\n"
			<< "    \"pat\": \"" << json_escape(s.sync_pat.get_ptr()) << "\",\n"
			<< "    \"branch\": \"" << json_escape(s.sync_branch.get_ptr()) << "\",\n"
			<< "    \"gitPath\": \"" << json_escape(s.sync_git_path.get_ptr()) << "\"\n"
			<< "  }";
	}
	json << ",\n  \"web\": {\n"
		<< "    \"enabled\": " << (s.web_enabled ? "true" : "false") << ",\n"
		<< "    \"host\": \"" << json_escape(s.web_host.get_ptr()) << "\",\n"
		<< "    \"port\": " << s.web_port << ",\n"
		<< "    \"authToken\": \"" << json_escape(s.web_auth_token.get_ptr()) << "\",\n"
		<< "    \"updateIntervalMs\": " << s.web_update_interval_ms << "\n"
		<< "  }";
	json << "\n}\n";

	try {
		service_ptr_t<file> f;
		filesystem::g_open_write_new(f, path.get_ptr(), abort_callback_dummy());
		f->write_object(json.get_ptr(), json.get_length(), abort_callback_dummy());
	} catch (std::exception const& e) {
		console::printf("Lyrics AI Translator: cannot save config: %s", e.what());
		return false;
	}
	return true;
}

bool ensure_cache_tree(const char* cache_file_path) {
	if (cache_file_path == NULL || cache_file_path[0] == '\0') return false;
	try {
		create_directory_helper::create_path(cache_file_path, abort_callback_dummy());
		return true;
	} catch (std::exception const& e) {
		console::printf("Lyrics AI Translator: cannot create cache path: %s (%s)", cache_file_path, e.what());
	} catch (...) {
		console::printf("Lyrics AI Translator: cannot create cache path: %s", cache_file_path);
	}
	return false;
}

} // namespace
