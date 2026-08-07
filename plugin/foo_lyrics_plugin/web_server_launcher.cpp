#include "stdafx.h"
#include "web_server_launcher.h"
#include "plugin_config.h"

namespace {

static HANDLE g_process = NULL;
static unsigned g_started_port = 0;
static pfc::string8 g_started_host;
static pfc::string8 g_started_token;

static bool win32_file_exists(const char* path) {
	if (path == NULL || path[0] == '\0') return false;
	const auto wpath = pfc::stringcvt::string_wide_from_utf8(path);
	const DWORD attr = GetFileAttributesW(wpath);
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void refresh_process_state() {
	if (g_process == NULL) return;
	DWORD code = 0;
	if (!GetExitCodeProcess(g_process, &code) || code == STILL_ACTIVE) return;
	CloseHandle(g_process);
	g_process = NULL;
	g_started_port = 0;
	g_started_host.reset();
	g_started_token.reset();
}

static bool same_web_launch(const plugin_settings_t& settings) {
	return g_process != NULL
		&& g_started_port == settings.web_port
		&& stricmp_utf8(g_started_host.get_ptr(), settings.web_host.get_ptr()) == 0
		&& strcmp(g_started_token.get_ptr(), settings.web_auth_token.get_ptr()) == 0;
}

} // namespace

namespace web_server_launcher {

bool is_running() {
	refresh_process_state();
	return g_process != NULL;
}

bool ensure_running(const plugin_settings_t& settings) {
	if (!settings.web_enabled) {
		stop();
		return false;
	}
	refresh_process_state();
	if (same_web_launch(settings)) return true;
	if (g_process != NULL) stop();

	pfc::string8 module_dir;
	if (!plugin_config::get_module_dir(module_dir)) {
		console::print("Lyrics Web: cannot resolve plugin folder.");
		return false;
	}

	pfc::string8 server_exe = module_dir;
	server_exe.add_filename("lyrics_server.exe");
	if (!win32_file_exists(server_exe.get_ptr())) {
		console::printf("Lyrics Web: missing lyrics_server.exe in %s", module_dir.get_ptr());
		return false;
	}

	pfc::string_formatter cmd;
	cmd << "\"" << server_exe.get_ptr() << "\""
		<< " -host \"" << settings.web_host.get_ptr() << "\""
		<< " -port " << settings.web_port;
	if (!settings.web_auth_token.is_empty())
		cmd << " -token \"" << settings.web_auth_token.get_ptr() << "\"";

	const auto cmd_wide = pfc::stringcvt::string_wide_from_utf8(cmd.get_ptr());
	const auto workdir_wide = pfc::stringcvt::string_wide_from_utf8(module_dir.get_ptr());
	pfc::array_t<wchar_t> cmd_buf;
	cmd_buf.set_size(cmd_wide.length() + 1);
	memcpy(cmd_buf.get_ptr(), cmd_wide.get_ptr(), (cmd_wide.length() + 1) * sizeof(wchar_t));

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	if (!CreateProcessW(NULL, cmd_buf.get_ptr(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, workdir_wide.get_ptr(), &si, &pi)) {
		console::printf("Lyrics Web: CreateProcess failed (%u)", GetLastError());
		return false;
	}
	CloseHandle(pi.hThread);
	g_process = pi.hProcess;
	g_started_port = settings.web_port;
	g_started_host = settings.web_host;
	g_started_token = settings.web_auth_token;
	console::printf("Lyrics Web: server started on port %u", settings.web_port);
	return true;
}

void stop() {
	refresh_process_state();
	if (g_process != NULL) {
		TerminateProcess(g_process, 0);
		CloseHandle(g_process);
		g_process = NULL;
		g_started_port = 0;
		g_started_host.reset();
		g_started_token.reset();
		console::print("Lyrics Web: server stopped.");
	}
}

} // namespace
