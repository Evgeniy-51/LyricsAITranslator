#include "stdafx.h"
#include "lyrics_window.h"
#include "resource.h"
#include "cache_reader.h"
#include "plugin_config.h"
#include "worker_launcher.h"
#include "git_sync.h"
#include "web_server_launcher.h"
#include "web_state_publisher.h"
#include <helpers/WindowPositionUtils.h>
#include <libPPUI/gdiplus_helpers.h>
#include <libPPUI/win32_utility.h>

#pragma comment(lib, "gdiplus.lib")

namespace {

static const DWORD kStatusTransientMs = 5000;
static const DWORD kWebInfoRefreshMs = 30000;

struct display_row_t {
	pfc::string8 original;
	pfc::string8 translation;
	int source_line_index = -1;
};

static const COLORREF kColorActiveLine = RGB(255, 236, 64);
static const COLORREF kColorActiveTranslation = RGB(220, 200, 70);
static const int kLyricsFontScalePercent = 120;
static const int kRowPairSpacingBase = 6;
static const int kRowPairSpacingPercent = 150;

static const int kToolbarSideMargin96 = 12;
static const int kToolbarBottomMargin96 = 12;
static const int kToolbarGapLyricsBtn96 = 16;
static const int kToolbarGapBtnStatus96 = 12;
static const int kToolbarGap96 = 8;
static const int kToolbarBtnH96 = 34;
static const int kToolbarStatusH96 = 20;
static const int kToolbarBtnPadX96 = 20;
static const int kToolbarMinBtnW96 = 108;
static const int kSyncHighlightBtnPadX96 = 10;
static const int kSyncHighlightBtnH96 = 24;
static const int kSyncHighlightInset96 = 6;
static const int kSyncHighlightScrollGap96 = 8;
static const wchar_t kSyncHighlightLabel[] = L"Sync";

static int scale_px_y(HWND hwnd, int px96) {
	const unsigned dpi = QueryScreenDPI_Y(hwnd);
	return MulDiv(px96, (int)dpi, 96);
}

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

static HFONT fb2k_query_font(const GUID& what) {
	auto api = ui_config_manager::tryGet();
	if (!api.is_valid()) return NULL;
	const t_ui_font font = api->query_font(what);
	return font != NULL ? (HFONT)font : NULL;
}

static COLORREF fb2k_query_ui_color(const GUID& what, int sys_color_fallback) {
	auto api = ui_config_manager::tryGet();
	t_ui_color color = 0;
	if (api.is_valid() && api->query_color(what, color))
		return (COLORREF)color;
	return fb2k_ui_color(sys_color_fallback);
}

static COLORREF fb2k_std_color(int sys_color_index) {
	auto api = ui_config_manager::tryGet();
	if (api.is_valid()) return api->getSysColor(sys_color_index);
	return ::GetSysColor(sys_color_index);
}

static COLORREF lyrics_active_line_color() {
	auto api = ui_config_manager::tryGet();
	t_ui_color color = 0;
	if (api.is_valid() && api->query_color(ui_color_highlight, color))
		return (COLORREF)color;
	return kColorActiveLine;
}

static COLORREF lyrics_active_translation_color() {
	const COLORREF base = lyrics_active_line_color();
	if (base == kColorActiveLine) return kColorActiveTranslation;
	return RGB(
		(GetRValue(base) * 220) / 255,
		(GetGValue(base) * 200) / 255,
		(GetBValue(base) * 70) / 255);
}

static COLORREF blend_colors(COLORREF fg, COLORREF bg, int alpha /* 0..255 */) {
	const int inv = 255 - alpha;
	return RGB(
		(GetRValue(fg) * alpha + GetRValue(bg) * inv) / 255,
		(GetGValue(fg) * alpha + GetGValue(bg) * inv) / 255,
		(GetBValue(fg) * alpha + GetBValue(bg) * inv) / 255);
}

static void draw_sync_highlight_button_bg(HDC dc, HWND ref_hwnd, const RECT& rc, bool pressed) {
	const COLORREF pane = fb2k_query_ui_color(ui_color_background, COLOR_WINDOW);
	const COLORREF text = fb2k_query_ui_color(ui_color_text, COLOR_WINDOWTEXT);
	const bool dark = effective_dark_ui();
	const COLORREF overlay = dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
	const int fill_alpha = pressed ? 56 : 40;
	const int border_alpha = dark ? 72 : 56;
	const COLORREF fill = blend_colors(overlay, pane, fill_alpha);
	const COLORREF border = blend_colors(text, pane, border_alpha);

	const int radius = scale_px_y(ref_hwnd, 8);
	HBRUSH brush = CreateSolidBrush(fill);
	HPEN pen = CreatePen(PS_SOLID, 1, border);
	HGDIOBJ oldBrush = SelectObject(dc, brush);
	HGDIOBJ oldPen = SelectObject(dc, pen);
	RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
	SelectObject(dc, oldPen);
	SelectObject(dc, oldBrush);
	DeleteObject(pen);
	DeleteObject(brush);
}

static void measure_sync_highlight_button_size(HWND hwnd, int& out_w, int& out_h) {
	HFONT font = fb2k_query_font(ui_font_default);
	if (font == NULL) font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	CClientDC dc(hwnd);
	HFONT old = dc.SelectFont(font);
	SIZE sz = {};
	::GetTextExtentPoint32W(dc, kSyncHighlightLabel, (int)_countof(kSyncHighlightLabel) - 1, &sz);
	dc.SelectFont(old);
	const int pad_x = scale_px_y(hwnd, kSyncHighlightBtnPadX96);
	out_w = sz.cx + pad_x * 2;
	out_h = pfc::max_t<int>(sz.cy + scale_px_y(hwnd, 6), scale_px_y(hwnd, kSyncHighlightBtnH96));
}

static void draw_sync_highlight_label(HDC dc, HWND ref_hwnd, const RECT& rc, bool enabled, bool pressed, COLORREF text_color) {
	HFONT font = fb2k_query_font(ui_font_default);
	if (font == NULL) font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	HFONT oldFont = (HFONT)SelectObject(dc, font);
	SetBkMode(dc, TRANSPARENT);

	COLORREF color = text_color;
	if (pressed) {
		color = RGB(
			(GetRValue(color) * 180) / 255,
			(GetGValue(color) * 180) / 255,
			(GetBValue(color) * 180) / 255);
	}
	SetTextColor(dc, color);

	RECT text_rc = rc;
	DrawTextW(dc, kSyncHighlightLabel, -1, &text_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

	if (!enabled) {
		SIZE sz = {};
		::GetTextExtentPoint32W(dc, kSyncHighlightLabel, (int)_countof(kSyncHighlightLabel) - 1, &sz);
		const int x0 = rc.left + (rc.right - rc.left - sz.cx) / 2;
		const int x1 = x0 + sz.cx;
		const int y = (rc.top + rc.bottom) / 2;
		HPEN pen = CreatePen(PS_SOLID, 2, color);
		HGDIOBJ oldPen = SelectObject(dc, pen);
		MoveToEx(dc, x0, y, NULL);
		LineTo(dc, x1, y);
		SelectObject(dc, oldPen);
		DeleteObject(pen);
	}

	SelectObject(dc, oldFont);
}

struct toolbar_layout_t {
	int side_margin = 0;
	int bottom_margin = 0;
	int gap_lyrics_btn = 0;
	int gap_btn_status = 0;
	int gap = 0;
	int btn_h = 0;
	int status_h = 0;
	int min_btn_w = 0;
	int btn_pad_x = 0;
	int reserved_height() const {
		return gap_lyrics_btn + btn_h + gap_btn_status + status_h + bottom_margin;
	}
};

static toolbar_layout_t toolbar_layout_for(HWND hwnd) {
	toolbar_layout_t t;
	t.side_margin = scale_px_y(hwnd, kToolbarSideMargin96);
	t.bottom_margin = scale_px_y(hwnd, kToolbarBottomMargin96);
	t.gap_lyrics_btn = scale_px_y(hwnd, kToolbarGapLyricsBtn96);
	t.gap_btn_status = scale_px_y(hwnd, kToolbarGapBtnStatus96);
	t.gap = scale_px_y(hwnd, kToolbarGap96);
	t.btn_h = scale_px_y(hwnd, kToolbarBtnH96);
	const int sys_btn = GetSystemMetrics(91 /* SM_CYBUTTON */);
	if (sys_btn > t.btn_h) t.btn_h = sys_btn;
	t.status_h = scale_px_y(hwnd, kToolbarStatusH96);
	{
		CClientDC dc(hwnd);
		CFontHandle font = fb2k_query_font(ui_font_statusbar);
		if (font == NULL) font = fb2k_query_font(ui_font_default);
		if (font == NULL) font = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
		if (font == NULL) font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
		if (font) {
			HFONT old = dc.SelectFont(font);
			TEXTMETRIC tm = {};
			dc.GetTextMetrics(&tm);
			dc.SelectFont(old);
			const int text_h = tm.tmHeight + scale_px_y(hwnd, 6);
			if (text_h > t.status_h) t.status_h = text_h;
		}
	}
	t.min_btn_w = scale_px_y(hwnd, kToolbarMinBtnW96);
	t.btn_pad_x = scale_px_y(hwnd, kToolbarBtnPadX96);
	return t;
}

static int measure_max_button_label_cx(HWND dlg, HFONT font, int pad_x) {
	CClientDC dc(dlg);
	HFONT old = dc.SelectFont(font);
	int max_cx = 0;
	static const wchar_t* kLabels[] = {
		L"Get all lyrics", L"Cancel", L"Sync cache", L"Reload track"
	};
	for (const wchar_t* label : kLabels) {
		SIZE sz = {};
		::GetTextExtentPoint32W(dc, label, (int)wcslen(label), &sz);
		if (sz.cx > max_cx) max_cx = sz.cx;
	}
	dc.SelectFont(old);
	return max_cx + pad_x;
}

class CLyricsWindow;
static CLyricsWindow* g_window = nullptr;

class CWebQrDialog : public CDialogImpl<CWebQrDialog> {
public:
	enum { IDD = IDD_WEB_QR };

	pfc::string8 m_url;
	pfc::array_t<t_uint8> m_png;
	std::unique_ptr<Gdiplus::Image> m_image;
	CRect m_qr_rect;

	BEGIN_MSG_MAP(CWebQrDialog)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_PAINT(OnPaint)
		COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnCloseCmd)
		COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnCloseCmd)
	END_MSG_MAP()

