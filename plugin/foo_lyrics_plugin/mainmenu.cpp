#include "stdafx.h"
#include "guids.h"
#include "lyrics_window.h"

namespace {

static mainmenu_group_popup_factory g_lyrics_menu_group(
	guid_lyrics_menu_group, mainmenu_groups::view, mainmenu_commands::sort_priority_dontcare, "Lyrics AI Translator");

class mainmenu_lyrics : public mainmenu_commands {
public:
	enum { cmd_open = 0, cmd_preferences = 1, cmd_total = 2 };

	t_uint32 get_command_count() override { return cmd_total; }

	GUID get_command(t_uint32 p_index) override {
		switch (p_index) {
		case cmd_open: return guid_lyrics_open_window;
		case cmd_preferences: return guid_lyrics_preferences_menu;
		default: uBugCheck();
		}
	}

	void get_name(t_uint32 p_index, pfc::string_base& p_out) override {
		switch (p_index) {
		case cmd_open: p_out = "Open lyrics window"; break;
		case cmd_preferences: p_out = "Preferences..."; break;
		default: uBugCheck();
		}
	}

	bool get_description(t_uint32 p_index, pfc::string_base& p_out) override {
		switch (p_index) {
		case cmd_open:
			p_out = "Show synced lyrics with AI translation (active while window is open).";
			return true;
		case cmd_preferences:
			p_out = "Open Lyrics AI Translator preferences (language, LLM, proxy, sync, web).";
			return true;
		default:
			return false;
		}
	}

	GUID get_parent() override { return guid_lyrics_menu_group; }

	void execute(t_uint32 p_index, service_ptr_t<service_base>) override {
		switch (p_index) {
		case cmd_open:
			lyrics_window_open_or_focus();
			break;
		case cmd_preferences:
			ui_control::get()->show_preferences(guid_lyrics_preferences_page);
			break;
		default:
			uBugCheck();
		}
	}
};

static service_factory_single_t<mainmenu_lyrics> g_mainmenu_lyrics;

} // namespace
