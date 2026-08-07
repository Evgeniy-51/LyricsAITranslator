#include "stdafx.h"
#include "web_state_publisher.h"
#include "json_helpers.h"
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace {

struct post_job_t {
	unsigned port = 8765;
	pfc::string8 json;
};

static CRITICAL_SECTION g_cs;
static bool g_cs_init = false;
static HANDLE g_worker = NULL;
static HANDLE g_event = NULL;
static volatile LONG g_plugin_quitting = 0;
static post_job_t g_pending;
static bool g_have_pending = false;

static void ensure_cs() {
	if (!g_cs_init) {
		InitializeCriticalSection(&g_cs);
		g_cs_init = true;
	}
}

static bool json_find_string(const char* json, const char* key, pfc::string8& out) {
	return json_helpers::find_string(json, key, out);
}

static bool http_get_loopback(unsigned port, const wchar_t* path, DWORD timeout_ms, pfc::array_t<t_uint8>& out_body) {
	out_body.set_count(0);
	if (path == NULL || path[0] == L'\0') return false;

	HINTERNET session = WinHttpOpen(L"lyrics-plugin/1.0",
		WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) return false;
	WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

	HINTERNET connect = WinHttpConnect(session, L"127.0.0.1", (INTERNET_PORT)port, 0);
	if (!connect) {
		WinHttpCloseHandle(session);
		return false;
	}

	HINTERNET request = WinHttpOpenRequest(connect, L"GET", path,
		NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
	if (!request) {
		WinHttpCloseHandle(connect);
		WinHttpCloseHandle(session);
		return false;
	}

	BOOL ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
	if (ok)
		ok = WinHttpReceiveResponse(request, NULL);
	if (!ok) {
		WinHttpCloseHandle(request);
		WinHttpCloseHandle(connect);
		WinHttpCloseHandle(session);
		return false;
	}

	DWORD status = 0;
	DWORD status_len = sizeof(status);
	if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_len, WINHTTP_NO_HEADER_INDEX) && status >= 400) {
		WinHttpCloseHandle(request);
		WinHttpCloseHandle(connect);
		WinHttpCloseHandle(session);
		return false;
	}

	for (;;) {
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0)
			break;
		const t_size base = out_body.get_size();
		out_body.set_size(base + avail);
		DWORD read = 0;
		if (!WinHttpReadData(request, out_body.get_ptr() + base, avail, &read)) {
			WinHttpCloseHandle(request);
			WinHttpCloseHandle(connect);
			WinHttpCloseHandle(session);
			out_body.set_count(0);
			return false;
		}
		out_body.set_size(base + read);
	}

	WinHttpCloseHandle(request);
	WinHttpCloseHandle(connect);
	WinHttpCloseHandle(session);
	return out_body.get_size() > 0;
}