	BOOL OnInitDialog(CWindow, LPARAM) {
		m_dark.AddDialogWithControls(m_hWnd);
		m_dark.SetDark(effective_dark_ui());
		uSetDlgItemText(*this, IDC_QR_URL_TEXT, m_url.get_ptr());
		CWindow qr = GetDlgItem(IDC_QR_IMAGE);
		if (qr.m_hWnd != NULL) {
			qr.GetWindowRect(m_qr_rect);
			ScreenToClient(m_qr_rect);
			qr.ShowWindow(SW_HIDE);
		}
		GdiplusScope::Once();
		if (m_png.get_size() > 0)
			m_image = GdiplusImageFromMem(m_png.get_ptr(), m_png.get_size());
		CenterWindow(GetParent());
		return TRUE;
	}

	void OnPaint(CDCHandle) {
		CPaintDC dc(*this);
		if (m_image && !m_qr_rect.IsRectEmpty()) {
			dc.FillSolidRect(m_qr_rect, RGB(255, 255, 255));
			Gdiplus::Graphics g(dc.m_hDC);
			g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
			g.DrawImage(m_image.get(),
				Gdiplus::Rect(m_qr_rect.left, m_qr_rect.top, m_qr_rect.Width(), m_qr_rect.Height()));
		}
	}

	void OnCloseCmd(UINT, int, CWindow) {
		EndDialog(IDOK);
	}

private:
	fb2k::CDarkModeHooks m_dark;
};

static bool read_track_info(metadb_handle_ptr handle, track_info_t& out) {
	if (handle.is_empty()) return false;
	file_info_impl info;
	handle->get_info(info);
	out.artist = info.meta_get("artist", 0);
	out.title = info.meta_get("title", 0);
	out.album = info.meta_get("album", 0);
	out.duration_sec = info.get_length();
	if (out.artist.is_empty()) out.artist = "Unknown Artist";
	if (out.title.is_empty()) out.title = "Unknown Title";
	if (out.album.is_empty()) out.album = "";
	return true;
}

static pfc::string8 track_cache_path_read(const plugin_settings_t& settings, const track_info_t& track) {
	return lyrics_cache::resolve_cache_file_path(
		settings.cache_dir.get_ptr(),
		track.artist.get_ptr(),
		track.album.get_ptr(),
		track.title.get_ptr());
}

static pfc::string8 track_cache_path_write(const plugin_settings_t& settings, const track_info_t& track) {
	return lyrics_cache::cache_file_path(
		settings.cache_dir.get_ptr(),
		track.artist.get_ptr(),
		track.album.get_ptr(),
		track.title.get_ptr());
}

static pfc::string8 web_json_escape(const char* s) {
	pfc::string8 out;
	for (const char* p = s ? s : ""; *p; ++p) {
		switch (*p) {
		case '\\': out += "\\\\"; break;
		case '"': out += "\\\""; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default: out.add_byte(*p); break;
		}
	}
	return out;
}

class CLyricsView : public CWindowImpl<CLyricsView> {
public:
	BEGIN_MSG_MAP_EX(CLyricsView)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_SIZE(OnSize)
		MSG_WM_VSCROLL(OnVScroll)
		MSG_WM_MOUSEWHEEL(OnMouseWheel)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
	END_MSG_MAP()

	void set_font(HFONT font) { m_font = font; rebuild_layout(); }
	void set_rows(const pfc::array_t<display_row_t>* rows, int active_source_line) {
		m_rows = rows;
		m_active_source = active_source_line;
		rebuild_layout();
		update_paint_offset();
		update_scrollbar();
		Invalidate(FALSE);
	}
	void set_active_source_line(int active_source_line) {
		if (m_active_source == active_source_line) return;
		m_active_source = active_source_line;
		update_paint_offset();
		update_scrollbar();
		Invalidate(FALSE);
	}

private:
	BOOL OnEraseBkgnd(CDCHandle dc) {
		CRect rc;
		GetClientRect(rc);
		dc.FillSolidRect(rc, fb2k_query_ui_color(ui_color_background, COLOR_WINDOW));
		return TRUE;
	}
	void OnSize(UINT, CSize) {
		update_paint_offset();
		update_scrollbar();
		Invalidate(FALSE);
	}
	void OnVScroll(UINT code, UINT pos, CScrollBar) {
		SCROLLINFO si = { sizeof(si) };
		si.fMask = SIF_ALL;
		GetScrollInfo(SB_VERT, &si);
		const int page = (int)si.nPage;
		const int maxPos = (int)si.nMax - (page > 0 ? page - 1 : 0);
		int y = (int)si.nPos;
		switch (code) {
		case SB_TOP: y = 0; break;
		case SB_BOTTOM: y = maxPos; break;
		case SB_LINEUP: y -= m_line_height; break;
		case SB_LINEDOWN: y += m_line_height; break;
		case SB_PAGEUP: y -= page; break;
		case SB_PAGEDOWN: y += page; break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: y = (int)pos; break;
		default: return;
		}
		if (y < 0) y = 0;
		if (y > maxPos) y = maxPos;
		apply_scroll_pos(y);
	}
	LRESULT OnMouseWheel(UINT flags, short delta, CPoint pt) {
		(void)flags;
		(void)pt;
		SCROLLINFO si = { sizeof(si) };
		si.fMask = SIF_ALL;
		GetScrollInfo(SB_VERT, &si);
		const int page = (int)si.nPage;
		const int maxPos = (int)si.nMax - (page > 0 ? page - 1 : 0);
		int y = (int)si.nPos - (delta / WHEEL_DELTA) * m_line_height * 3;
		if (y < 0) y = 0;
		if (y > maxPos) y = maxPos;
		apply_scroll_pos(y);
		return 0;
	}
	void OnPaint(CDCHandle) {
		CPaintDC dc(*this);
		CRect clip;
		dc.GetClipBox(clip);
		draw_content(CDCHandle(dc.m_hDC), clip);
	}

	int measure_line_height() const {
		if (!m_hWnd) return 16;
		CClientDC dc(m_hWnd);
		HFONT useFont = m_font ? m_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
		HFONT old = dc.SelectFont(useFont);
		TEXTMETRIC tm = {};
		dc.GetTextMetrics(&tm);
		dc.SelectFont(old);
		return tm.tmHeight + tm.tmExternalLeading;
	}

	int row_pair_spacing() const {
		return MulDiv(kRowPairSpacingBase, kRowPairSpacingPercent, 100);
	}

	int row_pixel_height(t_size row_index) const {
		if (!m_rows || row_index >= m_rows->get_size()) return m_line_height + row_pair_spacing();
		int lines = 1;
		if (!(*m_rows)[row_index].translation.is_empty()) ++lines;
		return lines * m_line_height + row_pair_spacing();
	}

	void rebuild_layout() {
		m_line_height = measure_line_height();
		const t_size n = m_rows ? m_rows->get_size() : 0;
		m_row_y.resize(n + 1);
		m_row_y[0] = 0;
		for (t_size i = 0; i < n; ++i)
			m_row_y[i + 1] = m_row_y[i] + row_pixel_height(i);
		m_content_height = n > 0 ? m_row_y[n] : 0;
	}

	t_size display_index_for_source(int source_line) const {
		if (!m_rows || source_line < 0) return SIZE_MAX;
		for (t_size i = 0; i < m_rows->get_size(); ++i) {
			if ((*m_rows)[i].source_line_index == source_line) return i;
		}
		return SIZE_MAX;
	}

