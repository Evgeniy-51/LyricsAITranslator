#include "stdafx.h"
#include "resource.h"
#include "guids.h"
#include "plugin_config.h"
#include "target_languages.h"
#include "lyrics_window.h"
#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>
#include <shellapi.h>
#include <shlobj.h>

namespace {

static COLORREF fb2k_ui_color(int sys_index) {
	auto api = ui_config_manager::tryGet();
	if (api.is_valid()) return api->getSysColor(sys_index);
	return ::GetSysColor(sys_index);
}

static bool effective_dark_ui() {
	auto api = ui_config_manager::tryGet();
	if (!api.is_valid()) return fb2k::isDarkMode();
	if (api->is_dark_mode()) return true;
	return DarkMode::IsThemeDark(fb2k_ui_color(COLOR_WINDOWTEXT), fb2k_ui_color(COLOR_WINDOW));
}

class CCustomLangDialog : public CDialogImpl<CCustomLangDialog> {
public:
	enum { IDD = IDD_CUSTOM_LANG };
	pfc::string8 m_initial;
	pfc::string8 m_result;

	BEGIN_MSG_MAP_EX(CCustomLangDialog)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnOk)
		COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnCancel)
	END_MSG_MAP()

private:
	fb2k::CDarkModeHooks m_dark;

	BOOL OnInitDialog(CWindow, LPARAM) {
		m_dark.AddDialogWithControls(m_hWnd);
		m_dark.SetDark(effective_dark_ui());
		if (!m_initial.is_empty())
			uSetDlgItemText(*this, IDC_EDIT_CUSTOM_LANG, m_initial.get_ptr());
		::SetFocus(GetDlgItem(IDC_EDIT_CUSTOM_LANG));
		return TRUE;
	}

	void OnOk(UINT, int, CWindow) {
		uGetDlgItemText(*this, IDC_EDIT_CUSTOM_LANG, m_result);
		m_result = pfc::string_trim_spacing(m_result.get_ptr());
		if (m_result.is_empty()) {
			popup_message::g_show("Enter a language name.", "Lyrics AI Translator");
			return;
		}
		EndDialog(IDOK);
	}

	void OnCancel(UINT, int, CWindow) { EndDialog(IDCANCEL); }
};

static bool prompt_custom_language(HWND parent, pfc::string8& inout) {
	CCustomLangDialog dlg;
	dlg.m_initial = inout;
	if (dlg.DoModal(parent) != IDOK) return false;
	inout = dlg.m_result;
	return true;
}

static pfc::string8 dlg_text(CWindow dlg, int id) {
	pfc::string8 out;
	uGetDlgItemText(dlg, id, out);
	return pfc::string8(pfc::string_trim_spacing(out.get_ptr()));
}

static void set_dlg_text(CWindow dlg, int id, const char* text) {
	uSetDlgItemText(dlg, id, text != NULL ? text : "");
}

static bool dlg_checked(CWindow dlg, int id) {
	return IsDlgButtonChecked(dlg, id) == BST_CHECKED;
}

static void set_dlg_checked(CWindow dlg, int id, bool on) {
	CheckDlgButton(dlg, id, on ? BST_CHECKED : BST_UNCHECKED);
}

static void enable_ids(CWindow dlg, const int* ids, size_t count, bool enabled) {
	for (size_t i = 0; i < count; ++i)
		::EnableWindow(dlg.GetDlgItem(ids[i]), enabled ? TRUE : FALSE);
}

static pfc::string8 generate_auth_token() {
	GUID g = {};
	pfc::string_formatter out;
	if (SUCCEEDED(CoCreateGuid(&g))) {
		out << pfc::format_hex((unsigned)g.Data1, 8)
			<< pfc::format_hex((unsigned)g.Data2, 4)
			<< pfc::format_hex((unsigned)g.Data3, 4);
		for (int i = 0; i < 8; ++i)
			out << pfc::format_hex((unsigned)g.Data4[i], 2);
		return out;
	}
	out << "lyrics-" << (unsigned)GetTickCount();
	return out;
}

static void open_config_folder(HWND parent) {
	pfc::string8 dir;
	if (!plugin_config::get_module_dir(dir)) {
		popup_message::g_show("Cannot resolve plugin folder.", "Lyrics AI Translator");
		return;
	}
	const auto wide = pfc::stringcvt::string_wide_from_utf8(dir.get_ptr());
	ShellExecuteW(parent, L"open", wide.get_ptr(), NULL, NULL, SW_SHOWNORMAL);
}

