#include "stdafx.h"
#include "worker_launcher.h"
#include "plugin_config.h"

namespace {

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

static pfc::string8 g_running_cache_path;
static pfc::string8 g_last_launch_error;
static HANDLE g_process = NULL;
static DWORD g_last_exit_code = 0;

static bool win32_file_exists(const char* path) {
	if (path == NULL || path[0] == '\0') return false;
	const auto wpath = pfc::stringcvt::string_wide_from_utf8(path);
	const DWORD attr = GetFileAttributesW(wpath);
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool win32_write_file(const char* path, const void* data, t_size bytes) {
	const auto wpath = pfc::stringcvt::string_wide_from_utf8(path);
	HANDLE h = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD written = 0;
	const BOOL ok = WriteFile(h, data, (DWORD)bytes, &written, NULL);
	CloseHandle(h);
	return ok && written == bytes;
}

} // namespace

namespace worker_launcher {

void refresh_process_state() {
	if (g_process == NULL) return;
	DWORD code = 0;
	if (!GetExitCodeProcess(g_process, &code) || code != STILL_ACTIVE) {
		g_last_exit_code = (code == STILL_ACTIVE) ? 0 : code;
		CloseHandle(g_process);
		g_process = NULL;
		g_running_cache_path.reset();
	}
}

DWORD last_exit_code() {
	refresh_process_state();
	return g_last_exit_code;
}

bool is_busy() {
	refresh_process_state();
	return g_process != NULL;
}

bool is_running_for(const char* cache_path) {
	if (!is_busy()) return false;
	return stricmp_utf8(g_running_cache_path.get_ptr(), cache_path) == 0;
}

const char* last_launch_error() {
	return g_last_launch_error.get_ptr();
}

const char* exit_code_message(DWORD code) {
	switch (code) {
	case 10: return "Proxy is misconfigured in config.json.";
	case 11: return "Network error while fetching lyrics.";
	case 12: return "No synced lyrics found on LRCLib.";
	case 21: return "Invalid translation response from the AI.";
	case 22: return "Translation failed.";
	case 30: return "Could not write cache file (check cacheDir path).";
	default: return NULL;
	}
}

bool launch(const plugin_settings_t& settings, const track_info_t& track, const char* cache_path) {
	g_last_launch_error.reset();
	refresh_process_state();
	if (g_process != NULL) {
		if (stricmp_utf8(g_running_cache_path.get_ptr(), cache_path) == 0) return true;
		g_last_launch_error = "Worker is already running for another track.";
		return false;
	}

	pfc::string8 module_dir;
	if (!plugin_config::get_module_dir(module_dir)) {
		g_last_launch_error = "Cannot resolve plugin folder.";
		console::printf("Lyrics: cannot resolve plugin directory");
		return false;
	}

	if (!plugin_config::ensure_cache_tree(cache_path)) {
		console::printf("Lyrics: cache folder not available for %s", cache_path);
	}

	pfc::string8 worker_exe = module_dir;
	worker_exe.add_filename("lyrics_worker.exe");
	if (!win32_file_exists(worker_exe.get_ptr())) {
		g_last_launch_error = PFC_string_formatter() << "Missing lyrics_worker.exe in: " << module_dir.get_ptr();
		console::printf("Lyrics: worker not found: %s", worker_exe.get_ptr());
		return false;
	}

	pfc::string8 run_config = module_dir;
	run_config.add_filename("run-config.json");

	pfc::string_formatter json;
	json << "{\n"
		<< "  \"track\": {\n"
		<< "    \"artist\": \"" << json_escape(track.artist.get_ptr()) << "\",\n"
		<< "    \"title\": \"" << json_escape(track.title.get_ptr()) << "\",\n"
		<< "    \"album\": \"" << json_escape(track.album.get_ptr()) << "\",\n"
		<< "    \"durationSec\": " << track.duration_sec << "\n"
		<< "  },\n"
		<< "  \"cacheDir\": \"" << json_escape(settings.cache_dir.get_ptr()) << "\",\n"
		<< "  \"llm\": {\n"
		<< "    \"baseUrl\": \"" << json_escape(settings.llm_base_url.get_ptr()) << "\",\n"
		<< "    \"model\": \"" << json_escape(settings.llm_model.get_ptr()) << "\",\n"
		<< "    \"apiKey\": \"" << json_escape(settings.llm_api_key.get_ptr()) << "\"\n"
		<< "  },\n"
		<< "  \"proxy\": {\n"
		<< "    \"enabled\": " << (settings.proxy_enabled ? "true" : "false") << ",\n"
		<< "    \"type\": \"" << json_escape(settings.proxy_type.get_ptr()) << "\",\n"
		<< "    \"url\": \"" << json_escape(settings.proxy_url.get_ptr()) << "\",\n"
		<< "    \"port\": \"" << json_escape(settings.proxy_port.get_ptr()) << "\",\n"
		<< "    \"user\": \"" << json_escape(settings.proxy_user.get_ptr()) << "\",\n"
		<< "    \"password\": \"" << json_escape(settings.proxy_pass.get_ptr()) << "\"\n"
		<< "  },\n"
		<< "  \"targetLang\": \"" << json_escape(settings.target_lang.get_ptr()) << "\",\n"
		<< "  \"enableTranslation\": " << (settings.enable_translation ? "true" : "false") << ",\n"
		<< "  \"timeoutSec\": " << settings.timeout_sec << "\n"
		<< "}\n";

	bool wrote_config = false;
	try {
		service_ptr_t<file> f;
		pfc::string8 run_config_uri = "file://";
		run_config_uri += run_config;
		filesystem::g_open_write_new(f, run_config_uri.get_ptr(), abort_callback_dummy());
		f->write_object(json.get_ptr(), json.get_length(), abort_callback_dummy());
		wrote_config = true;
	} catch (std::exception const& e) {
		console::printf("Lyrics: foobar write run-config failed: %s", e.what());
	}
	if (!wrote_config && !win32_write_file(run_config.get_ptr(), json.get_ptr(), json.get_length())) {
		g_last_launch_error = PFC_string_formatter() << "Cannot write run-config.json in: " << module_dir.get_ptr();
		console::printf("Lyrics: cannot write run-config: %s", run_config.get_ptr());
		return false;
	}

	pfc::string_formatter cmd;
	cmd << "\"" << worker_exe.get_ptr() << "\" -config \"" << run_config.get_ptr() << "\"";
	const auto cmd_wide = pfc::stringcvt::string_wide_from_utf8(cmd.get_ptr());
	const auto workdir_wide = pfc::stringcvt::string_wide_from_utf8(module_dir.get_ptr());
	pfc::array_t<wchar_t> cmd_mutable;
	cmd_mutable.set_size(cmd_wide.length() + 1);
	memcpy(cmd_mutable.get_ptr(), cmd_wide.get_ptr(), (cmd_wide.length() + 1) * sizeof(wchar_t));

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	if (!CreateProcessW(NULL, cmd_mutable.get_ptr(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, workdir_wide.get_ptr(), &si, &pi)) {
		const DWORD err = GetLastError();
		g_last_launch_error = PFC_string_formatter() << "CreateProcess failed (" << err << ").";
		console::printf("Lyrics: CreateProcess failed (%u) cmd=%s", err, cmd.get_ptr());
		return false;
	}
	CloseHandle(pi.hThread);
	if (g_process) CloseHandle(g_process);
	g_process = pi.hProcess;
	g_running_cache_path = cache_path;
	g_last_exit_code = 0;
	console::printf("Lyrics: worker started for %s - %s", track.artist.get_ptr(), track.title.get_ptr());
	console::printf("Lyrics: cache path %s", cache_path);
	return true;
}

} // namespace