	void update_paint_offset() {
		CRect client;
		GetClientRect(client);
		const int ch = client.Height();
		if (ch <= 0 || !m_rows || m_rows->get_size() == 0) {
			m_paint_offset_y = 0;
			return;
		}
		const t_size active_disp = display_index_for_source(m_active_source);
		if (active_disp == SIZE_MAX) {
			m_paint_offset_y = 0;
			return;
		}
		const int row_top = m_row_y[active_disp];
		const int row_h = m_row_y[active_disp + 1] - row_top;
		const int active_center = row_top + row_h / 2;
		const int ideal = ch / 2 - active_center;
		const int min_off = pfc::min_t(0, ch - m_content_height);
		const int max_off = pfc::max_t(0, ch - m_content_height);
		m_paint_offset_y = pfc::max_t(min_off, pfc::min_t(max_off, ideal));
	}

	void update_scrollbar() {
		CRect client;
		GetClientRect(client);
		const int ch = client.Height();
		SCROLLINFO si = { sizeof(si) };
		si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		si.nMin = 0;
		si.nMax = pfc::max_t(0, m_content_height);
		si.nPage = (UINT)pfc::max_t(1, ch);
		si.nPos = -m_paint_offset_y;
		SetScrollInfo(SB_VERT, &si, TRUE);
	}

	void apply_scroll_pos(int scroll_pos) {
		CRect client;
		GetClientRect(client);
		const int ch = client.Height();
		const int min_off = pfc::min_t(0, ch - m_content_height);
		const int max_off = pfc::max_t(0, ch - m_content_height);
		m_paint_offset_y = pfc::max_t(min_off, pfc::min_t(max_off, -scroll_pos));
		update_scrollbar();
		Invalidate(FALSE);
	}

	void draw_content(CDCHandle dc, const CRect& clip) const {
		if (!m_rows) return;
		CRect client;
		GetClientRect(client);
		dc.FillSolidRect(client, fb2k_query_ui_color(ui_color_background, COLOR_WINDOW));
		HFONT useFont = m_font ? m_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
		HFONT oldFont = dc.SelectFont(useFont);
		SetBkMode(dc, TRANSPARENT);
		const int lh = m_line_height;
		const int text_pad = 4;
		for (t_size i = 0; i < m_rows->get_size(); ++i) {
			const int y0 = m_paint_offset_y + m_row_y[i];
			const int y1 = m_paint_offset_y + m_row_y[i + 1];
			if (y1 < clip.top || y0 > clip.bottom) continue;
			const auto& row = (*m_rows)[i];
			const bool active = row.source_line_index == m_active_source;
			CRect rcItem(text_pad, y0, client.Width() - text_pad, y1);
			pfc::stringcvt::string_os_from_utf8 os_orig(row.original);
			CRect rcOrig = rcItem;
			rcOrig.bottom = rcOrig.top + lh;
			dc.SetTextColor(active ? lyrics_active_line_color() : fb2k_query_ui_color(ui_color_text, COLOR_WINDOWTEXT));
			dc.DrawText(os_orig.get_ptr(), -1, rcOrig, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE);
			if (!row.translation.is_empty()) {
				pfc::stringcvt::string_os_from_utf8 os_trans(row.translation);
				CRect rcTrans = rcItem;
				rcTrans.top = rcOrig.bottom;
				rcTrans.bottom = rcTrans.top + lh;
				dc.SetTextColor(active ? lyrics_active_translation_color() : fb2k_std_color(COLOR_GRAYTEXT));
				dc.DrawText(os_trans.get_ptr(), -1, rcTrans, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE);
			}
		}
		dc.SelectFont(oldFont);
	}

	const pfc::array_t<display_row_t>* m_rows = nullptr;
	int m_active_source = -1;
	pfc::array_t<int> m_row_y;
	int m_content_height = 0;
	int m_line_height = 16;
	int m_paint_offset_y = 0;
	HFONT m_font = NULL;
};

class CLyricsWindow : public CDialogImpl<CLyricsWindow>, private play_callback_impl_base, private ui_config_callback_impl {
public:
	enum { IDD = IDD_LYRICS_WINDOW };

	CLyricsWindow() : play_callback_impl_base(0) {}

	BEGIN_MSG_MAP_EX(CLyricsWindow)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_DESTROY(OnDestroy)
		MSG_WM_SIZE(OnSize)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_GETMINMAXINFO(OnGetMinMaxInfo)
		MSG_WM_TIMER(OnTimer)
		MESSAGE_HANDLER_EX(DarkMode::msgSetDarkMode(), OnSetDarkMode)
		MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
		COMMAND_HANDLER_EX(IDC_BTN_GET_ALL, BN_CLICKED, OnGetAll)
		COMMAND_HANDLER_EX(IDC_BTN_CANCEL_BATCH, BN_CLICKED, OnCancelBatch)
		COMMAND_HANDLER_EX(IDC_BTN_SYNC_CACHE, BN_CLICKED, OnSyncCache)
		COMMAND_HANDLER_EX(IDC_BTN_REFRESH_TRACK, BN_CLICKED, OnRefreshTrack)
		COMMAND_HANDLER_EX(IDC_BTN_SYNC_HIGHLIGHT, BN_CLICKED, OnSyncHighlightToggle)
		COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnCancel)
		COMMAND_HANDLER_EX(IDC_STATIC_STATUS, STN_CLICKED, OnStatusClicked)
		MSG_WM_DRAWITEM(OnDrawItem)
	END_MSG_MAP()

public:
	void reload_settings();
	void update_status_text(const char* text);
	void show_status_transient(const char* text, DWORD duration_ms = kStatusTransientMs);
	void refresh_status_bar();
	void refresh_web_info();
	void show_web_qr_dialog();
	void update_window_title();
	void refresh_track();

	void ui_fonts_changed() override { apply_fb2k_ui(); }
	void ui_colors_changed() override { apply_fb2k_ui(); }
	void apply_fb2k_ui();
	void sync_dark_mode();
	void update_lyrics_font();

private:
	BOOL OnInitDialog(CWindow, LPARAM);
	void OnDestroy();
	void OnSize(UINT, CSize);
	void OnMouseMove(UINT, CPoint);
	void OnGetMinMaxInfo(LPMINMAXINFO);
	void relay_tooltip_mouse(UINT msg, WPARAM wParam, LPARAM lParam);
	void OnTimer(UINT_PTR id);
	LRESULT OnSetDarkMode(UINT, WPARAM, LPARAM);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
	void layout_bottom_controls();
	void layout_lyrics_view();
	void layout_sync_highlight_button();
	void refresh_lyrics_view();
	void OnGetAll(UINT, int, CWindow);
	void OnCancelBatch(UINT, int, CWindow);
	void OnSyncCache(UINT, int, CWindow);
	void OnRefreshTrack(UINT, int, CWindow);
	void OnSyncHighlightToggle(UINT, int, CWindow);
	void OnCancel(UINT, int, CWindow);
	void OnStatusClicked(UINT, int, CWindow);
	void OnDrawItem(UINT, LPDRAWITEMSTRUCT);
	void refetch_current_track();

	void register_playback();
	void unregister_playback();

	void on_playback_new_track(metadb_handle_ptr p_track) override;
	void on_playback_seek(double p_time) override;
	void on_playback_stop(play_control::t_stop_reason p_reason) override;
	void on_playback_time(double p_time) override;

	void load_cache_and_display();
	void display_original_only(const lyrics_cache_t& data, const char* status_line);
	bool try_show_session_original(const char* status_line);
	void show_translation_session_failure();
	bool should_block_translation(pfc::string8& out_message);
	void on_worker_finished();
	void update_highlight(double pos_sec_override = -1.0);
	bool cache_has_sync_timestamps() const;
	bool has_lyric_display_rows() const;
	void show_list_message(const char* msg);
	void set_batch_ui_active(bool active);
	void update_sync_cache_button();
	void setup_button_tooltips();
	void update_sync_highlight_button();
	bool read_now_playing(track_info_t& out);
	bool begin_lyrics_worker_fetch();
	void redraw_list();

	void start_batch_fetch();
	void cancel_batch_fetch();
	void tick_batch_fetch();
	void launch_next_batch_job();
	bool collect_active_playlist_queue();
	void publish_web_state(bool force = false);
	void poll_web_highlight_control();
	void poll_web_player_control();

	static_api_ptr_t<playback_control> m_playback;
	static_api_ptr_t<playlist_manager> m_playlist_mgr;
	plugin_settings_t m_settings;
	lyrics_cache_t m_cache;
	track_info_t m_track;
	pfc::string8 m_cache_path;
	pfc::array_t<display_row_t> m_display_rows;
	pfc::list_t<track_info_t> m_batch_queue;
	int m_active_line = -1;
	bool m_worker_pending = false;
	bool m_batch_active = false;
	CLyricsView m_lyrics_view;
	bool m_batch_cancelled = false;
	t_size m_batch_pending = 0;
	t_size m_batch_skipped_ready = 0;
	t_size m_batch_skipped_dup = 0;
	t_size m_batch_skipped_no_album = 0;
	t_size m_batch_done = 0;
	bool m_batch_worker_was_busy = false;
	pfc::string8 m_no_retry_cache_path;
	fb2k::CDarkModeHooks m_dark;
	CToolTipCtrl m_tooltips;
	bool m_tooltips_ready = false;
	CFont m_lyrics_font;
	DWORD m_web_last_publish_tick = 0;
	CWindow m_btn_sync_highlight;
	bool m_sync_highlight_enabled = true;
	pfc::string8 m_web_lan_url;
	pfc::string8 m_status_transient;
	DWORD m_status_transient_until = 0;
	DWORD m_web_info_last_fetch = 0;
};