static bool post_state_http(unsigned port, const char* json) {
	if (json == NULL || json[0] == '\0') return false;

	pfc::string_formatter url;
	url << "http://127.0.0.1:" << port << "/api/state";

	const auto url_wide = pfc::stringcvt::string_wide_from_utf8(url.get_ptr());
	HINTERNET session = WinHttpOpen(L"lyrics-plugin/1.0",
		WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) return false;

	DWORD timeout_ms = 1500;
	WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

	HINTERNET connect = WinHttpConnect(session, L"127.0.0.1", (INTERNET_PORT)port, 0);
	if (!connect) {
		WinHttpCloseHandle(session);
		return false;
	}

	HINTERNET request = WinHttpOpenRequest(connect, L"POST", L"/api/state",
		NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
	if (!request) {
		WinHttpCloseHandle(connect);
		WinHttpCloseHandle(session);
		return false;
	}

	const DWORD body_len = (DWORD)strlen(json);
	const wchar_t headers[] = L"Content-Type: application/json\r\n";
	BOOL ok = WinHttpSendRequest(request, headers, (DWORD)-1L,
		(LPVOID)json, body_len, body_len, 0);
	if (ok)
		ok = WinHttpReceiveResponse(request, NULL);

	WinHttpCloseHandle(request);
	WinHttpCloseHandle(connect);
	WinHttpCloseHandle(session);
	return ok != FALSE;
}

static bool json_find_bool(const char* json, const char* key, bool& out) {
	if (json == NULL || key == NULL) return false;
	pfc::string_formatter pattern;
	pattern << "\"" << key << "\":";
	const char* pos = strstr(json, pattern.get_ptr());
	if (pos == NULL) return false;
	pos += pattern.get_length();
	while (*pos == ' ' || *pos == '\t') ++pos;
	if (strncmp(pos, "true", 4) == 0) {
		out = true;
		return true;
	}
	if (strncmp(pos, "false", 5) == 0) {
		out = false;
		return true;
	}
	return false;
}

static bool get_highlight_pending_http(unsigned port, bool* out_enabled) {
	if (out_enabled == NULL) return false;

	pfc::array_t<t_uint8> body;
	if (!http_get_loopback(port, L"/api/highlight", 800, body))
		return false;
	body.append_single('\0');

	bool pending = false;
	bool enabled = false;
	if (!json_find_bool((const char*)body.get_ptr(), "pending", pending) || !pending)
		return false;
	if (!json_find_bool((const char*)body.get_ptr(), "enabled", enabled))
		return false;
	*out_enabled = enabled;
	return true;
}

static bool get_player_command_pending_http(unsigned port, pfc::string8& out_command) {
	out_command.reset();

	pfc::array_t<t_uint8> body;
	if (!http_get_loopback(port, L"/api/player", 800, body))
		return false;
	body.append_single('\0');

	bool pending = false;
	if (!json_find_bool((const char*)body.get_ptr(), "pending", pending) || !pending)
		return false;
	return json_find_string((const char*)body.get_ptr(), "command", out_command);
}

static DWORD WINAPI worker_thread(LPVOID) {
	for (;;) {
		if (WaitForSingleObject(g_event, INFINITE) != WAIT_OBJECT_0) break;
		if (InterlockedCompareExchange(&g_plugin_quitting, 0, 0) != 0) break;

		post_job_t job;
		EnterCriticalSection(&g_cs);
		if (!g_have_pending) {
			LeaveCriticalSection(&g_cs);
			continue;
		}
		job = g_pending;
		g_have_pending = false;
		LeaveCriticalSection(&g_cs);

		if (!post_state_http(job.port, job.json.get_ptr()))
			console::print("Lyrics Web: state POST failed (server running?)");
	}
	return 0;
}

static void ensure_worker() {
	if (g_worker != NULL) return;
	g_event = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (!g_event) return;
	g_worker = CreateThread(NULL, 0, worker_thread, NULL, 0, NULL);
}

} // namespace

namespace web_state_publisher {

void post_async(unsigned port, const char* json_body) {
	if (json_body == NULL || json_body[0] == '\0') return;
	if (InterlockedCompareExchange(&g_plugin_quitting, 0, 0) != 0) return;

	ensure_cs();
	ensure_worker();
	if (!g_worker) return;

	EnterCriticalSection(&g_cs);
	g_pending.port = port;
	g_pending.json = json_body;
	g_have_pending = true;
	LeaveCriticalSection(&g_cs);
	SetEvent(g_event);
}

bool poll_highlight_pending(unsigned port, bool* out_enabled) {
	if (InterlockedCompareExchange(&g_plugin_quitting, 0, 0) != 0)
		return false;
	return get_highlight_pending_http(port, out_enabled);
}

bool poll_player_command_pending(unsigned port, pfc::string8& out_command) {
	if (InterlockedCompareExchange(&g_plugin_quitting, 0, 0) != 0)
		return false;
	return get_player_command_pending_http(port, out_command);
}

bool fetch_server_url(unsigned port, pfc::string8& out_url) {
	out_url.reset();
	if (InterlockedCompareExchange(&g_plugin_quitting, 0, 0) != 0)
		return false;

	pfc::array_t<t_uint8> body;
	if (!http_get_loopback(port, L"/api/info", 1500, body))
		return false;
	body.append_single('\0');
	return json_find_string((const char*)body.get_ptr(), "url", out_url);
}

bool fetch_qr_png(unsigned port, pfc::array_t<t_uint8>& out_png) {
	out_png.set_count(0);
	if (InterlockedCompareExchange(&g_plugin_quitting, 0, 0) != 0)
		return false;
	return http_get_loopback(port, L"/api/qr.png", 3000, out_png);
}

void shutdown() {
	InterlockedExchange(&g_plugin_quitting, 1);
	if (g_event) SetEvent(g_event);
	if (g_worker) {
		WaitForSingleObject(g_worker, 2000);
		CloseHandle(g_worker);
		g_worker = NULL;
	}
	if (g_event) {
		CloseHandle(g_event);
		g_event = NULL;
	}
	if (g_cs_init) {
		DeleteCriticalSection(&g_cs);
		g_cs_init = false;
	}
}

} // namespace