static pfc::string8 cache_path_for_ui(const pfc::string8& abs_or_rel) {
	pfc::string8 mod;
	if (!plugin_config::get_module_dir(mod)) return abs_or_rel;
	pfc::string8 prefix = mod;
	prefix += "\\";
	if (abs_or_rel.length() > prefix.length()
		&& stricmp_utf8_partial(abs_or_rel.get_ptr(), prefix.get_ptr()) == 0)
		return pfc::string8(abs_or_rel.get_ptr() + prefix.length());
	return abs_or_rel;
}

static bool browse_for_folder(HWND parent, pfc::string8& out_path) {
	wchar_t display[MAX_PATH] = {};
	BROWSEINFOW bi = {};
	bi.hwndOwner = parent;
	bi.pszDisplayName = display;
	bi.lpszTitle = L"Select lyrics cache folder";
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
	if (pidl == NULL) return false;
	wchar_t folder[MAX_PATH] = {};
	const BOOL ok = SHGetPathFromIDListW(pidl, folder);
	CoTaskMemFree(pidl);
	if (!ok || folder[0] == L'\0') return false;
	out_path = pfc::stringcvt::string_utf8_from_wide(folder).get_ptr();
	return true;
}

static bool save_and_reload(const plugin_settings_t& s) {
	if (!plugin_config::save(s)) {
		popup_message::g_show("Could not write config.json next to the plugin DLL.", "Lyrics AI Translator");
		return false;
	}
	lyrics_window_reload_settings();
	return true;
}

static plugin_settings_t make_defaults() {
	plugin_settings_t defaults;
	pfc::string8 dir;
	plugin_config::get_module_dir(dir);
	plugin_config::apply_defaults(defaults, dir.get_ptr());
	defaults.enable_translation = true;
	defaults.proxy_enabled = true;
	defaults.proxy_type = "socks5";
	defaults.sync_enabled = false;
	defaults.web_enabled = true;
	defaults.sync_pull_on_startup = true;
	defaults.sync_auto_setup = true;
	return defaults;
}

static t_uint32 prefs_state(bool changed) {
	t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
	if (changed) state |= preferences_state::changed;
	return state;
}

// --- Language ---

class CLyricsPrefsLanguage : public CDialogImpl<CLyricsPrefsLanguage>, public preferences_page_instance {
public:
	CLyricsPrefsLanguage(preferences_page_callback::ptr callback) : m_callback(callback) {}
	enum { IDD = IDD_PREFERENCES };

	t_uint32 get_state() override { return prefs_state(HasChanged()); }
	void apply() override;
	void reset() override;

	BEGIN_MSG_MAP_EX(CLyricsPrefsLanguage)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDC_PREF_ENABLE_TRANSLATION, BN_CLICKED, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_TARGET_LANG, CBN_SELCHANGE, OnLangSelChange)
		COMMAND_HANDLER_EX(IDC_PREF_OPEN_CONFIG_DIR, BN_CLICKED, OnOpenConfigDir)
	END_MSG_MAP()

private:
	BOOL OnInitDialog(CWindow, LPARAM);
	void OnChangedCmd(UINT, int, CWindow);
	void OnLangSelChange(UINT, int, CWindow);
	void OnOpenConfigDir(UINT, int, CWindow) { open_config_folder(m_hWnd); }
	void OnChanged() { m_callback->on_state_changed(); }
	bool HasChanged();
	void FillFromSettings(const plugin_settings_t& s);
	void FillLanguageCombo(const plugin_settings_t& s);
	pfc::string8 SelectedLanguage() const;

	const preferences_page_callback::ptr m_callback;
	fb2k::CDarkModeHooks m_dark;
	plugin_settings_t m_baseline;
	pfc::string8 m_custom_lang;
	bool m_suppress_change = false;
};

BOOL CLyricsPrefsLanguage::OnInitDialog(CWindow, LPARAM) {
	m_dark.AddDialogWithControls(*this);
	m_dark.SetDark(effective_dark_ui());
	plugin_config::load(m_baseline);
	m_suppress_change = true;
	FillFromSettings(m_baseline);
	m_suppress_change = false;
	return FALSE;
}

void CLyricsPrefsLanguage::FillLanguageCombo(const plugin_settings_t& s) {
	CWindow combo = GetDlgItem(IDC_PREF_TARGET_LANG);
	uSendMessage(combo, CB_RESETCONTENT, 0, 0);
	for (t_size i = 0; i < target_languages::preset_count(); ++i)
		uSendMessageText(combo, CB_ADDSTRING, 0, target_languages::entry(i).name);

	m_custom_lang.reset();
	const int preset = target_languages::index_for_name(s.target_lang.get_ptr());
	if (preset >= 0) {
		uSendMessage(combo, CB_SETCURSEL, (WPARAM)preset, 0);
	} else {
		m_custom_lang = s.target_lang;
		pfc::string8 label = m_custom_lang;
		if (label.is_empty()) label = target_languages::custom_menu_label();
		else label << " (custom)";
		const LRESULT idx = uSendMessageText(combo, CB_ADDSTRING, 0, label.get_ptr());
		uSendMessage(combo, CB_SETCURSEL, (WPARAM)idx, 0);
	}
	uSendMessageText(combo, CB_ADDSTRING, 0, target_languages::custom_menu_label());
}