void CLyricsWindow::publish_web_state(bool force) {
	if (!m_settings.web_enabled) return;
	if (!web_server_launcher::ensure_running(m_settings)) return;

	const DWORD now = GetTickCount();
	if (!force && m_web_last_publish_tick != 0) {
		const DWORD elapsed = now - m_web_last_publish_tick;
		if (elapsed < m_settings.web_update_interval_ms) return;
	}
	m_web_last_publish_tick = now;

	const bool playing = m_playback->is_playing();
	const bool paused = m_playback->is_paused();
	double pos_sec = 0.0;
	if (playing || paused)
		pos_sec = m_playback->playback_get_position();

	const bool has_sync = cache_has_sync_timestamps();
	pfc::string_formatter json;
	json << "{\n"
		<< "  \"track\": {\n"
		<< "    \"artist\": \"" << web_json_escape(m_track.artist.get_ptr()) << "\",\n"
		<< "    \"album\": \"" << web_json_escape(m_track.album.get_ptr()) << "\",\n"
		<< "    \"title\": \"" << web_json_escape(m_track.title.get_ptr()) << "\",\n"
		<< "    \"durationSec\": " << m_track.duration_sec << "\n"
		<< "  },\n"
		<< "  \"playback\": {\n"
		<< "    \"isPlaying\": " << (playing ? "true" : "false") << ",\n"
		<< "    \"isPaused\": " << (paused ? "true" : "false") << ",\n"
		<< "    \"positionSec\": " << pos_sec << "\n"
		<< "  },\n"
		<< "  \"lyrics\": {\n"
		<< "    \"hasSync\": " << (has_sync ? "true" : "false") << ",\n"
		<< "    \"highlightEnabled\": " << (m_sync_highlight_enabled ? "true" : "false") << ",\n"
		<< "    \"activeLine\": " << (m_sync_highlight_enabled ? m_active_line : -1) << ",\n"
		<< "    \"lines\": [";

	bool first_line = true;
	for (t_size i = 0; i < m_display_rows.get_size(); ++i) {
		const auto& row = m_display_rows[i];
		if (row.source_line_index < 0) continue;
		int time_ms = 0;
		if ((t_size)row.source_line_index < m_cache.lines.get_size())
			time_ms = m_cache.lines[row.source_line_index].time_ms;
		if (!first_line) json << ",";
		first_line = false;
		json << "\n      {\"index\": " << row.source_line_index
			<< ", \"timeMs\": " << time_ms
			<< ", \"original\": \"" << web_json_escape(row.original.get_ptr()) << "\"";
		if (!row.translation.is_empty())
			json << ", \"translation\": \"" << web_json_escape(row.translation.get_ptr()) << "\"";
		json << "}";
	}
	if (m_display_rows.get_size() > 0 && first_line) {
		const auto& row = m_display_rows[0];
		if (row.source_line_index < 0 && !row.original.is_empty()) {
			json << "\n      {\"index\": 0, \"original\": \"" << web_json_escape(row.original.get_ptr()) << "\"}";
			first_line = false;
		}
	}
	json << "\n    ]\n  },\n"
		<< "  \"status\": \"" << web_json_escape(m_cache.status.get_ptr()) << "\"\n"
		<< "}";

	web_state_publisher::post_async(m_settings.web_port, json.get_ptr());
}

void CLyricsWindow::poll_web_highlight_control() {
	if (!m_settings.web_enabled) return;
	if (!web_server_launcher::ensure_running(m_settings)) return;

	bool enabled = false;
	if (!web_state_publisher::poll_highlight_pending(m_settings.web_port, &enabled))
		return;
	if (enabled == m_sync_highlight_enabled)
		return;

	m_sync_highlight_enabled = enabled;
	if (!m_sync_highlight_enabled) {
		m_active_line = -1;
		if (m_lyrics_view.m_hWnd)
			m_lyrics_view.set_active_source_line(-1);
	} else {
		update_highlight();
	}
	update_sync_highlight_button();
}

void CLyricsWindow::poll_web_player_control() {
	if (!m_settings.web_enabled) return;
	if (!web_server_launcher::ensure_running(m_settings)) return;

	pfc::string8 command;
	if (!web_state_publisher::poll_player_command_pending(m_settings.web_port, command))
		return;

	if (strcmp(command.get_ptr(), "prev") == 0) {
		m_playback->previous();
	} else if (strcmp(command.get_ptr(), "playPause") == 0) {
		m_playback->play_or_pause();
	} else if (strcmp(command.get_ptr(), "next") == 0) {
		m_playback->next();
	} else if (strcmp(command.get_ptr(), "seekBack") == 0) {
		if (m_playback->playback_can_seek())
			m_playback->playback_seek_delta(-10.0);
	} else if (strcmp(command.get_ptr(), "seekForward") == 0) {
		if (m_playback->playback_can_seek())
			m_playback->playback_seek_delta(10.0);
	}
}

void CLyricsWindow::show_status_transient(const char* text, DWORD duration_ms) {
	if (text != NULL && text[0] != '\0') {
		m_status_transient = text;
		m_status_transient_until = GetTickCount() + duration_ms;
	} else {
		m_status_transient.reset();
		m_status_transient_until = 0;
	}
	refresh_status_bar();
}

void CLyricsWindow::refresh_status_bar() {
	const DWORD now = GetTickCount();
	if (m_status_transient.length() > 0 && (int)(now - m_status_transient_until) < 0) {
		uSetDlgItemText(*this, IDC_STATIC_STATUS, m_status_transient.get_ptr());
		return;
	}
	m_status_transient.reset();
	m_status_transient_until = 0;

	if (m_settings.web_enabled) {
		if (web_server_launcher::is_running()) {
			pfc::string_formatter line;
			if (!m_web_lan_url.is_empty())
				line << "Web: " << m_web_lan_url.get_ptr() << "  (click for QR)";
			else
				line << "Web: port " << m_settings.web_port << "  (click for QR)";
			uSetDlgItemText(*this, IDC_STATIC_STATUS, line.get_ptr());
		} else {
			uSetDlgItemText(*this, IDC_STATIC_STATUS,
				"Web: lyrics_server.exe missing or failed (see console)");
		}
		return;
	}
	uSetDlgItemText(*this, IDC_STATIC_STATUS, "Ready");
}

void CLyricsWindow::refresh_web_info() {
	if (!m_settings.web_enabled) {
		m_web_lan_url.reset();
		refresh_status_bar();
		return;
	}
	if (!web_server_launcher::ensure_running(m_settings)) {
		m_web_lan_url.reset();
		refresh_status_bar();
		return;
	}
	pfc::string8 url;
	for (int attempt = 0; attempt < 10; ++attempt) {
		if (web_state_publisher::fetch_server_url(m_settings.web_port, url)) {
			m_web_lan_url = url;
			break;
		}
		Sleep(100);
	}
	refresh_status_bar();
}

void CLyricsWindow::show_web_qr_dialog() {
	if (!m_settings.web_enabled || !web_server_launcher::is_running())
		return;

	if (m_web_lan_url.is_empty())
		refresh_web_info();

	CWebQrDialog dlg;
	if (!m_web_lan_url.is_empty())
		dlg.m_url = m_web_lan_url;
	else {
		pfc::string_formatter f;
		f << "http://<this-pc-ip>:" << m_settings.web_port << "/";
		dlg.m_url = f;
	}
	if (!web_state_publisher::fetch_qr_png(m_settings.web_port, dlg.m_png))
		console::print("Lyrics Web: QR fetch failed");
	dlg.DoModal(m_hWnd);
}

void CLyricsWindow::update_status_text(const char* text) {
	show_status_transient(text);
}

void CLyricsWindow::OnStatusClicked(UINT, int, CWindow) {
	if (m_settings.web_enabled && web_server_launcher::is_running())
		show_web_qr_dialog();
}

bool CLyricsWindow::cache_has_sync_timestamps() const {
	for (t_size i = 0; i < m_cache.lines.get_size(); ++i) {
		if (m_cache.lines[i].time_ms > 0)
			return true;
	}
	return false;
}

