#include "stdafx.h"
#include "json_helpers.h"

namespace json_helpers {

pfc::string8 unescape(const char* begin, const char* end) {
	pfc::string8 out;
	if (begin == NULL || end == NULL || begin > end) return out;
	for (const char* p = begin; p < end; ++p) {
		if (*p == '\\' && p + 1 < end) {
			++p;
			switch (*p) {
			case 'n': out += "\n"; break;
			case 'r': out += "\r"; break;
			case 't': out += "\t"; break;
			case '"': out += "\""; break;
			case '\\': out += "\\"; break;
			case '/': out += "/"; break;
			case 'b': out.add_byte('\b'); break;
			case 'f': out.add_byte('\f'); break;
			case 'u': out.add_byte(*p); break;
			default: out.add_byte(*p); break;
			}
		} else {
			out.add_byte(*p);
		}
	}
	return out;
}

bool find_string(const char* json, const char* key, pfc::string8& out) {
	if (json == NULL || key == NULL) return false;
	pfc::string8 pattern;
	pattern << "\"" << key << "\"";
	const char* pos = strstr(json, pattern.get_ptr());
	if (!pos) return false;
	pos = strchr(pos + pattern.length(), '"');
	if (!pos) return false;
	++pos;
	const char* end = pos;
	while (*end && !(end[0] == '"' && end[-1] != '\\')) ++end;
	out = unescape(pos, end);
	return true;
}

} // namespace json_helpers