void CLyricsPrefsLanguage::FillFromSettings(const plugin_settings_t& s) {
	set_dlg_checked(*this, IDC_PREF_ENABLE_TRANSLATION, s.enable_translation);
	FillLanguageCombo(s);
}

pfc::string8 CLyricsPrefsLanguage::SelectedLanguage() const {
	CWindow combo = GetDlgItem(IDC_PREF_TARGET_LANG);
	const int sel = (int)uSendMessage(combo, CB_GETCURSEL, 0, 0);
	if (sel < 0) return m_baseline.target_lang;
	const int count = (int)uSendMessage(combo, CB_GETCOUNT, 0, 0);
	if (count > 0 && sel == count - 1) {
		if (!m_custom_lang.is_empty()) return m_custom_lang;
		return m_baseline.target_lang;
	}
	if ((t_size)sel < target_languages::preset_count())
		return target_languages::entry((t_size)sel).name;
	if (!m_custom_lang.is_empty()) return m_custom_lang;
	return m_baseline.target_lang;
}

void CLyricsPrefsLanguage::OnChangedCmd(UINT, int, CWindow) {
	if (!m_suppress_change) OnChanged();
}

void CLyricsPrefsLanguage::OnLangSelChange(UINT, int, CWindow) {
	if (m_suppress_change) return;
	CWindow combo = GetDlgItem(IDC_PREF_TARGET_LANG);
	const int sel = (int)uSendMessage(combo, CB_GETCURSEL, 0, 0);
	const int count = (int)uSendMessage(combo, CB_GETCOUNT, 0, 0);
	if (sel < 0 || count <= 0) return;

	if (sel == count - 1) {
		pfc::string8 value = m_custom_lang;
		if (target_languages::is_preset_name(value.get_ptr())) value.reset();
		if (!prompt_custom_language(m_hWnd, value)) {
			m_suppress_change = true;
			plugin_settings_t tmp = m_baseline;
			tmp.target_lang = m_custom_lang.is_empty() ? m_baseline.target_lang : m_custom_lang;
			FillLanguageCombo(tmp);
			m_suppress_change = false;
			return;
		}
		m_custom_lang = value;
		plugin_settings_t tmp = m_baseline;
		tmp.target_lang = value;
		m_suppress_change = true;
		FillLanguageCombo(tmp);
		m_suppress_change = false;
	} else if ((t_size)sel < target_languages::preset_count()) {
		m_custom_lang.reset();
	}
	OnChanged();
}

bool CLyricsPrefsLanguage::HasChanged() {
	return dlg_checked(*this, IDC_PREF_ENABLE_TRANSLATION) != m_baseline.enable_translation
		|| stricmp_utf8(SelectedLanguage().get_ptr(), m_baseline.target_lang.get_ptr()) != 0;
}

void CLyricsPrefsLanguage::reset() {
	m_suppress_change = true;
	FillFromSettings(make_defaults());
	m_suppress_change = false;
	OnChanged();
}

void CLyricsPrefsLanguage::apply() {
	plugin_settings_t s;
	plugin_config::load(s);
	s.enable_translation = dlg_checked(*this, IDC_PREF_ENABLE_TRANSLATION);
	s.target_lang = SelectedLanguage();
	if (!save_and_reload(s)) return;
	plugin_config::load(m_baseline);
	m_suppress_change = true;
	FillFromSettings(m_baseline);
	m_suppress_change = false;
	OnChanged();
}

// --- LLM ---

class CLyricsPrefsLLM : public CDialogImpl<CLyricsPrefsLLM>, public preferences_page_instance {
public:
	CLyricsPrefsLLM(preferences_page_callback::ptr callback) : m_callback(callback) {}
	enum { IDD = IDD_PREFERENCES_LLM };

	t_uint32 get_state() override { return prefs_state(HasChanged()); }
	void apply() override;
	void reset() override;

	BEGIN_MSG_MAP_EX(CLyricsPrefsLLM)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDC_PREF_LLM_BASE, EN_CHANGE, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_LLM_MODEL, EN_CHANGE, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_LLM_KEY, EN_CHANGE, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_OPEN_CONFIG_DIR_LLM, BN_CLICKED, OnOpenConfigDir)
	END_MSG_MAP()