bool CLyricsWindow::has_lyric_display_rows() const {
	for (t_size i = 0; i < m_display_rows.get_size(); ++i) {
		if (m_display_rows[i].source_line_index >= 0)
			return true;
	}
	return false;
}

void CLyricsWindow::update_window_title() {
	pfc::string8 title = "Lyrics";
	if (!m_track.title.is_empty()) {
		if (!m_track.artist.is_empty())
			title = pfc::string_formatter() << m_track.artist << " - " << m_track.title;
		else
			title = m_track.title;
	}
	if (has_lyric_display_rows() && !cache_has_sync_timestamps())
		title << "    (no sync \xE2\x80\x94 highlight disabled)";
	SetWindowText(pfc::stringcvt::string_wide_from_utf8(title.get_ptr()));
}

void CLyricsWindow::set_batch_ui_active(bool active) {
	CWindow btnGet(GetDlgItem(IDC_BTN_GET_ALL));
	CWindow btnCancel(GetDlgItem(IDC_BTN_CANCEL_BATCH));
	btnGet.EnableWindow(!active);
	btnCancel.EnableWindow(active ? TRUE : FALSE);
	update_sync_cache_button();
}

void CLyricsWindow::update_sync_cache_button() {
	CWindow btn(GetDlgItem(IDC_BTN_SYNC_CACHE));
	if (btn.m_hWnd == NULL) return;
	btn.EnableWindow(m_settings.sync_enabled && !m_batch_active ? TRUE : FALSE);
}

