#include "stdafx.h"
#include "target_languages.h"
#include "guids.h"

namespace {

static const char* kCustomMenuLabel = "Custom language...";

static const target_language_t kPresets[] = {
	{ "Русский", guid_lang_ru },
	{ "English", guid_lang_en },
	{ "中文", guid_lang_zh },
	{ "Español", guid_lang_es },
	{ "Deutsch", guid_lang_de },
	{ "Français", guid_lang_fr },
};

static const target_language_t kCustomEntry = { kCustomMenuLabel, guid_lang_custom };

} // namespace

namespace target_languages {

const char* custom_menu_label() { return kCustomMenuLabel; }

t_size preset_count() { return PFC_TABSIZE(kPresets); }

t_size count() { return preset_count() + 1; }

bool is_custom_menu_index(t_size index) { return index == preset_count(); }

bool is_preset_name(const char* name) {
	return index_for_name(name) >= 0;
}

const target_language_t& entry(t_size index) {
	if (is_custom_menu_index(index)) return kCustomEntry;
	PFC_ASSERT(index < preset_count());
	return kPresets[index];
}

int index_for_name(const char* name) {
	if (name == NULL) return -1;
	for (t_size i = 0; i < preset_count(); ++i) {
		if (stricmp_utf8(kPresets[i].name, name) == 0)
			return (int)i;
	}
	return -1;
}

} // namespace