private:
	BOOL OnInitDialog(CWindow, LPARAM);
	void OnChangedCmd(UINT, int, CWindow) { if (!m_suppress_change) m_callback->on_state_changed(); }
	void OnOpenConfigDir(UINT, int, CWindow) { open_config_folder(m_hWnd); }
	bool HasChanged();
	void FillFromSettings(const plugin_settings_t& s);

	const preferences_page_callback::ptr m_callback;
	fb2k::CDarkModeHooks m_dark;
	plugin_settings_t m_baseline;
	bool m_suppress_change = false;
};

BOOL CLyricsPrefsLLM::OnInitDialog(CWindow, LPARAM) {
	m_dark.AddDialogWithControls(*this);
	m_dark.SetDark(effective_dark_ui());
	plugin_config::load(m_baseline);
	m_suppress_change = true;
	FillFromSettings(m_baseline);
	m_suppress_change = false;
	return FALSE;
}

void CLyricsPrefsLLM::FillFromSettings(const plugin_settings_t& s) {
	set_dlg_text(*this, IDC_PREF_LLM_BASE, s.llm_base_url.get_ptr());
	set_dlg_text(*this, IDC_PREF_LLM_MODEL, s.llm_model.get_ptr());
	set_dlg_text(*this, IDC_PREF_LLM_KEY, s.llm_api_key.get_ptr());
}

bool CLyricsPrefsLLM::HasChanged() {
	return stricmp_utf8(dlg_text(*this, IDC_PREF_LLM_BASE).get_ptr(), m_baseline.llm_base_url.get_ptr()) != 0
		|| stricmp_utf8(dlg_text(*this, IDC_PREF_LLM_MODEL).get_ptr(), m_baseline.llm_model.get_ptr()) != 0
		|| strcmp(dlg_text(*this, IDC_PREF_LLM_KEY).get_ptr(), m_baseline.llm_api_key.get_ptr()) != 0;
}

void CLyricsPrefsLLM::reset() {
	m_suppress_change = true;
	FillFromSettings(make_defaults());
	m_suppress_change = false;
	m_callback->on_state_changed();
}

void CLyricsPrefsLLM::apply() {
	plugin_settings_t s;
	plugin_config::load(s);
	s.llm_base_url = dlg_text(*this, IDC_PREF_LLM_BASE);
	s.llm_model = dlg_text(*this, IDC_PREF_LLM_MODEL);
	s.llm_api_key = dlg_text(*this, IDC_PREF_LLM_KEY);
	if (!save_and_reload(s)) return;
	plugin_config::load(m_baseline);
	m_suppress_change = true;
	FillFromSettings(m_baseline);
	m_suppress_change = false;
	m_callback->on_state_changed();
}

// --- Proxy ---

class CLyricsPrefsProxy : public CDialogImpl<CLyricsPrefsProxy>, public preferences_page_instance {
public:
	CLyricsPrefsProxy(preferences_page_callback::ptr callback) : m_callback(callback) {}
	enum { IDD = IDD_PREFERENCES_PROXY };

	t_uint32 get_state() override { return prefs_state(HasChanged()); }
	void apply() override;
	void reset() override;

	BEGIN_MSG_MAP_EX(CLyricsPrefsProxy)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDC_PREF_PROXY_ENABLED, BN_CLICKED, OnToggle)
		COMMAND_HANDLER_EX(IDC_PREF_PROXY_TYPE_SOCKS5, BN_CLICKED, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_PROXY_TYPE_HTTP, BN_CLICKED, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_PROXY_URL, EN_CHANGE, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_PROXY_PORT, EN_CHANGE, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_PROXY_USER, EN_CHANGE, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_PROXY_PASS, EN_CHANGE, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_OPEN_CONFIG_DIR_PROXY, BN_CLICKED, OnOpenConfigDir)
	END_MSG_MAP()

private:
	BOOL OnInitDialog(CWindow, LPARAM);
	void OnChangedCmd(UINT, int, CWindow) { if (!m_suppress_change) m_callback->on_state_changed(); }
	void OnToggle(UINT, int, CWindow);
	void OnOpenConfigDir(UINT, int, CWindow) { open_config_folder(m_hWnd); }
	bool HasChanged();
	void FillFromSettings(const plugin_settings_t& s);
	void UpdateDependentEnables();
	pfc::string8 dlg_proxy_type() const;

	const preferences_page_callback::ptr m_callback;
	fb2k::CDarkModeHooks m_dark;
	plugin_settings_t m_baseline;
	bool m_suppress_change = false;
};

BOOL CLyricsPrefsProxy::OnInitDialog(CWindow, LPARAM) {
	m_dark.AddDialogWithControls(*this);
	m_dark.SetDark(effective_dark_ui());
	plugin_config::load(m_baseline);
	m_suppress_change = true;
	FillFromSettings(m_baseline);
	m_suppress_change = false;
	UpdateDependentEnables();
	return FALSE;
}