void CLyricsWindow::setup_button_tooltips() {
	if (m_tooltips.m_hWnd == NULL) {
		m_tooltips.Create(m_hWnd, NULL, NULL, TTS_ALWAYSTIP | TTS_NOPREFIX, WS_EX_TOPMOST);
		m_tooltips.SetMaxTipWidth(280);
		m_tooltips_ready = false;
	}
	auto add = [&](UINT id, const wchar_t* text) {
		CWindow btn = GetDlgItem(id);
		if (btn.m_hWnd == NULL) return;
		TOOLINFO ti = {};
		ti.cbSize = sizeof(TOOLINFO);
		ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
		ti.hwnd = m_hWnd;
		ti.uId = (UINT_PTR)btn.m_hWnd;
		ti.lpszText = const_cast<wchar_t*>(text);
		if (!m_tooltips_ready)
			m_tooltips.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
		else
			m_tooltips.SendMessage(TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
	};
	// Rect-based tip on the dialog: works for disabled Sync cache button
	// (disabled controls do not receive mouse messages).
	auto add_rect = [&](UINT id, const wchar_t* text) {
		CWindow btn = GetDlgItem(id);
		if (btn.m_hWnd == NULL) return;
		TOOLINFO ti = {};
		ti.cbSize = sizeof(TOOLINFO);
		ti.uFlags = TTF_SUBCLASS;
		ti.hwnd = m_hWnd;
		ti.uId = id;
		btn.GetWindowRect(&ti.rect);
		ScreenToClient(&ti.rect);
		ti.lpszText = const_cast<wchar_t*>(text);
		if (!m_tooltips_ready)
			m_tooltips.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
		else {
			m_tooltips.SendMessage(TTM_NEWTOOLRECT, 0, (LPARAM)&ti);
			m_tooltips.SendMessage(TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
		}
	};
	add(IDC_BTN_GET_ALL, L"Batch-fetch lyrics for the active playlist");
	add(IDC_BTN_CANCEL_BATCH, L"Stop batch fetch");
	add_rect(IDC_BTN_SYNC_CACHE, m_settings.sync_enabled
		? L"Sync local cache with Git (push and pull)"
		: L"Git sync is disabled in Preferences (Cache tab)");
	add(IDC_BTN_REFRESH_TRACK, L"Refetch lyrics for the current track");
	if (m_settings.web_enabled) {
		add(IDC_STATIC_STATUS, L"Click to show QR code for phone access");
	}
	if (m_btn_sync_highlight.m_hWnd != NULL) {
		const wchar_t* tip = m_sync_highlight_enabled
			? L"Auto-scroll and highlight synced lyrics (click to disable)"
			: L"Manual scroll mode — highlight off (click to enable)";
		add(IDC_BTN_SYNC_HIGHLIGHT, tip);
	}
	m_tooltips_ready = true;
	m_tooltips.Activate(TRUE);
}

void CLyricsWindow::relay_tooltip_mouse(UINT msg, WPARAM wParam, LPARAM lParam) {
	if (m_tooltips.m_hWnd == NULL) return;
	MSG relay = { m_hWnd, msg, wParam, lParam };
	m_tooltips.RelayEvent(&relay);
}

void CLyricsWindow::OnMouseMove(UINT nFlags, CPoint point) {
	relay_tooltip_mouse(WM_MOUSEMOVE, nFlags, MAKELPARAM(point.x, point.y));
}

void CLyricsWindow::redraw_list() {
	if (m_lyrics_view.m_hWnd) m_lyrics_view.Invalidate(FALSE);
}

void CLyricsWindow::apply_fb2k_ui() {
	const HFONT dlg_font = fb2k_query_font(ui_font_default);
	if (dlg_font != NULL) {
		SetFont(dlg_font);
		static const UINT kBtnIds[] = {
			IDC_BTN_GET_ALL, IDC_BTN_CANCEL_BATCH, IDC_BTN_SYNC_CACHE, IDC_BTN_REFRESH_TRACK
		};
		for (UINT id : kBtnIds) {
			CWindow btn = GetDlgItem(id);
			if (btn.m_hWnd != NULL) btn.SetFont(dlg_font);
		}
	}
	const HFONT status_font = fb2k_query_font(ui_font_statusbar);
	CWindow status = GetDlgItem(IDC_STATIC_STATUS);
	if (status.m_hWnd != NULL) {
		if (status_font != NULL) status.SetFont(status_font);
		else if (dlg_font != NULL) status.SetFont(dlg_font);
	}
	update_lyrics_font();
	layout_bottom_controls();
	if (m_lyrics_view.m_hWnd) layout_lyrics_view();
	sync_dark_mode();
	redraw_list();
	Invalidate(FALSE);
}

void CLyricsWindow::update_lyrics_font() {
	HFONT base = fb2k_query_font(ui_font_default);
	LOGFONT lf = {};
	if (base == NULL || !GetObject(base, sizeof(lf), &lf)) {
		CFontHandle dlg = GetFont();
		if (!dlg.GetLogFont(lf)) {
			const unsigned dpi = QueryScreenDPI_Y(m_hWnd);
			lf.lfHeight = -MulDiv(9, (int)dpi, 72);
			lf.lfCharSet = DEFAULT_CHARSET;
			_tcscpy_s(lf.lfFaceName, LF_FACESIZE, _T("Segoe UI"));
		}
	}
	if (lf.lfHeight < 0)
		lf.lfHeight = -MulDiv(-lf.lfHeight, kLyricsFontScalePercent, 100);
	else if (lf.lfHeight > 0)
		lf.lfHeight = MulDiv(lf.lfHeight, kLyricsFontScalePercent, 100);
	lf.lfWidth = 0;
	m_lyrics_font.DeleteObject();
	m_lyrics_font.CreateFontIndirect(&lf);
	if (m_lyrics_view.m_hWnd)
		m_lyrics_view.set_font(m_lyrics_font);
}

void CLyricsWindow::layout_bottom_controls() {
	CRect rc;
	GetClientRect(rc);
	const toolbar_layout_t tl = toolbar_layout_for(m_hWnd);
	static const UINT kBtnIds[] = {
		IDC_BTN_GET_ALL, IDC_BTN_CANCEL_BATCH, IDC_BTN_SYNC_CACHE, IDC_BTN_REFRESH_TRACK
	};
	const int btn_count = (int)(sizeof(kBtnIds) / sizeof(kBtnIds[0]));
	const int y_status = rc.bottom - tl.bottom_margin - tl.status_h;
	const int y_btn = y_status - tl.gap_btn_status - tl.btn_h;
	const int total_w = rc.Width() - 2 * tl.side_margin - (btn_count - 1) * tl.gap;
	int label_min_w = 0;
	{
		HFONT dlg_font = fb2k_query_font(ui_font_default);
		if (dlg_font == NULL) dlg_font = GetFont();
		if (dlg_font) label_min_w = measure_max_button_label_cx(m_hWnd, dlg_font, tl.btn_pad_x);
	}
	const int min_w = pfc::max_t<int>(tl.min_btn_w, label_min_w);
	int btn_w = btn_count > 0 ? total_w / btn_count : 0;
	if (btn_w < min_w) btn_w = min_w;
	int row_w = btn_count > 0 ? btn_w * btn_count + (btn_count - 1) * tl.gap : 0;
	if (row_w > total_w && btn_count > 0) {
		btn_w = total_w / btn_count;
		row_w = btn_w * btn_count + (btn_count - 1) * tl.gap;
	}
	int x = tl.side_margin;
	if (row_w < total_w)
		x += (total_w - row_w) / 2;
	for (int i = 0; i < btn_count; ++i) {
		CWindow btn = GetDlgItem(kBtnIds[i]);
		if (btn.m_hWnd != NULL)
			btn.MoveWindow(x, y_btn, btn_w, tl.btn_h, TRUE);
		x += btn_w + tl.gap;
	}
	CWindow status = GetDlgItem(IDC_STATIC_STATUS);
	if (status.m_hWnd != NULL)
		status.MoveWindow(tl.side_margin, y_status, rc.Width() - 2 * tl.side_margin, tl.status_h, TRUE);
}

void CLyricsWindow::layout_sync_highlight_button() {
	if (!m_btn_sync_highlight.m_hWnd) return;
	const bool show = cache_has_sync_timestamps() && has_lyric_display_rows();
	m_btn_sync_highlight.ShowWindow(show ? SW_SHOW : SW_HIDE);
	if (!show) return;
	if (!m_lyrics_view.m_hWnd) return;

	CRect rcLyrics;
	m_lyrics_view.GetWindowRect(rcLyrics);
	ScreenToClient(rcLyrics);
	const int scroll_w = GetSystemMetrics(SM_CXVSCROLL);
	const int inset = scale_px_y(m_hWnd, kSyncHighlightInset96);
	const int scroll_gap = scale_px_y(m_hWnd, kSyncHighlightScrollGap96);
	int btn_w = 0;
	int btn_h = 0;
	measure_sync_highlight_button_size(m_hWnd, btn_w, btn_h);
	const int x = rcLyrics.right - scroll_w - scroll_gap - btn_w;
	const int y = rcLyrics.top + inset;
	m_btn_sync_highlight.MoveWindow(x, y, btn_w, btn_h, TRUE);
	m_btn_sync_highlight.SetWindowPos(HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void CLyricsWindow::layout_lyrics_view() {
	if (!m_lyrics_view.m_hWnd) return;
	CRect rcClient;
	GetClientRect(rcClient);
	const toolbar_layout_t tl = toolbar_layout_for(m_hWnd);
	const int bottom = rcClient.Height() - tl.reserved_height();
	CRect rc(tl.side_margin, tl.side_margin, rcClient.Width() - tl.side_margin,
		bottom > tl.side_margin ? bottom : rcClient.Height());
	m_lyrics_view.MoveWindow(rc, TRUE);
	layout_sync_highlight_button();
}

void CLyricsWindow::update_sync_highlight_button() {
	layout_sync_highlight_button();
	if (m_btn_sync_highlight.m_hWnd != NULL)
		m_btn_sync_highlight.Invalidate(FALSE);
	setup_button_tooltips();
}

void CLyricsWindow::OnGetMinMaxInfo(LPMINMAXINFO mmi) {
	if (mmi == NULL) return;
	const toolbar_layout_t tl = toolbar_layout_for(m_hWnd);
	CRect rcWnd;
	GetWindowRect(rcWnd);
	CRect rcClient;
	GetClientRect(rcClient);
	const int nonclient_h = (rcWnd.Height() - rcClient.Height());
	const int min_client_h = scale_px_y(m_hWnd, 220) + tl.reserved_height();
	mmi->ptMinTrackSize.x = scale_px_y(m_hWnd, 420);
	mmi->ptMinTrackSize.y = min_client_h + nonclient_h;
}

void CLyricsWindow::refresh_lyrics_view() {
	if (!m_lyrics_view.m_hWnd) return;
	m_lyrics_view.set_rows(&m_display_rows, m_active_line);
}

void CLyricsWindow::sync_dark_mode() {
	m_dark.SetDark(effective_dark_ui());
	redraw_list();
	if (m_btn_sync_highlight.m_hWnd != NULL)
		m_btn_sync_highlight.Invalidate(FALSE);
	Invalidate(FALSE);
}

BOOL CLyricsWindow::OnInitDialog(CWindow, LPARAM) {
	g_window = this;
	{
		CWindow listbox = GetDlgItem(IDC_LYRICS_LIST);
		CRect rc;
		listbox.GetWindowRect(rc);
		ScreenToClient(rc);
		listbox.DestroyWindow();
		m_lyrics_view.Create(m_hWnd, rc, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL);
	}
	int btn_w = 0;
	int btn_h = 0;
	measure_sync_highlight_button_size(m_hWnd, btn_w, btn_h);
	m_btn_sync_highlight = ::CreateWindowExW(
		0, L"BUTTON", L"",
		WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
		0, 0, btn_w, btn_h, m_hWnd, (HMENU)(UINT_PTR)IDC_BTN_SYNC_HIGHLIGHT,
		GetModuleHandle(NULL), NULL);
	m_dark.AddDialogWithControls(m_hWnd);
	{
		CWindow status = GetDlgItem(IDC_STATIC_STATUS);
		if (status.m_hWnd != NULL)
			status.ModifyStyle(0, SS_NOTIFY);
	}
	apply_fb2k_ui();
	plugin_config::load(m_settings);
	register_playback();
	SetTimer(ID_TIMER_HIGHLIGHT, 200, NULL);
	set_batch_ui_active(false);
	setup_button_tooltips();
	refresh_web_info();
	m_web_info_last_fetch = GetTickCount();
	refresh_track();
	::ShowWindowCentered(*this, core_api::get_main_window());
	return TRUE;
}

void CLyricsWindow::OnSize(UINT, CSize) {
	layout_bottom_controls();
	layout_lyrics_view();
	refresh_lyrics_view();
	setup_button_tooltips();
}

void CLyricsWindow::OnDestroy() {
	cancel_batch_fetch();
	KillTimer(ID_TIMER_HIGHLIGHT);
	m_lyrics_font.DeleteObject();
	unregister_playback();
	g_window = nullptr;
}

LRESULT CLyricsWindow::OnDpiChanged(UINT, WPARAM, LPARAM lParam, BOOL&) {
	const RECT* rc = reinterpret_cast<const RECT*>(lParam);
	if (rc != NULL)
		SetWindowPos(NULL, rc, SWP_NOZORDER | SWP_NOACTIVATE);
	apply_fb2k_ui();
	setup_button_tooltips();
	return TRUE;
}

void CLyricsWindow::OnCancel(UINT, int, CWindow) {
	DestroyWindow();
}

void CLyricsWindow::register_playback() {
	play_callback_reregister(
		flag_on_playback_new_track | flag_on_playback_seek | flag_on_playback_stop | flag_on_playback_time,
		true);
}

void CLyricsWindow::unregister_playback() {
	play_callback_reregister(0, false);
}

void CLyricsWindow::on_playback_new_track(metadb_handle_ptr) {
	m_no_retry_cache_path.reset();
	m_sync_highlight_enabled = true;
	m_active_line = -1;
	if (m_lyrics_view.m_hWnd)
		m_lyrics_view.set_active_source_line(-1);
	refresh_track();
	update_highlight(0.0);
	update_sync_highlight_button();
}

void CLyricsWindow::on_playback_seek(double) {
	update_highlight();
}

void CLyricsWindow::on_playback_stop(play_control::t_stop_reason) {
	update_highlight();
}

void CLyricsWindow::on_playback_time(double p_time) {
	update_highlight(p_time);
}

LRESULT CLyricsWindow::OnSetDarkMode(UINT, WPARAM, LPARAM) {
	sync_dark_mode();
	return 1;
}

bool CLyricsWindow::read_now_playing(track_info_t& out) {
	metadb_handle_ptr item;
	if (!m_playback->get_now_playing(item)) return false;
	if (!read_track_info(item, out)) return false;
	if (out.duration_sec <= 0) {
		const double playback_len = m_playback->playback_get_length();
		if (playback_len > 0)
			out.duration_sec = playback_len;
	}
	if (out.album.is_empty())
		console::printf("Lyrics: album tag missing; LRCLIB lookup will retry without album");
	return true;
}

bool CLyricsWindow::begin_lyrics_worker_fetch() {
	if (m_batch_active)
		return false;
	const pfc::string8 write_path = track_cache_path_write(m_settings, m_track);
	if (worker_launcher::is_busy()) {
		if (worker_launcher::is_running_for(write_path.get_ptr())) {
			show_list_message("Loading lyrics...");
			m_worker_pending = true;
			return true;
		}
		show_list_message("Lyrics worker is busy. Try again in a moment.");
		m_worker_pending = false;
		return true;
	}
	if (!worker_launcher::launch(m_settings, m_track, write_path.get_ptr())) {
		const char* err = worker_launcher::last_launch_error();
		show_list_message(err != NULL && err[0] != '\0' ? err : "Could not start lyrics_worker.exe.");
		m_worker_pending = false;
		return true;
	}
	show_list_message("Loading lyrics...");
	m_worker_pending = true;
	return true;
}

void CLyricsWindow::show_list_message(const char* msg) {
	m_display_rows.set_count(0);
	display_row_t row;
	row.original = msg;
	m_display_rows.append_single(row);
	m_active_line = -1;
	refresh_lyrics_view();
	update_window_title();
	publish_web_state(true);
}

void CLyricsWindow::reload_settings() {
	plugin_config::load(m_settings);
	m_no_retry_cache_path.reset();
	m_web_lan_url.reset();
	m_web_info_last_fetch = 0;
	update_sync_cache_button();
	setup_button_tooltips();
	if (!m_settings.web_enabled) {
		web_server_launcher::stop();
		refresh_status_bar();
	} else {
		web_server_launcher::ensure_running(m_settings);
		refresh_web_info();
		m_web_info_last_fetch = GetTickCount();
	}
	refresh_track();
}

void CLyricsWindow::refresh_track() {
	if (!read_now_playing(m_track)) {
		m_track = track_info_t{};
		update_window_title();
		show_list_message("No track playing.");
		return;
	}
	m_cache_path = track_cache_path_read(m_settings, m_track);
	console::printf("Lyrics: track %s / %s / %s", m_track.artist.get_ptr(), m_track.album.get_ptr(), m_track.title.get_ptr());
	console::printf("Lyrics: cache dir %s", m_settings.cache_dir.get_ptr());
	console::printf("Lyrics: cache file %s", m_cache_path.get_ptr());
	load_cache_and_display();
}

bool CLyricsWindow::should_block_translation(pfc::string8& out_message) {
	out_message.reset();
	if (!m_settings.enable_translation) return false;
	if (m_settings.llm_api_key.is_empty()) {
		out_message = "Translation is enabled but LLM API key is missing. Open Preferences.";
		return true;
	}
	return false;
}

void CLyricsWindow::display_original_only(const lyrics_cache_t& data, const char* status_line) {
	m_cache = data;
	m_active_line = -1;
	m_display_rows.set_count(0);

	for (t_size i = 0; i < m_cache.lines.get_size(); ++i) {
		const auto& line = m_cache.lines[i];
		if (line.original.is_empty()) continue;
		display_row_t row;
		row.original = line.original;
		row.source_line_index = (int)i;
		m_display_rows.append_single(row);
	}

	refresh_lyrics_view();
	m_worker_pending = false;
	if (status_line != NULL && status_line[0] != '\0')
		update_status_text(status_line);
	else
		update_status_text("Original only - translation unavailable");
	update_highlight();
	update_window_title();
	publish_web_state(true);
}

bool CLyricsWindow::try_show_session_original(const char* status_line) {
	pfc::string8 module_dir;
	if (!plugin_config::get_module_dir(module_dir)) return false;
	lyrics_cache_t preview;
	if (!lyrics_cache::load_session_display(module_dir.get_ptr(), m_cache_path.get_ptr(), preview))
		return false;
	display_original_only(preview, status_line);
	return true;
}

void CLyricsWindow::show_translation_session_failure() {
	m_no_retry_cache_path = m_cache_path;
	pfc::string8 module_dir;
	pfc::string8 err_msg;
	if (plugin_config::get_module_dir(module_dir))
		lyrics_cache::load_session_error(module_dir.get_ptr(), m_cache_path.get_ptr(), err_msg);

	pfc::string_formatter status;
	status << "Original only";
	if (!err_msg.is_empty()) status << " - " << err_msg;

	if (try_show_session_original(status.get_ptr()))
		;
	else if (!err_msg.is_empty())
		show_list_message(err_msg.get_ptr());
	else
		show_list_message("Translation unavailable.");
	m_worker_pending = false;
}

void CLyricsWindow::on_worker_finished() {
	const DWORD code = worker_launcher::last_exit_code();
	if (worker_launcher::is_session_exit_code(code)) {
		show_translation_session_failure();
		return;
	}
	if (lyrics_cache::is_ready(m_cache_path.get_ptr())) {
		load_cache_and_display();
		return;
	}
	if (!m_worker_pending) return;

	pfc::string8 err_msg;
	pfc::string8 module_dir;
	if (plugin_config::get_module_dir(module_dir) &&
		lyrics_cache::load_session_error(module_dir.get_ptr(), m_cache_path.get_ptr(), err_msg) &&
		!err_msg.is_empty()) {
		show_list_message(err_msg.get_ptr());
	} else if (const char* known = worker_launcher::exit_code_message(code)) {
		show_list_message(known);
	} else if (code != 0) {
		pfc::string_formatter fm;
		fm << "Worker failed (exit " << code << ").";
		show_list_message(fm);
	} else {
		show_list_message("Lyrics file was not created. Check cacheDir and foobar console.");
	}
	m_worker_pending = false;
}

void CLyricsWindow::load_cache_and_display() {
	m_cache = {};
	m_active_line = -1;
	m_display_rows.set_count(0);

	const bool no_retry = (m_no_retry_cache_path == m_cache_path);

	if (lyrics_cache::is_ready(m_cache_path.get_ptr())) {
		m_no_retry_cache_path.reset();
		if (!lyrics_cache::load_file(m_cache_path.get_ptr(), m_cache)) {
			show_list_message("Cache read error");
			return;
		}
	} else if (no_retry) {
		show_translation_session_failure();
		return;
	} else {
		pfc::string8 block_msg;
		if (should_block_translation(block_msg)) {
			show_list_message(block_msg.get_ptr());
			m_worker_pending = false;
			return;
		}
	}
	if (!lyrics_cache::load_file(m_cache_path.get_ptr(), m_cache)) {
		if (begin_lyrics_worker_fetch())
			return;
	}
	if (stricmp_utf8(m_cache.status.get_ptr(), "ready") != 0) {
		if (begin_lyrics_worker_fetch())
			return;
	} else {
		m_worker_pending = false;
	}

	const bool pairs = !m_cache.already_in_target_language;
	for (t_size i = 0; i < m_cache.lines.get_size(); ++i) {
		const auto& line = m_cache.lines[i];
		if (line.original.is_empty()) continue;
		display_row_t row;
		row.original = line.original;
		row.source_line_index = (int)i;
		if (pairs && !line.translation.is_empty() && stricmp_utf8(line.translation.get_ptr(), line.original.get_ptr()) != 0)
			row.translation = line.translation;
		m_display_rows.append_single(row);
	}

	refresh_lyrics_view();
	update_highlight();
	update_window_title();
	update_sync_highlight_button();
	publish_web_state(true);
}

void CLyricsWindow::OnSyncHighlightToggle(UINT, int, CWindow) {
	if (!cache_has_sync_timestamps()) return;
	m_sync_highlight_enabled = !m_sync_highlight_enabled;
	if (!m_sync_highlight_enabled) {
		m_active_line = -1;
		if (m_lyrics_view.m_hWnd)
			m_lyrics_view.set_active_source_line(-1);
	} else {
		update_highlight();
	}
	update_sync_highlight_button();
	publish_web_state(true);
}

void CLyricsWindow::OnDrawItem(UINT, LPDRAWITEMSTRUCT dis) {
	if (dis == NULL || dis->CtlID != IDC_BTN_SYNC_HIGHLIGHT) return;
	CDCHandle dc(dis->hDC);
	CRect rc(dis->rcItem);
	const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
	draw_sync_highlight_button_bg(dc, m_hWnd, rc, pressed);
	const COLORREF text = fb2k_query_ui_color(ui_color_text, COLOR_WINDOWTEXT);
	const COLORREF label = m_sync_highlight_enabled ? text : RGB(230, 80, 80);
	draw_sync_highlight_label(dc, m_hWnd, rc, m_sync_highlight_enabled, pressed, label);
}

void CLyricsWindow::update_highlight(double pos_sec_override) {
	if (m_display_rows.get_size() == 0 || m_cache.lines.get_size() == 0) return;

	if (!cache_has_sync_timestamps()) {
		if (m_active_line != -1) {
			m_active_line = -1;
			if (m_lyrics_view.m_hWnd)
				m_lyrics_view.set_active_source_line(-1);
		}
		return;
	}

	if (!m_sync_highlight_enabled) {
		if (m_active_line != -1) {
			m_active_line = -1;
			if (m_lyrics_view.m_hWnd)
				m_lyrics_view.set_active_source_line(-1);
		}
		return;
	}

	double pos_sec = pos_sec_override;
	if (pos_sec < 0.0) {
		if (!m_playback->is_playing() && !m_playback->is_paused())
			return;
		pos_sec = m_playback->playback_get_position();
	}
	const int pos_ms = (int)(pos_sec * 1000.0 + 0.5);

	int best_cache = -1;
	for (t_size i = 0; i < m_cache.lines.get_size(); ++i) {
		const int t = m_cache.lines[i].time_ms;
		if (t > 0 && t <= pos_ms)
			best_cache = (int)i;
	}

	int idx = -1;
	for (t_size i = 0; i < m_display_rows.get_size(); ++i) {
		const int src = m_display_rows[i].source_line_index;
		if (src < 0 || src >= (int)m_cache.lines.get_size()) continue;
		if (best_cache >= 0) {
			if (src <= best_cache)
				idx = src;
		} else if (m_cache.lines[src].time_ms > 0 && m_cache.lines[src].time_ms <= pos_ms) {
			idx = src;
		}
	}

	if (idx == m_active_line) return;
	m_active_line = idx;
	if (m_lyrics_view.m_hWnd)
		m_lyrics_view.set_active_source_line(m_active_line);
}

bool CLyricsWindow::collect_active_playlist_queue() {
	m_batch_queue.remove_all();
	m_batch_pending = 0;
	m_batch_skipped_ready = 0;
	m_batch_skipped_dup = 0;
	m_batch_skipped_no_album = 0;
	m_batch_done = 0;
	m_batch_worker_was_busy = false;

	const t_size pl_index = m_playlist_mgr->get_active_playlist();
	if (pl_index == SIZE_MAX) {
		update_status_text("No active playlist");
		return false;
	}

	pfc::list_t<pfc::string8> seen_paths;

	class enum_cb : public playlist_manager::enum_items_callback {
	public:
		enum_cb(pfc::list_t<track_info_t>* q, pfc::list_t<pfc::string8>* seen, const plugin_settings_t* s,
			t_size* skipped_ready, t_size* skipped_dup, t_size* skipped_no_album)
			: m_queue(q), m_seen(seen), m_settings(s),
			m_skipped_ready(skipped_ready), m_skipped_dup(skipped_dup), m_skipped_no_album(skipped_no_album) {}

		bool on_item(t_size, const metadb_handle_ptr& handle, bool) override {
			track_info_t tr;
			if (!read_track_info(handle, tr)) return true;
			if (tr.album.is_empty()) {
				++(*m_skipped_no_album);
				return true;
			}
			pfc::string8 path = track_cache_path_read(*m_settings, tr);
			if (m_seen->find_item(path) != SIZE_MAX) {
				++(*m_skipped_dup);
				return true;
			}
			m_seen->add_item(path);
			if (lyrics_cache::is_ready(path.get_ptr())) {
				++(*m_skipped_ready);
				return true;
			}
			m_queue->add_item(tr);
			return true;
		}

	private:
		pfc::list_t<track_info_t>* m_queue;
		pfc::list_t<pfc::string8>* m_seen;
		const plugin_settings_t* m_settings;
		t_size* m_skipped_ready;
		t_size* m_skipped_dup;
		t_size* m_skipped_no_album;
	};

	enum_cb cb(&m_batch_queue, &seen_paths, &m_settings, &m_batch_skipped_ready, &m_batch_skipped_dup, &m_batch_skipped_no_album);
	m_playlist_mgr->playlist_enum_items(pl_index, cb, bit_array_true());

	m_batch_pending = m_batch_queue.get_count();
	return m_batch_pending > 0;
}

void CLyricsWindow::start_batch_fetch() {
	if (m_batch_active) return;
	plugin_config::load(m_settings);
	if (!collect_active_playlist_queue()) {
		if (m_batch_skipped_ready > 0 && m_batch_skipped_no_album == 0)
			update_status_text("All tracks already cached");
		return;
	}
	m_batch_active = true;
	m_batch_cancelled = false;
	set_batch_ui_active(true);
	m_batch_done = 0;
	m_batch_worker_was_busy = worker_launcher::is_busy();
	console::printf("Lyrics: batch fetch started, %u tracks queued", (unsigned)m_batch_pending);
	launch_next_batch_job();
	tick_batch_fetch();
}

void CLyricsWindow::cancel_batch_fetch() {
	if (!m_batch_active) return;
	m_batch_cancelled = true;
	m_batch_active = false;
	m_batch_queue.remove_all();
	set_batch_ui_active(false);
	update_status_text("Cancelled");
}

void CLyricsWindow::launch_next_batch_job() {
	if (!m_batch_active || m_batch_cancelled) return;
	if (worker_launcher::is_busy()) return;
	while (m_batch_queue.get_count() > 0) {
		track_info_t tr = m_batch_queue.get_item(0);
		m_batch_queue.remove_by_idx(0);
		pfc::string8 read_path = track_cache_path_read(m_settings, tr);
		if (lyrics_cache::is_ready(read_path.get_ptr())) {
			m_batch_skipped_ready++;
			continue;
		}
		worker_launcher::launch(m_settings, tr, track_cache_path_write(m_settings, tr).get_ptr());
		return;
	}
}

void CLyricsWindow::tick_batch_fetch() {
	if (!m_batch_active || m_batch_cancelled) return;

	const bool busy = worker_launcher::is_busy();
	if (m_batch_worker_was_busy && !busy) {
		m_batch_done++;
		launch_next_batch_job();
	}
	m_batch_worker_was_busy = busy;

	pfc::string_formatter status;
	status << "Batch: " << m_batch_done << "/" << m_batch_pending;
	if (m_batch_skipped_ready > 0) status << " (" << m_batch_skipped_ready << " already cached)";
	if (busy) status << " — working";
	update_status_text(status);

	if (m_batch_queue.get_count() == 0 && !busy) {
		m_batch_active = false;
		set_batch_ui_active(false);
		status.reset();
		status << "Done: " << m_batch_done << "/" << m_batch_pending;
		if (m_batch_skipped_ready > 0) status << ", " << m_batch_skipped_ready << " cached";
		if (m_batch_skipped_no_album > 0) status << ", " << m_batch_skipped_no_album << " no album tag";
		update_status_text(status);
		console::printf("Lyrics: batch fetch finished");
		refresh_track();
	}
}

void CLyricsWindow::OnGetAll(UINT, int, CWindow) {
	start_batch_fetch();
}

void CLyricsWindow::OnCancelBatch(UINT, int, CWindow) {
	cancel_batch_fetch();
}

void CLyricsWindow::OnSyncCache(UINT, int, CWindow) {
	plugin_config::load(m_settings);
	if (!git_sync::start_push(m_settings)) {
		const char* msg = git_sync::last_status_message();
		update_status_text(msg != NULL && msg[0] != '\0' ? msg : "Cache sync failed to start.");
	}
}

void CLyricsWindow::refetch_current_track() {
	if (!read_now_playing(m_track)) {
		update_status_text("No track playing.");
		return;
	}
	m_cache_path = track_cache_path_read(m_settings, m_track);
	m_no_retry_cache_path.reset();
	lyrics_cache::delete_cache_file(m_cache_path.get_ptr());
	{
		const pfc::string8 write_path = track_cache_path_write(m_settings, m_track);
		if (stricmp_utf8(write_path.get_ptr(), m_cache_path.get_ptr()) != 0)
			lyrics_cache::delete_cache_file(write_path.get_ptr());
	}
	pfc::string8 module_dir;
	if (plugin_config::get_module_dir(module_dir))
		lyrics_cache::clear_session_files(module_dir.get_ptr(), m_cache_path.get_ptr());
	m_worker_pending = false;
	update_status_text("Reloading lyrics...");
	load_cache_and_display();
}

void CLyricsWindow::OnRefreshTrack(UINT, int, CWindow) {
	refetch_current_track();
}

void CLyricsWindow::OnTimer(UINT_PTR id) {
	if (id != ID_TIMER_HIGHLIGHT) return;
	const DWORD now = GetTickCount();
	if (m_status_transient.length() > 0 && (int)(now - m_status_transient_until) >= 0)
		refresh_status_bar();
	if (m_settings.web_enabled && web_server_launcher::is_running()) {
		if (m_web_lan_url.is_empty() ||
			(m_web_info_last_fetch != 0 && (now - m_web_info_last_fetch) >= kWebInfoRefreshMs)) {
			refresh_web_info();
			m_web_info_last_fetch = now;
		}
	}
	git_sync::refresh_process_state();
	worker_launcher::refresh_process_state();
	if (m_batch_active) tick_batch_fetch();
	if (m_worker_pending && !m_batch_active) {
		if (!worker_launcher::is_busy())
			on_worker_finished();
	}
	update_highlight();
	poll_web_highlight_control();
	poll_web_player_control();
	publish_web_state(false);
}

} // namespace

void lyrics_window_reload_settings() {
	if (g_window != nullptr) g_window->reload_settings();
}

void lyrics_window_set_status_from_git(const char* text) {
	if (g_window != nullptr && text != NULL)
		g_window->update_status_text(text);
}

void lyrics_window_reload_after_sync() {
	if (g_window != nullptr) {
		g_window->reload_settings();
		g_window->refresh_track();
	}
}

void lyrics_window_open_or_focus() {
	if (g_window != nullptr) {
		::SetForegroundWindow(g_window->m_hWnd);
		return;
	}
	try {
		new CWindowAutoLifetime<ImplementModelessTracking<CLyricsWindow>>(core_api::get_main_window());
	} catch (std::exception const& e) {
		popup_message::g_complain("Lyrics window", e);
	}
}
