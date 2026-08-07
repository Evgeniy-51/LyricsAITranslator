#include "stdafx.h"
#include "cache_reader.h"

namespace {

pfc::string8 json_unescape(const char* begin, const char* end) {
	pfc::string8 out;
	for (const char* p = begin; p < end; ++p) {
		if (*p == '\\' && p + 1 < end) {
			++p;
			switch (*p) {
			case 'n': out += "\n"; break;
			case 'r': out += "\r"; break;
			case 't': out += "\t"; break;
			case '"': out += "\""; break;
			case '\\': out += "\\"; break;
			default: out.add_byte(*p); break;
			}
		} else {
			out.add_byte(*p);
		}
	}
	return out;
}

bool find_string_field(const char* json, const char* key, pfc::string8& out) {
	pfc::string8 pattern;
	pattern << "\"" << key << "\"";
	const char* pos = strstr(json, pattern.get_ptr());
	if (!pos) return false;
	pos = strchr(pos + pattern.length(), '"');
	if (!pos) return false;
	++pos;
	const char* end = pos;
	while (*end && !(end[0] == '"' && end[-1] != '\\')) ++end;
	out = json_unescape(pos, end);
	return true;
}

bool find_bool_field(const char* json, const char* key, bool& out) {
	pfc::string8 pattern;
	pattern << "\"" << key << "\"";
	const char* pos = strstr(json, pattern.get_ptr());
	if (!pos) return false;
	pos = strchr(pos + pattern.length(), ':');
	if (!pos) return false;
	++pos;
	while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') ++pos;
	if (strncmp(pos, "true", 4) == 0) { out = true; return true; }
	if (strncmp(pos, "false", 5) == 0) { out = false; return true; }
	return false;
}

int find_int_in_range(const char* begin, const char* end, const char* key) {
	pfc::string8 pattern;
	pattern << "\"" << key << "\"";
	const char* pos = strstr(begin, pattern.get_ptr());
	if (!pos || pos >= end) return 0;
	pos = strchr(pos + pattern.length(), ':');
	if (!pos || pos >= end) return 0;
	return atoi(pos + 1);
}

bool find_string_in_range(const char* begin, const char* end, const char* key, pfc::string8& out) {
	pfc::string8 pattern;
	pattern << "\"" << key << "\"";
	const char* pos = strstr(begin, pattern.get_ptr());
	if (!pos || pos >= end) return false;
	pos = strchr(pos + pattern.length(), '"');
	if (!pos || pos >= end) return false;
	++pos;
	const char* e = pos;
	while (e < end && !(e[0] == '"' && e[-1] != '\\')) ++e;
	out = json_unescape(pos, e);
	return true;
}

const char* find_json_object_end(const char* obj_start) {
	if (obj_start == NULL || *obj_start != '{') return NULL;
	int depth = 0;
	bool in_string = false;
	for (const char* p = obj_start; *p; ++p) {
		if (in_string) {
			if (*p == '\\' && p[1]) { ++p; continue; }
			if (*p == '"') in_string = false;
			continue;
		}
		if (*p == '"') { in_string = true; continue; }
		if (*p == '{') ++depth;
		else if (*p == '}') {
			--depth;
			if (depth == 0) return p;
		}
	}
	return NULL;
}

void parse_line_object(const char* begin, const char* end, lyric_line_t& line) {
	pfc::string8 tmp;
	line.index = find_int_in_range(begin, end, "index");
	line.time_ms = find_int_in_range(begin, end, "timeMs");
	if (line.time_ms == 0)
		line.time_ms = find_int_in_range(begin, end, "time_ms");
	if (find_string_in_range(begin, end, "original", tmp)) line.original = tmp;
	if (find_string_in_range(begin, end, "translation", tmp)) line.translation = tmp;
}

static pfc::string8 fb2k_local_path(const char* path) {
	if (path == NULL || path[0] == '\0') return {};
	if (strstr(path, "://") != NULL) return path;
	pfc::string8 out = "file://";
	out += path;
	return out;
}

static bool read_file_bytes(const char* path, pfc::array_t<t_uint8>& data) {
	data.set_size(0);
	const pfc::string8 fb2k_path = fb2k_local_path(path);
	try {
		service_ptr_t<file> f;
		filesystem::g_open_read(f, fb2k_path.get_ptr(), abort_callback_dummy());
		t_filesize size = f->get_size(abort_callback_dummy());
		if (size == foobar2000_io::filesize_invalid || size > 32 * 1024 * 1024) return false;
		data.set_size((t_size)size);
		f->read_object(data.get_ptr(), size, abort_callback_dummy());
		return true;
	} catch (...) {}

	pfc::stringcvt::string_wide_from_utf8 wpath(path);
	HANDLE h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER li = {};
	if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0 || li.QuadPart > 32 * 1024 * 1024) {
		CloseHandle(h);
		return false;
	}
	data.set_size((t_size)li.QuadPart);
	DWORD read = 0;
	const BOOL ok = ReadFile(h, data.get_ptr(), (DWORD)data.get_size(), &read, NULL);
	CloseHandle(h);
	return ok && read == data.get_size();
}

} // namespace