pfc::string8 CLyricsPrefsProxy::dlg_proxy_type() const {
	if (IsDlgButtonChecked(IDC_PREF_PROXY_TYPE_HTTP) == BST_CHECKED)
		return "http";
	return "socks5";
}

void CLyricsPrefsProxy::FillFromSettings(const plugin_settings_t& s) {
	set_dlg_checked(*this, IDC_PREF_PROXY_ENABLED, s.proxy_enabled);
	const bool http = stricmp_utf8(s.proxy_type.get_ptr(), "http") == 0
		|| stricmp_utf8(s.proxy_type.get_ptr(), "https") == 0;
	CheckRadioButton(IDC_PREF_PROXY_TYPE_SOCKS5, IDC_PREF_PROXY_TYPE_HTTP,
		http ? IDC_PREF_PROXY_TYPE_HTTP : IDC_PREF_PROXY_TYPE_SOCKS5);
	set_dlg_text(*this, IDC_PREF_PROXY_URL, s.proxy_url.get_ptr());
	set_dlg_text(*this, IDC_PREF_PROXY_PORT, s.proxy_port.get_ptr());
	set_dlg_text(*this, IDC_PREF_PROXY_USER, s.proxy_user.get_ptr());
	set_dlg_text(*this, IDC_PREF_PROXY_PASS, s.proxy_pass.get_ptr());
}

void CLyricsPrefsProxy::UpdateDependentEnables() {
	static const int kIds[] = {
		IDC_PREF_LBL_PROXY_TYPE, IDC_PREF_PROXY_TYPE_SOCKS5, IDC_PREF_PROXY_TYPE_HTTP,
		IDC_PREF_PROXY_URL, IDC_PREF_PROXY_PORT, IDC_PREF_PROXY_USER, IDC_PREF_PROXY_PASS,
		IDC_PREF_LBL_PROXY_URL, IDC_PREF_LBL_PROXY_PORT, IDC_PREF_LBL_PROXY_USER, IDC_PREF_LBL_PROXY_PASS
	};
	enable_ids(*this, kIds, PFC_TABSIZE(kIds), dlg_checked(*this, IDC_PREF_PROXY_ENABLED));
}

void CLyricsPrefsProxy::OnToggle(UINT, int, CWindow) {
	UpdateDependentEnables();
	OnChangedCmd(0, 0, NULL);
}

bool CLyricsPrefsProxy::HasChanged() {
	return dlg_checked(*this, IDC_PREF_PROXY_ENABLED) != m_baseline.proxy_enabled
		|| stricmp_utf8(dlg_proxy_type().get_ptr(), m_baseline.proxy_type.get_ptr()) != 0
		|| stricmp_utf8(dlg_text(*this, IDC_PREF_PROXY_URL).get_ptr(), m_baseline.proxy_url.get_ptr()) != 0
		|| stricmp_utf8(dlg_text(*this, IDC_PREF_PROXY_PORT).get_ptr(), m_baseline.proxy_port.get_ptr()) != 0
		|| strcmp(dlg_text(*this, IDC_PREF_PROXY_USER).get_ptr(), m_baseline.proxy_user.get_ptr()) != 0
		|| strcmp(dlg_text(*this, IDC_PREF_PROXY_PASS).get_ptr(), m_baseline.proxy_pass.get_ptr()) != 0;
}

void CLyricsPrefsProxy::reset() {
	m_suppress_change = true;
	FillFromSettings(make_defaults());
	m_suppress_change = false;
	UpdateDependentEnables();
	m_callback->on_state_changed();
}

void CLyricsPrefsProxy::apply() {
	plugin_settings_t s;
	plugin_config::load(s);
	s.proxy_enabled = dlg_checked(*this, IDC_PREF_PROXY_ENABLED);
	s.proxy_type = dlg_proxy_type();
	s.proxy_url = dlg_text(*this, IDC_PREF_PROXY_URL);
	s.proxy_port = dlg_text(*this, IDC_PREF_PROXY_PORT);
	s.proxy_user = dlg_text(*this, IDC_PREF_PROXY_USER);
	s.proxy_pass = dlg_text(*this, IDC_PREF_PROXY_PASS);
	if (!save_and_reload(s)) return;
	plugin_config::load(m_baseline);
	m_suppress_change = true;
	FillFromSettings(m_baseline);
	m_suppress_change = false;
	UpdateDependentEnables();
	m_callback->on_state_changed();
}

// --- Cache (local + optional Git sync) ---

class CLyricsPrefsCache : public CDialogImpl<CLyricsPrefsCache>, public preferences_page_instance {
public:
	CLyricsPrefsCache(preferences_page_callback::ptr callback) : m_callback(callback) {}
	enum { IDD = IDD_PREFERENCES_CACHE };

