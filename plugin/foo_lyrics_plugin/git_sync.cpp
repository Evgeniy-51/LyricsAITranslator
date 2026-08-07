#include "stdafx.h"
#include "git_sync.h"
#include <helpers/create_directory_helper.h>

namespace {

static HANDLE g_process = NULL;
static DWORD g_last_exit_code = 0;
static git_sync::operation_t g_operation = git_sync::operation_t::pull;
static plugin_settings_t g_last_sync_settings;
static bool g_have_sync_settings = false;
static pfc::string8 g_last_status;
static pfc::string8 g_pending_status;

static bool win32_file_exists(const char* path) {
	if (path == NULL || path[0] == '\0') return false;
	const auto wpath = pfc::stringcvt::string_wide_from_utf8(path);
	const DWORD attr = GetFileAttributesW(wpath);
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool win32_dir_exists(const char* path) {
	if (path == NULL || path[0] == '\0') return false;
	const auto wpath = pfc::stringcvt::string_wide_from_utf8(path);
	const DWORD attr = GetFileAttributesW(wpath);
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool is_git_repo(const char* repo_dir) {
	if (repo_dir == NULL || repo_dir[0] == '\0') return false;
	pfc::string8 git_dir = repo_dir;
	git_dir.add_filename(".git");
	return win32_dir_exists(git_dir.get_ptr());
}

static bool ensure_repo_directory(const char* repo_dir) {
	if (repo_dir == NULL || repo_dir[0] == '\0') return false;
	if (win32_dir_exists(repo_dir)) return true;
	try {
		pfc::string8 marker = repo_dir;
		marker.add_filename(".gitkeep");
		create_directory_helper::create_path(marker.get_ptr(), abort_callback_dummy());
		return win32_dir_exists(repo_dir);
	} catch (...) {
		return false;
	}
}

static pfc::string8 cmd_quote(const char* s) {
	pfc::string8 out = "\"";
	for (const char* p = s; *p; ++p) {
		if (*p == '"') out += "\"\"";
		else out.add_byte(*p);
	}
	out += "\"";
	return out;
}

static pfc::string8 authenticated_remote_url(const plugin_settings_t& s) {
	const char* url = s.sync_remote_url.get_ptr();
	const char* pat = s.sync_pat.get_ptr();
	if (url == NULL || url[0] == '\0') return "";
	if (pat == NULL || pat[0] == '\0') return url;
	if (strncmp(url, "https://", 8) == 0)
		return pfc::string_formatter() << "https://x-access-token:" << pat << "@" << (url + 8);
	if (strncmp(url, "http://", 7) == 0)
		return pfc::string_formatter() << "http://x-access-token:" << pat << "@" << (url + 7);
	return url;
}

static pfc::string8 effective_repo_dir(const plugin_settings_t& s) {
	if (!s.sync_repo_dir.is_empty()) return s.sync_repo_dir;
	return s.cache_dir;
}

static pfc::string8 git_exe(const plugin_settings_t& s) {
	return s.sync_git_path.is_empty() ? "git" : s.sync_git_path;
}

static pfc::string8 git_branch(const plugin_settings_t& s) {
	return s.sync_branch.is_empty() ? "main" : s.sync_branch;
}

// Remote URL without PAT (stored in .git/config).
static pfc::string8 plain_remote_url(const plugin_settings_t& s) {
	return s.sync_remote_url;
}

static void append_git_line(pfc::string_formatter& script, const plugin_settings_t& s, const char* args) {
	script << cmd_quote(git_exe(s).get_ptr()) << " " << args << " >> git-sync.log 2>&1\r\n";
}

static void append_script_header(pfc::string_formatter& script, const plugin_settings_t& s) {
	const pfc::string8 repo = effective_repo_dir(s);
	script << "@echo off\r\nset GIT_TERMINAL_PROMPT=0\r\n"
		<< "if not exist " << cmd_quote(repo.get_ptr()) << " mkdir " << cmd_quote(repo.get_ptr()) << "\r\n"
		<< "cd /d " << cmd_quote(repo.get_ptr()) << "\r\n"
		<< "if errorlevel 1 exit /b 1\r\n"
		<< "echo === Lyrics git sync %DATE% %TIME% ===> git-sync.log\r\n";
}

static void append_integrate_fetch_head(pfc::string_formatter& script, const plugin_settings_t& s) {
	const pfc::string8 branch = git_branch(s);
	// Empty local tree: take remote as-is. Otherwise merge; local wins on conflicts.
	script << cmd_quote(git_exe(s).get_ptr()) << " ls-files | findstr /r \".\" >nul\r\n"
		<< "if errorlevel 1 (\r\n"
		<< "  " << cmd_quote(git_exe(s).get_ptr()) << " checkout -B " << cmd_quote(branch.get_ptr())
		<< " FETCH_HEAD >> git-sync.log 2>&1\r\n"
		<< ") else (\r\n"
		<< "  " << cmd_quote(git_exe(s).get_ptr())
		<< " merge FETCH_HEAD --allow-unrelated-histories -X ours --no-edit >> git-sync.log 2>&1\r\n"
		<< ")\r\n"
		<< "if errorlevel 1 exit /b 1\r\n";
}

static pfc::string8 build_setup_section(const plugin_settings_t& s) {
	const pfc::string8 branch = git_branch(s);
	const pfc::string8 auth = authenticated_remote_url(s);
	const pfc::string8 plain = plain_remote_url(s);
	const pfc::string8 fetch_args = pfc::string_formatter()
		<< "fetch " << cmd_quote(auth.get_ptr()) << " " << cmd_quote(branch.get_ptr());
	pfc::string_formatter script;
	script << "if not exist .git (\r\n"
		<< "  " << cmd_quote(git_exe(s).get_ptr()) << " init >> git-sync.log 2>&1\r\n"
		<< "  if errorlevel 1 exit /b 1\r\n"
		<< ")\r\n"
		<< cmd_quote(git_exe(s).get_ptr()) << " remote get-url origin 1>nul 2>nul\r\n"
		<< "if errorlevel 1 (\r\n"
		<< "  " << cmd_quote(git_exe(s).get_ptr()) << " remote add origin " << cmd_quote(plain.get_ptr())
		<< " >> git-sync.log 2>&1\r\n"
		<< "  if errorlevel 1 exit /b 1\r\n"
		<< ")\r\n"
		<< cmd_quote(git_exe(s).get_ptr()) << " rev-parse --verify HEAD 1>nul 2>nul\r\n"
		<< "if errorlevel 1 (\r\n";
	append_git_line(script, s, fetch_args.get_ptr());
	script << "if errorlevel 1 exit /b 1\r\n"
		<< "  " << cmd_quote(git_exe(s).get_ptr()) << " checkout -B " << cmd_quote(branch.get_ptr())
		<< " FETCH_HEAD >> git-sync.log 2>&1\r\n"
		<< "  if errorlevel 1 exit /b 1\r\n"
		<< ")\r\n";
	return script;
}

static pfc::string8 build_pull_script(const plugin_settings_t& s) {
	const pfc::string8 auth = authenticated_remote_url(s);
	const pfc::string8 branch = git_branch(s);
	const pfc::string8 fetch_args = pfc::string_formatter()
		<< "fetch " << cmd_quote(auth.get_ptr()) << " " << cmd_quote(branch.get_ptr());
	pfc::string_formatter script;
	append_script_header(script, s);
	if (s.sync_auto_setup) script << build_setup_section(s);
	append_git_line(script, s, fetch_args.get_ptr());
	script << "if errorlevel 1 exit /b 1\r\n";
	append_integrate_fetch_head(script, s);
	script << cmd_quote(git_exe(s).get_ptr()) << " ls-files > git-sync-tracked.txt 2>> git-sync.log\r\n"
		<< "exit /b 0\r\n";
	return script;
}

static pfc::string8 build_push_script(const plugin_settings_t& s) {
	const pfc::string8 auth = authenticated_remote_url(s);
	const pfc::string8 branch = git_branch(s);
	const pfc::string8 fetch_args = pfc::string_formatter()
		<< "fetch " << cmd_quote(auth.get_ptr()) << " " << cmd_quote(branch.get_ptr());
	const pfc::string8 push_args = pfc::string_formatter()
		<< "push " << cmd_quote(auth.get_ptr()) << " " << cmd_quote(branch.get_ptr());
	pfc::string_formatter script;
	append_script_header(script, s);
	if (s.sync_auto_setup) script << build_setup_section(s);
	// Pathspec required: "add -A --ignore-removal" is a no-op on modern Git.
	// Never stage deletions: missing local files must not remove tracks from remote.
	append_git_line(script, s, "add --ignore-removal .");
	script << "if errorlevel 1 exit /b 1\r\n";
	append_git_line(script, s, "diff --cached --quiet");
	script << "if errorlevel 1 " << cmd_quote(git_exe(s).get_ptr())
		<< " commit -m \"Lyrics cache sync\" >> git-sync.log 2>&1\r\n";
	append_git_line(script, s, fetch_args.get_ptr());
	script << "if errorlevel 1 exit /b 1\r\n";
	append_integrate_fetch_head(script, s);
	append_git_line(script, s, push_args.get_ptr());
	script << "exit /b %ERRORLEVEL%\r\n";
	return script;
}

static bool validate_sync_config(const plugin_settings_t& settings, pfc::string8& err) {
	err.reset();
	if (settings.sync_remote_url.is_empty()) {
		err = "sync.remoteUrl is missing in config.json.";
		return false;
	}
	const pfc::string8 repo = effective_repo_dir(settings);
	if (!ensure_repo_directory(repo.get_ptr())) {
		err = "Cannot create cache/repo directory.";
		return false;
	}
	if (!settings.sync_auto_setup && !is_git_repo(repo.get_ptr())) {
		err = "Git repo not found (.git missing). Enable sync.autoSetup or run git init.";
		return false;
	}
	return true;
}

static bool write_script_and_run(const plugin_settings_t& settings, git_sync::operation_t op) {
	pfc::string8 module_dir;
	if (!plugin_config::get_module_dir(module_dir)) {
		g_last_status = "Cannot resolve plugin folder.";
		return false;
	}
	pfc::string8 err;
	if (!validate_sync_config(settings, err)) {
		g_last_status = err;
		console::printf("Lyrics Git: %s", g_last_status.get_ptr());
		return false;
	}

	const pfc::string8 script_path = pfc::string_formatter() << module_dir.get_ptr() << "\\git-sync-op.cmd";
	const pfc::string8 script = (op == git_sync::operation_t::pull)
		? build_pull_script(settings) : build_push_script(settings);

	try {
		service_ptr_t<file> f;
		pfc::string8 uri = "file://";
		uri += script_path;
		filesystem::g_open_write_new(f, uri.get_ptr(), abort_callback_dummy());
		f->write_object(script.get_ptr(), script.get_length(), abort_callback_dummy());
	} catch (...) {
		const auto wpath = pfc::stringcvt::string_wide_from_utf8(script_path);
		HANDLE h = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE) {
			g_last_status = "Cannot write git-sync-op.cmd.";
			return false;
		}
		DWORD written = 0;
		WriteFile(h, script.get_ptr(), (DWORD)script.get_length(), &written, NULL);
		CloseHandle(h);
	}

	pfc::string_formatter cmd;
	cmd << "cmd /c " << cmd_quote(script_path.get_ptr());
	const auto cmd_wide = pfc::stringcvt::string_wide_from_utf8(cmd.get_ptr());
	const auto workdir_wide = pfc::stringcvt::string_wide_from_utf8(module_dir.get_ptr());
	pfc::array_t<wchar_t> cmd_buf;
	cmd_buf.set_size(cmd_wide.length() + 1);
	memcpy(cmd_buf.get_ptr(), cmd_wide.get_ptr(), (cmd_wide.length() + 1) * sizeof(wchar_t));

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	if (!CreateProcessW(NULL, cmd_buf.get_ptr(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, workdir_wide.get_ptr(), &si, &pi)) {
		g_last_status = PFC_string_formatter() << "Git process failed (" << GetLastError() << ").";
		return false;
	}
	CloseHandle(pi.hThread);
	if (g_process != NULL) CloseHandle(g_process);
	g_process = pi.hProcess;
	g_operation = op;
	g_last_exit_code = 0;
	g_last_sync_settings = settings;
	g_have_sync_settings = true;
	return true;
}

static unsigned count_tracked_files(const plugin_settings_t& settings) {
	const pfc::string8 repo = effective_repo_dir(settings);
	pfc::string8 list_path = repo;
	list_path.add_filename("git-sync-tracked.txt");
	const auto wpath = pfc::stringcvt::string_wide_from_utf8(list_path);
	HANDLE h = CreateFileW(wpath.get_ptr(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	unsigned count = 0;
	char buf[4096];
	DWORD read = 0;
	pfc::string8 pending;
	for (;;) {
		if (!ReadFile(h, buf, sizeof(buf), &read, NULL) || read == 0) break;
		pending.add_string(buf, read);
	}
	CloseHandle(h);
	t_size line_start = 0;
	for (t_size i = 0; i <= pending.get_length(); ++i) {
		if (i == pending.get_length() || pending[i] == '\n' || pending[i] == '\r') {
			bool non_empty = false;
			for (t_size j = line_start; j < i; ++j) {
				const char c = pending[j];
				if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
					non_empty = true;
					break;
				}
			}
			if (non_empty) ++count;
			if (i < pending.get_length() && pending[i] == '\r' && i + 1 < pending.get_length() && pending[i + 1] == '\n') ++i;
			line_start = i + 1;
		}
	}
	return count;
}

static void finish_operation(DWORD code, const plugin_settings_t* settings) {
	if (g_operation == git_sync::operation_t::pull) {
		if (code == 0) {
			unsigned tracked = settings != NULL ? count_tracked_files(*settings) : 0;
			if (tracked == 0)
				g_last_status = "Cache pull OK (remote has no tracked lyrics files yet).";
			else
				g_last_status = PFC_string_formatter() << "Cache pull OK (" << tracked << " tracked files).";
		} else {
			g_last_status = PFC_string_formatter() << "Cache pull failed (exit " << code << "). See git-sync.log in cache folder.";
		}
	} else {
		if (code == 0) g_last_status = "Cache sync push OK.";
		else g_last_status = PFC_string_formatter() << "Cache sync failed (exit " << code << "). See git-sync.log in cache folder.";
	}
	console::printf("Lyrics Git: %s", g_last_status.get_ptr());
	git_sync::notify_window_status(g_last_status.get_ptr());
	if (code == 0) {
		// Timer / refresh_process_state often runs on main thread; plain inMainThread() only
		// queues and can leave UI updates/reload effectively stuck during heavy git I/O.
		fb2k::inMainThread2([] {
			extern void lyrics_window_reload_after_sync();
			lyrics_window_reload_after_sync();
		});
	}
}

static DWORD WINAPI delayed_pull_thread(LPVOID param) {
	auto* settings = reinterpret_cast<plugin_settings_t*>(param);
	const DWORD delay = settings->sync_startup_delay_ms;
	if (delay > 0) Sleep(delay);
	// start_pull only spawns git via CreateProcess — no need for main thread here.
	// Marshalling through inMainThread delayed startup and interacted badly with UI message flow.
	git_sync::start_pull(*settings);
	delete settings;
	return 0;
}

} // namespace

namespace git_sync {

void notify_window_status(const char* text) {
	if (text == NULL) return;
	g_pending_status = text;
	fb2k::inMainThread2([] {
		extern void lyrics_window_set_status_from_git(const char*);
		if (!g_pending_status.is_empty())
			lyrics_window_set_status_from_git(g_pending_status.get_ptr());
	});
}

bool is_busy() {
	refresh_process_state();
	return g_process != NULL;
}

void refresh_process_state() {
	if (g_process == NULL) return;
	DWORD code = 0;
	if (!GetExitCodeProcess(g_process, &code) || code == STILL_ACTIVE) return;
	g_last_exit_code = code;
	CloseHandle(g_process);
	g_process = NULL;
	finish_operation(code, g_have_sync_settings ? &g_last_sync_settings : NULL);
}

operation_t current_operation() { return g_operation; }

const char* last_status_message() { return g_last_status.get_ptr(); }

DWORD last_exit_code() {
	refresh_process_state();
	return g_last_exit_code;
}

void schedule_startup_pull(const plugin_settings_t& settings) {
	if (!settings.sync_enabled || !settings.sync_pull_on_startup) return;
	auto* copy = new plugin_settings_t(settings);
	HANDLE th = CreateThread(NULL, 0, delayed_pull_thread, copy, 0, NULL);
	if (th != NULL) CloseHandle(th);
	else delete copy;
}

bool start_pull(const plugin_settings_t& settings) {
	if (!settings.sync_enabled) {
		g_last_status = "Git sync disabled in config.";
		return false;
	}
	refresh_process_state();
	if (g_process != NULL) {
		g_last_status = "Git operation already running.";
		return false;
	}
	g_last_status = "Pulling cache from remote...";
	notify_window_status(g_last_status.get_ptr());
	console::printf("Lyrics Git: starting pull");
	return write_script_and_run(settings, operation_t::pull);
}

bool start_push(const plugin_settings_t& settings) {
	if (!settings.sync_enabled) {
		g_last_status = "Git sync disabled in config.";
		return false;
	}
	refresh_process_state();
	if (g_process != NULL) {
		g_last_status = "Git operation already running.";
		return false;
	}
	g_last_status = "Syncing cache to remote...";
	notify_window_status(g_last_status.get_ptr());
	console::printf("Lyrics Git: starting push");
	return write_script_and_run(settings, operation_t::push);
}

} // namespace
