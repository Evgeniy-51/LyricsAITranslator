#pragma once

struct target_language_t {
	const char* name;
	GUID command_guid;
};

namespace target_languages {
	const char* custom_menu_label();
	t_size preset_count();
	t_size count();
	bool is_custom_menu_index(t_size index);
	bool is_preset_name(const char* name);
	const target_language_t& entry(t_size index);
	int index_for_name(const char* name);
}