	t_uint32 get_state() override { return prefs_state(HasChanged()); }
	void apply() override;
	void reset() override;

	BEGIN_MSG_MAP_EX(CLyricsPrefsCache)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDC_PREF_CACHE_DIR, EN_CHANGE, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_CACHE_BROWSE, BN_CLICKED, OnBrowse)
		COMMAND_HANDLER_EX(IDC_PREF_SYNC_ENABLED, BN_CLICKED, OnToggle)
		COMMAND_HANDLER_EX(IDC_PREF_SYNC_PULL_STARTUP, BN_CLICKED, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_SYNC_AUTO_SETUP, BN_CLICKED, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_SYNC_REMOTE, EN_CHANGE, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_SYNC_PAT, EN_CHANGE, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_SYNC_BRANCH, EN_CHANGE, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_OPEN_CONFIG_DIR_CACHE, BN_CLICKED, OnOpenConfigDir)
	END_MSG_MAP()

private:
	BOOL OnInitDialog(CWindow, LPARAM);
	void OnChangedCmd(UINT, int, CWindow) { if (!m_suppress_change) m_callback->on_state_changed(); }
	void OnToggle(UINT, int, CWindow);
	void OnBrowse(UINT, int, CWindow);
	void OnOpenConfigDir(UINT, int, CWindow) { open_config_folder(m_hWnd); }
	bool HasChanged();
	void FillFromSettings(const plugin_settings_t& s);
	void UpdateDependentEnables();
	pfc::string8 BaselineCacheUi() const { return cache_path_for_ui(m_baseline.cache_dir); }

	const preferences_page_callback::ptr m_callback;
	fb2k::CDarkModeHooks m_dark;
	plugin_settings_t m_baseline;
	bool m_suppress_change = false;
};

BOOL CLyricsPrefsCache::OnInitDialog(CWindow, LPARAM) {
	m_dark.AddDialogWithControls(*this);
	m_dark.SetDark(effective_dark_ui());
	plugin_config::load(m_baseline);
	m_suppress_change = true;
	FillFromSettings(m_baseline);
	m_suppress_change = false;
	UpdateDependentEnables();
	return FALSE;
}

void CLyricsPrefsCache::FillFromSettings(const plugin_settings_t& s) {
	set_dlg_text(*this, IDC_PREF_CACHE_DIR, cache_path_for_ui(s.cache_dir).get_ptr());
	set_dlg_checked(*this, IDC_PREF_SYNC_ENABLED, s.sync_enabled);
	set_dlg_text(*this, IDC_PREF_SYNC_REMOTE, s.sync_remote_url.get_ptr());
	set_dlg_text(*this, IDC_PREF_SYNC_PAT, s.sync_pat.get_ptr());
	set_dlg_text(*this, IDC_PREF_SYNC_BRANCH, s.sync_branch.get_ptr());
	set_dlg_checked(*this, IDC_PREF_SYNC_PULL_STARTUP, s.sync_pull_on_startup);
	set_dlg_checked(*this, IDC_PREF_SYNC_AUTO_SETUP, s.sync_auto_setup);
}

void CLyricsPrefsCache::UpdateDependentEnables() {
	static const int kIds[] = {
		IDC_PREF_SYNC_REMOTE, IDC_PREF_SYNC_PAT, IDC_PREF_SYNC_BRANCH,
		IDC_PREF_SYNC_PULL_STARTUP, IDC_PREF_SYNC_AUTO_SETUP,
		IDC_PREF_LBL_SYNC_REMOTE, IDC_PREF_LBL_SYNC_PAT, IDC_PREF_LBL_SYNC_BRANCH
	};
	enable_ids(*this, kIds, PFC_TABSIZE(kIds), dlg_checked(*this, IDC_PREF_SYNC_ENABLED));
}

void CLyricsPrefsCache::OnToggle(UINT, int, CWindow) {
	UpdateDependentEnables();
	OnChangedCmd(0, 0, NULL);
}

void CLyricsPrefsCache::OnBrowse(UINT, int, CWindow) {
	pfc::string8 path;
	if (!browse_for_folder(m_hWnd, path)) return;
	set_dlg_text(*this, IDC_PREF_CACHE_DIR, cache_path_for_ui(path).get_ptr());
	OnChangedCmd(0, 0, NULL);
}