namespace {

static const int kMaxMetadataPasses = 8;

static bool win32_file_exists(const char* path) {
	if (path == NULL || path[0] == '\0') return false;
	const auto wpath = pfc::stringcvt::string_wide_from_utf8(path);
	const DWORD attr = GetFileAttributesW(wpath);
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static pfc::string8 replace_invalid_path_chars(const pfc::string8& in) {
	pfc::string8 out;
	for (t_size i = 0; i < in.length(); ++i) {
		const char c = in[i];
		if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
			out.add_byte('_');
		else
			out.add_byte(c);
	}
	return out;
}

static pfc::string8 compact_spaces(const char* s) {
	pfc::string8 out = pfc::string_trim_spacing(s ? s : "");
	pfc::string8 next;
	bool previous_space = false;
	for (t_size i = 0; i < out.length(); ++i) {
		const char c = out[i];
		if (c == ' ' || c == '\t') {
			if (!previous_space && next.length() > 0)
				next.add_byte(' ');
			previous_space = true;
		} else {
			next.add_byte(c);
			previous_space = false;
		}
	}
	return pfc::string_trim_spacing(next.get_ptr());
}

static bool icontains(const char* haystack, const char* needle) {
	if (haystack == NULL || needle == NULL) return false;
	pfc::string8 h = pfc::stringToLower(haystack);
	pfc::string8 n = pfc::stringToLower(needle);
	return strstr(h.get_ptr(), n.get_ptr()) != NULL;
}

static bool bracket_block_is_noise(const char* inner) {
	if (inner == NULL || inner[0] == '\0') return false;
	return icontains(inner, "disc") || icontains(inner, "cd ") || icontains(inner, "disk") ||
		icontains(inner, "remaster") || icontains(inner, "deluxe") || icontains(inner, "expanded") ||
		icontains(inner, "bonus") || icontains(inner, "remix") || icontains(inner, "radio edit") ||
		icontains(inner, "single version") || icontains(inner, "album version") || icontains(inner, "mono") ||
		icontains(inner, "stereo") || icontains(inner, "live") || icontains(inner, "explicit") ||
		icontains(inner, "clean");
}

static void strip_trailing_bracket_noise(pfc::string8& s) {
	const char* p = s.get_ptr();
	const t_size len = s.length();
	if (len == 0) return;
	const char endc = p[len - 1];
	if (endc != ')' && endc != ']') return;
	const char open = (endc == ')') ? '(' : '[';
	int depth = 0;
	t_size start = SIZE_MAX;
	for (t_size i = len; i-- > 0;) {
		if (p[i] == endc) depth++;
		else if (p[i] == open) {
			depth--;
			if (depth == 0) {
				start = i;
				break;
			}
		}
	}
	if (start == SIZE_MAX || start + 1 >= len - 1) return;
	pfc::string8 inner;
	inner.set_string(p + start + 1, len - start - 2);
	if (!bracket_block_is_noise(inner.get_ptr())) return;
	s.truncate(start);
	s = compact_spaces(s.get_ptr());
}

static void strip_feat_suffix(pfc::string8& s) {
	pfc::string8 lower = pfc::stringToLower(s);
	const char* markers[] = { " feat.", " featuring ", " ft." };
	for (auto marker : markers) {
		const char* pos = strstr(lower.get_ptr(), marker);
		if (pos != NULL) {
			s.truncate((t_size)(pos - lower.get_ptr()));
			s = compact_spaces(s.get_ptr());
			return;
		}
	}
}

static bool starts_with_lower(const pfc::string8& s, const char* prefix) {
	pfc::string8 lower = pfc::stringToLower(s);
	const char* h = lower.get_ptr();
	for (const char* p = prefix; *p; ++p, ++h) {
		if (*h == '\0' || pfc::ascii_tolower(*h) != pfc::ascii_tolower(*p))
			return false;
	}
	return true;
}

static void strip_leading_disc_prefix(pfc::string8& s) {
	const char* prefixes[] = { "disc ", "cd ", "disk " };
	for (auto pref : prefixes) {
		if (!starts_with_lower(s, pref)) continue;
		t_size i = strlen(pref);
		pfc::string8 lower = pfc::stringToLower(s);
		while (i < lower.length() && lower[i] >= '0' && lower[i] <= '9') ++i;
		while (i < lower.length() && (lower[i] == ' ' || lower[i] == '-' || lower[i] == ':' || lower[i] == '.')) ++i;
		if (i > strlen(pref) && i < s.length())
			s = compact_spaces(s.get_ptr() + i);
		return;
	}
}

static pfc::string8 clean_path_metadata(const char* s) {
	pfc::string8 out = compact_spaces(s);
	for (int pass = 0; pass < kMaxMetadataPasses; ++pass) {
		pfc::string8 next = out;
		strip_trailing_bracket_noise(next);
		strip_leading_disc_prefix(next);
		strip_feat_suffix(next);
		next = compact_spaces(next.get_ptr());
		if (next == out) return next;
		out = next;
	}
	return compact_spaces(out.get_ptr());
}

static pfc::string8 primary_path_artist(const char* s) {
	pfc::string8 out = compact_spaces(s);
	const char* seps[] = { ";", " / ", " & " };
	for (auto sep : seps) {
		const char* pos = strstr(out.get_ptr(), sep);
		if (pos != NULL) {
			out.truncate((t_size)(pos - out.get_ptr()));
			return compact_spaces(out.get_ptr());
		}
	}
	return out;
}

struct path_segment_opts_t {
	bool primary_artist = false;
	bool clean_metadata = false;
	bool lowercase = false;
};

static pfc::string8 path_segment_legacy(const char* raw) {
	pfc::string8 out = replace_invalid_path_chars(pfc::string_trim_spacing(raw ? raw : ""));
	if (out.is_empty()) return "_unknown";
	out = pfc::string_trim_spacing(out.get_ptr());
	if (out.is_empty() || out == "." || out == "..") return "_";
	return out;
}

static pfc::string8 finalize_path_segment(const char* raw, path_segment_opts_t opts) {
	pfc::string8 s = compact_spaces(raw);
	if (opts.clean_metadata)
		s = clean_path_metadata(s.get_ptr());
	if (opts.primary_artist)
		s = primary_path_artist(s.get_ptr());
	s = compact_spaces(s.get_ptr());
	if (s.is_empty()) return "_unknown";
	s = replace_invalid_path_chars(s);
	s = compact_spaces(s.get_ptr());
	if (opts.lowercase)
		s = pfc::stringToLower(s);
	s = pfc::string_trim_spacing(s.get_ptr());
	if (s.is_empty() || s == "." || s == "..") return "_";
	return s;
}

static pfc::string8 path_segment_artist(const char* s) {
	path_segment_opts_t opts = {};
	opts.primary_artist = true;
	opts.clean_metadata = true;
	opts.lowercase = true;
	return finalize_path_segment(s, opts);
}

static pfc::string8 path_segment_album_or_title(const char* s) {
	path_segment_opts_t opts = {};
	opts.clean_metadata = true;
	opts.lowercase = true;
	return finalize_path_segment(s, opts);
}

static pfc::string8 build_cache_path(const char* cache_dir, const char* artist, const char* album, const char* title, bool legacy) {
	pfc::string8 path = cache_dir;
	if (legacy) {
		path.add_filename(path_segment_legacy(artist));
		path.add_filename(path_segment_legacy(album));
		path.add_filename(pfc::string_formatter() << path_segment_legacy(title) << ".json");
	} else {
		path.add_filename(path_segment_artist(artist));
		path.add_filename(path_segment_album_or_title(album));
		path.add_filename(pfc::string_formatter() << path_segment_album_or_title(title) << ".json");
	}
	return path;
}

} // namespace

namespace lyrics_cache {

pfc::string8 sanitize_path_component(const char* s) {
	return path_segment_album_or_title(s);
}

pfc::string8 cache_file_path(const char* cache_dir, const char* artist, const char* album, const char* title) {
	return build_cache_path(cache_dir, artist, album, title, false);
}

pfc::string8 cache_file_path_legacy(const char* cache_dir, const char* artist, const char* album, const char* title) {
	return build_cache_path(cache_dir, artist, album, title, true);
}

pfc::string8 resolve_cache_file_path(const char* cache_dir, const char* artist, const char* album, const char* title) {
	const pfc::string8 norm = cache_file_path(cache_dir, artist, album, title);
	if (win32_file_exists(norm.get_ptr())) return norm;
	const pfc::string8 leg = cache_file_path_legacy(cache_dir, artist, album, title);
	if (win32_file_exists(leg.get_ptr())) return leg;
	return norm;
}

bool load_file(const char* path, lyrics_cache_t& out) {
	out = {};
	pfc::array_t<t_uint8> data;
	if (!read_file_bytes(path, data)) return false;
	pfc::string8 json;
	json.set_string(reinterpret_cast<const char*>(data.get_ptr()), data.get_size());

	find_string_field(json.get_ptr(), "status", out.status);
	find_bool_field(json.get_ptr(), "alreadyInTargetLanguage", out.already_in_target_language);

	const char* lyrics_key = "\"lyrics\"";
	const char* arr = strstr(json.get_ptr(), lyrics_key);
	if (!arr) return !out.status.is_empty();
	arr = strchr(arr + strlen(lyrics_key), '[');
	if (!arr) return !out.status.is_empty();
	++arr;

	const char* p = arr;
	while (p && *p && *p != ']') {
		const char* bracket_end = strchr(p, ']');
		const char* obj = strchr(p, '{');
		if (!obj) break;
		if (bracket_end != NULL && obj > bracket_end) break;
		const char* obj_end = find_json_object_end(obj);
		if (!obj_end) break;
		lyric_line_t line;
		parse_line_object(obj, obj_end + 1, line);
		if (!line.original.is_empty() || line.time_ms > 0 || line.index > 0)
			out.lines.append_single(line);
		p = obj_end + 1;
	}
	return true;
}

bool is_ready(const char* path) {
	lyrics_cache_t c;
	if (!load_file(path, c)) return false;
	return stricmp_utf8(c.status.get_ptr(), "ready") == 0;
}

static bool win32_delete_file(const char* path) {
	if (path == NULL || path[0] == '\0') return false;
	const auto wpath = pfc::stringcvt::string_wide_from_utf8(path);
	return DeleteFileW(wpath) != FALSE;
}

bool delete_cache_file(const char* path) {
	return win32_delete_file(path);
}

void clear_session_files(const char* module_dir, const char* expected_cache_path) {
	if (module_dir == NULL || module_dir[0] == '\0') return;
	pfc::string8 err_msg;
	if (load_session_error(module_dir, expected_cache_path, err_msg)) {
		pfc::string8 path = module_dir;
		path.add_filename("run-error.json");
		win32_delete_file(path.get_ptr());
	}
	{
		pfc::string8 path = module_dir;
		path.add_filename("run-display.json");
		win32_delete_file(path.get_ptr());
	}
}

bool load_session_display(const char* module_dir, const char* expected_cache_path, lyrics_cache_t& out) {
	out = {};
	pfc::string8 path = module_dir;
	path.add_filename("run-display.json");
	pfc::array_t<t_uint8> data;
	try {
		service_ptr_t<file> f;
		filesystem::g_open_read(f, path.get_ptr(), abort_callback_dummy());
		t_filesize size = f->get_size(abort_callback_dummy());
		if (size == foobar2000_io::filesize_invalid || size > 4 * 1024 * 1024) return false;
		data.set_size((t_size)size);
		f->read_object(data.get_ptr(), size, abort_callback_dummy());
	} catch (...) {
		return false;
	}
	pfc::string8 json;
	json.set_string(reinterpret_cast<const char*>(data.get_ptr()), data.get_size());

	pfc::string8 cache_path_field;
	if (!find_string_field(json.get_ptr(), "cachePath", cache_path_field)) return false;
	if (stricmp_utf8(cache_path_field.get_ptr(), expected_cache_path) != 0) return false;

	const char* lyrics_key = "\"lyrics\"";
	const char* arr = strstr(json.get_ptr(), lyrics_key);
	if (!arr) return false;
	arr = strchr(arr + strlen(lyrics_key), '[');
	if (!arr) return false;
	++arr;

	const char* p = arr;
	while (p && *p && *p != ']') {
		const char* bracket_end = strchr(p, ']');
		const char* obj = strchr(p, '{');
		if (!obj) break;
		if (bracket_end != NULL && obj > bracket_end) break;
		const char* obj_end = find_json_object_end(obj);
		if (!obj_end) break;
		lyric_line_t line;
		parse_line_object(obj, obj_end + 1, line);
		if (!line.original.is_empty() || line.time_ms > 0 || line.index > 0)
			out.lines.append_single(line);
		p = obj_end + 1;
	}
	out.status = "ready";
	out.already_in_target_language = true;
	return out.lines.get_size() > 0;
}

static bool parse_session_error_json(const char* json, const char* expected_cache_path, bool allow_blank_cache_path,
	pfc::string8& out_message) {
	pfc::string8 cache_path_field;
	if (!find_string_field(json, "cachePath", cache_path_field)) return false;
	if (!find_string_field(json, "userMessage", out_message) || out_message.is_empty()) return false;

	if (cache_path_field.is_empty()) return allow_blank_cache_path;
	if (expected_cache_path == NULL || expected_cache_path[0] == '\0') return false;
	return stricmp_utf8(cache_path_field.get_ptr(), expected_cache_path) == 0;
}

bool load_session_error(const char* module_dir, const char* expected_cache_path, pfc::string8& out_message) {
	out_message.reset();
	pfc::string8 path = module_dir;
	path.add_filename("run-error.json");
	pfc::array_t<t_uint8> data;
	try {
		service_ptr_t<file> f;
		filesystem::g_open_read(f, path.get_ptr(), abort_callback_dummy());
		t_filesize size = f->get_size(abort_callback_dummy());
		if (size == foobar2000_io::filesize_invalid || size > 1024 * 1024) return false;
		data.set_size((t_size)size);
		f->read_object(data.get_ptr(), size, abort_callback_dummy());
	} catch (...) {
		return false;
	}
	pfc::string8 json;
	json.set_string(reinterpret_cast<const char*>(data.get_ptr()), data.get_size());
	if (parse_session_error_json(json.get_ptr(), expected_cache_path, true, out_message))
		return true;
	return false;
}

} // namespace
