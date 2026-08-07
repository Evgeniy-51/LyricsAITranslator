#pragma once

namespace json_helpers {
	pfc::string8 unescape(const char* begin, const char* end);
	bool find_string(const char* json, const char* key, pfc::string8& out);
}