bool CLyricsPrefsCache::HasChanged() {
	return stricmp_utf8(dlg_text(*this, IDC_PREF_CACHE_DIR).get_ptr(), BaselineCacheUi().get_ptr()) != 0
		|| dlg_checked(*this, IDC_PREF_SYNC_ENABLED) != m_baseline.sync_enabled
		|| stricmp_utf8(dlg_text(*this, IDC_PREF_SYNC_REMOTE).get_ptr(), m_baseline.sync_remote_url.get_ptr()) != 0
		|| strcmp(dlg_text(*this, IDC_PREF_SYNC_PAT).get_ptr(), m_baseline.sync_pat.get_ptr()) != 0
		|| stricmp_utf8(dlg_text(*this, IDC_PREF_SYNC_BRANCH).get_ptr(), m_baseline.sync_branch.get_ptr()) != 0
		|| dlg_checked(*this, IDC_PREF_SYNC_PULL_STARTUP) != m_baseline.sync_pull_on_startup
		|| dlg_checked(*this, IDC_PREF_SYNC_AUTO_SETUP) != m_baseline.sync_auto_setup;
}

void CLyricsPrefsCache::reset() {
	m_suppress_change = true;
	FillFromSettings(make_defaults());
	m_suppress_change = false;
	UpdateDependentEnables();
	m_callback->on_state_changed();
}

void CLyricsPrefsCache::apply() {
	plugin_settings_t s;
	plugin_config::load(s);
	s.cache_dir = dlg_text(*this, IDC_PREF_CACHE_DIR);
	if (s.cache_dir.is_empty()) s.cache_dir = "temp";
	s.sync_repo_dir = s.cache_dir;
	s.sync_enabled = dlg_checked(*this, IDC_PREF_SYNC_ENABLED);
	s.sync_remote_url = dlg_text(*this, IDC_PREF_SYNC_REMOTE);
	s.sync_pat = dlg_text(*this, IDC_PREF_SYNC_PAT);
	s.sync_branch = dlg_text(*this, IDC_PREF_SYNC_BRANCH);
	s.sync_pull_on_startup = dlg_checked(*this, IDC_PREF_SYNC_PULL_STARTUP);
	s.sync_auto_setup = dlg_checked(*this, IDC_PREF_SYNC_AUTO_SETUP);
	if (!save_and_reload(s)) return;
	plugin_config::load(m_baseline);
	m_suppress_change = true;
	FillFromSettings(m_baseline);
	m_suppress_change = false;
	UpdateDependentEnables();
	m_callback->on_state_changed();
}

// --- Web ---
class CLyricsPrefsWeb : public CDialogImpl<CLyricsPrefsWeb>, public preferences_page_instance {
public:
	CLyricsPrefsWeb(preferences_page_callback::ptr callback) : m_callback(callback) {}
	enum { IDD = IDD_PREFERENCES_WEB };

	t_uint32 get_state() override { return prefs_state(HasChanged()); }
	void apply() override;
	void reset() override;

	BEGIN_MSG_MAP_EX(CLyricsPrefsWeb)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDC_PREF_WEB_ENABLED, BN_CLICKED, OnToggle)
		COMMAND_HANDLER_EX(IDC_PREF_GEN_WEB_TOKEN, BN_CLICKED, OnGenerateToken)
		COMMAND_HANDLER_EX(IDC_PREF_WEB_PORT, EN_CHANGE, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_WEB_TOKEN, EN_CHANGE, OnChangedCmd)
		COMMAND_HANDLER_EX(IDC_PREF_OPEN_CONFIG_DIR_WEB, BN_CLICKED, OnOpenConfigDir)
	END_MSG_MAP()

private:
	BOOL OnInitDialog(CWindow, LPARAM);
	void OnChangedCmd(UINT, int, CWindow) { if (!m_suppress_change) m_callback->on_state_changed(); }
	void OnToggle(UINT, int, CWindow);
	void OnGenerateToken(UINT, int, CWindow);
	void OnOpenConfigDir(UINT, int, CWindow) { open_config_folder(m_hWnd); }
	bool HasChanged();
	void FillFromSettings(const plugin_settings_t& s);
	void UpdateDependentEnables();

	const preferences_page_callback::ptr m_callback;
	fb2k::CDarkModeHooks m_dark;
	plugin_settings_t m_baseline;
	bool m_suppress_change = false;
};

BOOL CLyricsPrefsWeb::OnInitDialog(CWindow, LPARAM) {
	m_dark.AddDialogWithControls(*this);
	m_dark.SetDark(effective_dark_ui());
	plugin_config::load(m_baseline);
	m_suppress_change = true;
	FillFromSettings(m_baseline);
	m_suppress_change = false;
	UpdateDependentEnables();
	return FALSE;
}

void CLyricsPrefsWeb::FillFromSettings(const plugin_settings_t& s) {
	set_dlg_checked(*this, IDC_PREF_WEB_ENABLED, s.web_enabled);
	SetDlgItemInt(IDC_PREF_WEB_PORT, s.web_port, FALSE);
	set_dlg_text(*this, IDC_PREF_WEB_TOKEN, s.web_auth_token.get_ptr());
}

