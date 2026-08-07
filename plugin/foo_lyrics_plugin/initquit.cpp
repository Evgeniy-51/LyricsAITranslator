#include "stdafx.h"
#include "plugin_config.h"
#include "git_sync.h"
#include "web_server_launcher.h"
#include "web_state_publisher.h"

namespace {
	class lyrics_initquit : public initquit {
	public:
		void on_init() {
#ifdef LYRICS_SMOKE_BUILD
			console::print("Lyrics AI Translator loaded (smoke build)");
#else
			console::print("Lyrics AI Translator loaded");
			plugin_settings_t settings;
			const bool had_config = plugin_config::load(settings);
			if (had_config)
				git_sync::schedule_startup_pull(settings);
			if (settings.web_enabled)
				web_server_launcher::ensure_running(settings);
#endif
		}
		void on_quit() {
#ifdef LYRICS_SMOKE_BUILD
			console::print("Lyrics AI Translator unloaded (smoke build)");
#else
			web_state_publisher::shutdown();
			web_server_launcher::stop();
			console::print("Lyrics AI Translator unloaded");
#endif
		}
	};
	FB2K_SERVICE_FACTORY(lyrics_initquit);
}