void CLyricsPrefsWeb::UpdateDependentEnables() {
	static const int kIds[] = {
		IDC_PREF_WEB_PORT, IDC_PREF_WEB_TOKEN, IDC_PREF_GEN_WEB_TOKEN,
		IDC_PREF_LBL_WEB_PORT, IDC_PREF_LBL_WEB_TOKEN
	};
	enable_ids(*this, kIds, PFC_TABSIZE(kIds), dlg_checked(*this, IDC_PREF_WEB_ENABLED));
}

void CLyricsPrefsWeb::OnToggle(UINT, int, CWindow) {
	UpdateDependentEnables();
	OnChangedCmd(0, 0, NULL);
}

void CLyricsPrefsWeb::OnGenerateToken(UINT, int, CWindow) {
	set_dlg_text(*this, IDC_PREF_WEB_TOKEN, generate_auth_token().get_ptr());
	OnChangedCmd(0, 0, NULL);
}

bool CLyricsPrefsWeb::HasChanged() {
	const unsigned port = GetDlgItemInt(IDC_PREF_WEB_PORT, NULL, FALSE);
	return dlg_checked(*this, IDC_PREF_WEB_ENABLED) != m_baseline.web_enabled
		|| port != m_baseline.web_port
		|| strcmp(dlg_text(*this, IDC_PREF_WEB_TOKEN).get_ptr(), m_baseline.web_auth_token.get_ptr()) != 0;
}

void CLyricsPrefsWeb::reset() {
	m_suppress_change = true;
	FillFromSettings(make_defaults());
	m_suppress_change = false;
	UpdateDependentEnables();
	m_callback->on_state_changed();
}

void CLyricsPrefsWeb::apply() {
	plugin_settings_t s;
	plugin_config::load(s);
	s.web_enabled = dlg_checked(*this, IDC_PREF_WEB_ENABLED);
	s.web_port = GetDlgItemInt(IDC_PREF_WEB_PORT, NULL, FALSE);
	if (s.web_port == 0) s.web_port = 8765;
	s.web_auth_token = dlg_text(*this, IDC_PREF_WEB_TOKEN);
	if (!save_and_reload(s)) return;
	plugin_config::load(m_baseline);
	m_suppress_change = true;
	FillFromSettings(m_baseline);
	m_suppress_change = false;
	UpdateDependentEnables();
	m_callback->on_state_changed();
}

// --- factories ---

static preferences_branch_factory g_lyrics_prefs_branch(
	guid_lyrics_preferences_branch, preferences_page::guid_tools, "Lyrics AI Translator");

class preferences_page_language : public preferences_page_impl<CLyricsPrefsLanguage> {
public:
	const char* get_name() override { return "Language"; }
	GUID get_guid() override { return guid_lyrics_preferences_page; }
	GUID get_parent_guid() override { return guid_lyrics_preferences_branch; }
	double get_sort_priority() override { return 1; }
};

class preferences_page_llm : public preferences_page_impl<CLyricsPrefsLLM> {
public:
	const char* get_name() override { return "LLM"; }
	GUID get_guid() override { return guid_lyrics_preferences_llm; }
	GUID get_parent_guid() override { return guid_lyrics_preferences_branch; }
	double get_sort_priority() override { return 2; }
};

class preferences_page_proxy : public preferences_page_impl<CLyricsPrefsProxy> {
public:
	const char* get_name() override { return "Proxy"; }
	GUID get_guid() override { return guid_lyrics_preferences_proxy; }
	GUID get_parent_guid() override { return guid_lyrics_preferences_branch; }
	double get_sort_priority() override { return 3; }
};

class preferences_page_cache : public preferences_page_impl<CLyricsPrefsCache> {
public:
	const char* get_name() override { return "Cache"; }
	GUID get_guid() override { return guid_lyrics_preferences_sync; }
	GUID get_parent_guid() override { return guid_lyrics_preferences_branch; }
	double get_sort_priority() override { return 4; }
};

class preferences_page_web : public preferences_page_impl<CLyricsPrefsWeb> {
public:
	const char* get_name() override { return "Web"; }
	GUID get_guid() override { return guid_lyrics_preferences_web; }
	GUID get_parent_guid() override { return guid_lyrics_preferences_branch; }
	double get_sort_priority() override { return 5; }
};

static preferences_page_factory_t<preferences_page_language> g_prefs_language;
static preferences_page_factory_t<preferences_page_llm> g_prefs_llm;
static preferences_page_factory_t<preferences_page_proxy> g_prefs_proxy;
static preferences_page_factory_t<preferences_page_cache> g_prefs_cache;
static preferences_page_factory_t<preferences_page_web> g_prefs_web;

} // namespace
