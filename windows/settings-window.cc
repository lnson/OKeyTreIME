// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/settings-window.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "windows/resources/resource.h"

namespace okeytre::win {

namespace {

constexpr wchar_t kSettingsClassName[] = L"OKeyTreSettingsWindowClass";

enum ControlId : int {
  kIdComboLanguage = 2001,
  kIdComboStyle = 2002,
  kIdEditBufferSize = 2003,
  kIdUpDownBufferSize = 2004,
};

constexpr int kSidebarWidthDip = 220;
constexpr int kDefaultWidthDip = 720;
constexpr int kDefaultHeightDip = 520;
constexpr int kMinWidthDip = 660;
constexpr int kMinHeightDip = 460;

std::wstring UiFontName() {
  static const std::wstring kName = []() -> std::wstring {
    Gdiplus::FontFamily ff(L"Segoe UI Variable Text");
    return ff.IsAvailable() ? L"Segoe UI Variable Text" : L"Segoe UI";
  }();
  return kName;
}

void DrawNavBadge(Gdiplus::Graphics& g, const Gdiplus::RectF& rect,
                  const Gdiplus::Color& bg, SettingsSection sec, UINT dpi) {
  FillRoundedRect(g, rect, ScaleDpiF(5.0f, dpi), bg);

  const float cx = rect.X + rect.Width * 0.5f;
  const float cy = rect.Y + rect.Height * 0.5f;
  const float r = rect.Width * 0.24f;
  Gdiplus::Pen pen(Gdiplus::Color(255, 255, 255, 255), ScaleDpiF(1.5f, dpi));
  pen.SetStartCap(Gdiplus::LineCapRound);
  pen.SetEndCap(Gdiplus::LineCapRound);

  switch (sec) {
    case SettingsSection::kGeneral: {
      // Gear ring
      g.DrawEllipse(&pen, cx - r, cy - r, r * 2.0f, r * 2.0f);
      const float inner = r * 0.45f;
      g.DrawEllipse(&pen, cx - inner, cy - inner, inner * 2.0f, inner * 2.0f);
      break;
    }
    case SettingsSection::kCorrection: {
      // Sparkle star
      g.DrawLine(&pen, cx, cy - r * 1.2f, cx, cy + r * 1.2f);
      g.DrawLine(&pen, cx - r * 1.2f, cy, cx + r * 1.2f, cy);
      const float d = r * 0.72f;
      g.DrawLine(&pen, cx - d, cy - d, cx + d, cy + d);
      g.DrawLine(&pen, cx + d, cy - d, cx - d, cy + d);
      break;
    }
    case SettingsSection::kAdvanced: {
      // Two horizontal sliders
      const float half = r * 1.2f;
      g.DrawLine(&pen, cx - half, cy - r * 0.55f, cx + half, cy - r * 0.55f);
      g.DrawLine(&pen, cx - half, cy + r * 0.55f, cx + half, cy + r * 0.55f);
      Gdiplus::SolidBrush dot(Gdiplus::Color(255, 255, 255, 255));
      const float kr = ScaleDpiF(2.0f, dpi);
      g.FillEllipse(&dot, cx + r * 0.3f - kr, cy - r * 0.55f - kr, kr * 2.0f,
                    kr * 2.0f);
      g.FillEllipse(&dot, cx - r * 0.35f - kr, cy + r * 0.55f - kr, kr * 2.0f,
                    kr * 2.0f);
      break;
    }
  }
}

void DrawTextLeft(Gdiplus::Graphics& g, std::wstring_view text,
                  const Gdiplus::Font& font, const Gdiplus::Color& color,
                  const Gdiplus::RectF& rect, bool wrap = false) {
  Gdiplus::SolidBrush brush(color);
  Gdiplus::StringFormat fmt;
  fmt.SetAlignment(Gdiplus::StringAlignmentNear);
  fmt.SetLineAlignment(wrap ? Gdiplus::StringAlignmentNear
                            : Gdiplus::StringAlignmentCenter);
  if (!wrap) {
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
  }
  g.DrawString(text.data(), static_cast<INT>(text.size()), &font, rect, &fmt,
               &brush);
}

}  // namespace

SettingsWindow::SettingsWindow(SettingsCallbacks callbacks)
    : callbacks_(std::move(callbacks)),
      palette_(GetThemePalette(IsAppDarkMode())) {}

SettingsWindow::~SettingsWindow() {
  Close();
  if (ui_font_ != nullptr) DeleteObject(ui_font_);
  if (brush_sidebar_ != nullptr) DeleteObject(brush_sidebar_);
  if (brush_card_ != nullptr) DeleteObject(brush_card_);
}

void SettingsWindow::EnsureWindowClass() {
  static bool registered = false;
  if (registered) return;

  HINSTANCE hinst = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &SettingsWindow::WindowProc;
  wc.hInstance = hinst;
  wc.hIcon = LoadIconW(hinst, MAKEINTRESOURCEW(IDI_APP_ICON));
  wc.hIconSm = wc.hIcon;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kSettingsClassName;
  RegisterClassExW(&wc);
  registered = true;
}

void SettingsWindow::Show(const SettingsViewModel& model) {
  model_ = model;
  palette_ = GetThemePalette(IsAppDarkMode());

  if (hwnd_ == nullptr) {
    EnsureWindowClass();
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    dpi_ = GetDpiForSystem();

    RECT wr{0, 0, ScaleDpi(kDefaultWidthDip, dpi_),
            ScaleDpi(kDefaultHeightDip, dpi_)};
    const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    AdjustWindowRectExForDpi(&wr, style, FALSE, 0, dpi_);

    const int win_w = wr.right - wr.left;
    const int win_h = wr.bottom - wr.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - win_w) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - win_h) / 2;

    const std::wstring title =
        Localized(model_.language, "settings.window_title");
    hwnd_ = CreateWindowExW(0, kSettingsClassName, title.c_str(), style, x, y,
                            win_w, win_h, nullptr, nullptr, hinst, this);
    if (hwnd_ == nullptr) {
      return;
    }

    dpi_ = GetDpiForWindow(hwnd_);
    ApplyWindowThemeAttributes(hwnd_, palette_.dark);
    UpdateFontsAndBrushes();
    CreateChildControls();
    SyncControlsFromModel();
    LayoutChildControls();
  } else {
    Update(model_);
  }

  if (IsIconic(hwnd_)) {
    ShowWindow(hwnd_, SW_RESTORE);
  } else {
    ShowWindow(hwnd_, SW_SHOW);
  }
  SetForegroundWindow(hwnd_);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::Update(const SettingsViewModel& model) {
  model_ = model;
  if (hwnd_ == nullptr) {
    return;
  }
  const std::wstring title =
      Localized(model_.language, "settings.window_title");
  SetWindowTextW(hwnd_, title.c_str());
  SyncControlsFromModel();
  LayoutChildControls();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::RefreshTheme() {
  palette_ = GetThemePalette(IsAppDarkMode());
  if (hwnd_ == nullptr) {
    return;
  }
  ApplyWindowThemeAttributes(hwnd_, palette_.dark);
  UpdateFontsAndBrushes();
  InvalidateRect(hwnd_, nullptr, TRUE);
}

void SettingsWindow::Close() {
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

bool SettingsWindow::IsVisible() const {
  return hwnd_ != nullptr && IsWindowVisible(hwnd_);
}

void SettingsWindow::UpdateFontsAndBrushes() {
  if (ui_font_ != nullptr) DeleteObject(ui_font_);
  if (brush_sidebar_ != nullptr) DeleteObject(brush_sidebar_);
  if (brush_card_ != nullptr) DeleteObject(brush_card_);

  ui_font_ = CreateUiFont(9, FW_NORMAL, dpi_);
  brush_sidebar_ = CreateSolidBrush(palette_.win32_sidebar_bg);
  brush_card_ = CreateSolidBrush(palette_.win32_card_bg);

  for (HWND child :
       {combo_language_, combo_style_, edit_buffer_size_, updown_buffer_size_}) {
    if (child != nullptr) {
      SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
    }
  }
}

void SettingsWindow::CreateChildControls() {
  HINSTANCE hinst = GetModuleHandleW(nullptr);

  combo_language_ = CreateWindowExW(
      0, WC_COMBOBOXW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 120, 160,
      hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdComboLanguage)),
      hinst, nullptr);

  combo_style_ = CreateWindowExW(
      0, WC_COMBOBOXW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 120, 160,
      hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdComboStyle)),
      hinst, nullptr);

  edit_buffer_size_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, WC_EDITW, L"16",
      WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_RIGHT | ES_AUTOHSCROLL, 0, 0, 64,
      26, hwnd_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEditBufferSize)), hinst,
      nullptr);

  updown_buffer_size_ = CreateWindowExW(
      0, UPDOWN_CLASSW, L"",
      WS_CHILD | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK, 0, 0, 18, 26,
      hwnd_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdUpDownBufferSize)), hinst,
      nullptr);

  SendMessageW(updown_buffer_size_, UDM_SETBUDDY,
               reinterpret_cast<WPARAM>(edit_buffer_size_), 0);
  SendMessageW(updown_buffer_size_, UDM_SETRANGE32, model_.min_buffer_size,
               model_.max_allowed_buffer_size);

  UpdateFontsAndBrushes();
}

void SettingsWindow::SyncControlsFromModel() {
  if (hwnd_ == nullptr) return;
  updating_controls_ = true;

  // Populate Language ComboBox.
  if (combo_language_ != nullptr) {
    SendMessageW(combo_language_, CB_RESETCONTENT, 0, 0);
    const std::wstring s_sys =
        LanguageDisplayName(AppLanguage::kSystem, model_.language);
    const std::wstring s_vi =
        LanguageDisplayName(AppLanguage::kVietnamese, model_.language);
    const std::wstring s_en =
        LanguageDisplayName(AppLanguage::kEnglish, model_.language);
    SendMessageW(combo_language_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(s_sys.c_str()));
    SendMessageW(combo_language_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(s_vi.c_str()));
    SendMessageW(combo_language_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(s_en.c_str()));
    SendMessageW(combo_language_, CB_SETCURSEL,
                 static_cast<WPARAM>(model_.language), 0);
  }

  // Populate Input Style ComboBox.
  if (combo_style_ != nullptr) {
    SendMessageW(combo_style_, CB_RESETCONTENT, 0, 0);
    const InputStyle styles[] = {InputStyle::kOff, InputStyle::kTelex,
                                 InputStyle::kVNI, InputStyle::kVIQR};
    for (InputStyle s : styles) {
      const std::wstring name = InputStyleDisplayName(s, model_.language);
      SendMessageW(combo_style_, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(name.c_str()));
    }
    SendMessageW(combo_style_, CB_SETCURSEL, static_cast<WPARAM>(model_.style),
                 0);
  }

  // Sync Buffer Size Edit & UpDown.
  if (edit_buffer_size_ != nullptr) {
    const std::wstring num_str = std::to_wstring(model_.max_buffer_size);
    SetWindowTextW(edit_buffer_size_, num_str.c_str());
    SendMessageW(updown_buffer_size_, UDM_SETPOS32, 0, model_.max_buffer_size);
  }

  updating_controls_ = false;
}

void SettingsWindow::LayoutChildControls() {
  if (hwnd_ == nullptr) return;

  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const int width = rc.right - rc.left;
  const int height = rc.bottom - rc.top;
  const int sidebar_w = ScaleDpi(kSidebarWidthDip, dpi_);

  // Sidebar footer language combobox (positioned above the 64px app logo).
  if (combo_language_ != nullptr) {
    const int cb_w = ScaleDpi(136, dpi_);
    const int cb_h = ScaleDpi(26, dpi_);
    const int cb_x = (sidebar_w - cb_w) / 2 + ScaleDpi(10, dpi_);
    const int cb_y = height - ScaleDpi(156, dpi_);
    MoveWindow(combo_language_, cb_x, cb_y, cb_w, ScaleDpi(180, dpi_), TRUE);
    SendMessageW(combo_language_, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
                 cb_h - ScaleDpi(6, dpi_));
  }

  const int content_x = sidebar_w + ScaleDpi(24, dpi_);
  const int content_w = std::max(ScaleDpi(320, dpi_),
                                 width - sidebar_w - ScaleDpi(48, dpi_));

  // General pane Input Style combobox.
  if (combo_style_ != nullptr) {
    const bool show = (section_ == SettingsSection::kGeneral);
    ShowWindow(combo_style_, show ? SW_SHOW : SW_HIDE);
    if (show) {
      const int cb_w = ScaleDpi(118, dpi_);
      const int cb_x = content_x + content_w - ScaleDpi(16, dpi_) - cb_w;
      const int cb_y = ScaleDpi(106, dpi_);
      MoveWindow(combo_style_, cb_x, cb_y, cb_w, ScaleDpi(180, dpi_), TRUE);
    }
  }

  // Advanced pane Buffer Size edit + updown.
  if (edit_buffer_size_ != nullptr && updown_buffer_size_ != nullptr) {
    const bool show = (section_ == SettingsSection::kAdvanced);
    ShowWindow(edit_buffer_size_, show ? SW_SHOW : SW_HIDE);
    ShowWindow(updown_buffer_size_, show ? SW_SHOW : SW_HIDE);
    if (show) {
      const int edit_w = ScaleDpi(74, dpi_);
      const int edit_h = ScaleDpi(26, dpi_);
      const int edit_x = content_x + content_w - ScaleDpi(16, dpi_) - edit_w;
      const int edit_y = ScaleDpi(112, dpi_);
      MoveWindow(edit_buffer_size_, edit_x, edit_y, edit_w, edit_h, TRUE);
      SendMessageW(updown_buffer_size_, UDM_SETBUDDY,
                   reinterpret_cast<WPARAM>(edit_buffer_size_), 0);
    }
  }
}

void SettingsWindow::Paint() {
  PAINTSTRUCT ps{};
  HDC hdc = BeginPaint(hwnd_, &ps);
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const int width = rc.right - rc.left;
  const int height = rc.bottom - rc.top;
  if (width <= 0 || height <= 0) {
    EndPaint(hwnd_, &ps);
    return;
  }

  HDC mem_dc = CreateCompatibleDC(hdc);
  HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, width, height);
  HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

  {
    Gdiplus::Graphics g(mem_dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    hit_regions_.clear();
    PaintSidebar(g, width, height);
    PaintDetailPane(g, width, height);
  }

  BitBlt(hdc, 0, 0, width, height, mem_dc, 0, 0, SRCCOPY);
  SelectObject(mem_dc, old_bmp);
  DeleteObject(mem_bmp);
  DeleteDC(mem_dc);
  EndPaint(hwnd_, &ps);
}

void SettingsWindow::PaintSidebar(Gdiplus::Graphics& g, int /*width*/,
                                  int height) {
  const float sidebar_w = static_cast<float>(ScaleDpi(kSidebarWidthDip, dpi_));
  Gdiplus::SolidBrush sb_brush(palette_.sidebar_bg);
  g.FillRectangle(&sb_brush, 0.0f, 0.0f, sidebar_w,
                  static_cast<float>(height));

  // Hairline vertical divider between sidebar and detail pane.
  Gdiplus::Pen div_pen(palette_.card_border, 1.0f);
  g.DrawLine(&div_pen, sidebar_w - 0.5f, 0.0f, sidebar_w - 0.5f,
             static_cast<float>(height));

  const std::wstring font_name = UiFontName();
  Gdiplus::Font nav_font(font_name.c_str(), ScaleDpiF(13.5f, dpi_),
                         Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::Font nav_font_sel(font_name.c_str(), ScaleDpiF(13.5f, dpi_),
                             Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

  struct NavItem {
    SettingsSection section;
    HitTargetKind target;
    std::string_view key;
    Gdiplus::Color badge_color;
  };
  const NavItem items[] = {
      {SettingsSection::kGeneral, HitTargetKind::kNavGeneral,
       "settings.section.general", palette_.accent_teal},
      {SettingsSection::kCorrection, HitTargetKind::kNavCorrection,
       "settings.section.correction", palette_.accent_blue},
      {SettingsSection::kAdvanced, HitTargetKind::kNavAdvanced,
       "settings.section.advanced", palette_.accent_gray},
  };

  float row_y = ScaleDpiF(18.0f, dpi_);
  const float row_h = ScaleDpiF(36.0f, dpi_);
  const float pad_x = ScaleDpiF(12.0f, dpi_);
  const float row_w = sidebar_w - pad_x * 2.0f;

  for (const auto& item : items) {
    Gdiplus::RectF row_rect(pad_x, row_y, row_w, row_h);
    const bool selected = (section_ == item.section);
    const bool hovered = (hovered_target_ == item.target);

    if (selected) {
      FillRoundedRect(g, row_rect, ScaleDpiF(7.0f, dpi_), palette_.selected_bg);
    } else if (hovered) {
      FillRoundedRect(g, row_rect, ScaleDpiF(7.0f, dpi_), palette_.hover_bg);
    }

    const float badge_sz = ScaleDpiF(22.0f, dpi_);
    Gdiplus::RectF badge_rect(row_rect.X + ScaleDpiF(10.0f, dpi_),
                              row_rect.Y + (row_h - badge_sz) * 0.5f, badge_sz,
                              badge_sz);
    DrawNavBadge(g, badge_rect, item.badge_color, item.section, dpi_);

    Gdiplus::RectF text_rect(badge_rect.GetRight() + ScaleDpiF(10.0f, dpi_),
                             row_rect.Y,
                             row_rect.Width - badge_sz - ScaleDpiF(26.0f, dpi_),
                             row_h);
    const std::wstring label = Localized(model_.language, item.key);
    DrawTextLeft(g, label, selected ? nav_font_sel : nav_font,
                 palette_.text_primary, text_rect);

    hit_regions_.push_back({item.target, row_rect});
    row_y += row_h + ScaleDpiF(4.0f, dpi_);
  }

  // Sidebar bottom footer: Globe glyph beside language combobox, app logo,
  // app name, and version string.
  const float globe_sz = ScaleDpiF(15.0f, dpi_);
  const float globe_x = (sidebar_w - ScaleDpiF(136.0f, dpi_)) * 0.5f -
                        ScaleDpiF(12.0f, dpi_);
  const float globe_y =
      static_cast<float>(height) - ScaleDpiF(151.0f, dpi_);
  Gdiplus::Pen globe_pen(palette_.text_secondary, ScaleDpiF(1.2f, dpi_));
  g.DrawEllipse(&globe_pen, globe_x, globe_y, globe_sz, globe_sz);
  g.DrawEllipse(&globe_pen, globe_x + globe_sz * 0.26f, globe_y,
                globe_sz * 0.48f, globe_sz);
  g.DrawLine(&globe_pen, globe_x, globe_y + globe_sz * 0.5f,
             globe_x + globe_sz, globe_y + globe_sz * 0.5f);

  const float logo_sz = ScaleDpiF(64.0f, dpi_);
  Gdiplus::RectF logo_rect((sidebar_w - logo_sz) * 0.5f,
                           static_cast<float>(height) - ScaleDpiF(116.0f, dpi_),
                           logo_sz, logo_sz);
  DrawAppLogo(g, logo_rect, ScaleDpiF(14.0f, dpi_));

  Gdiplus::Font app_font(font_name.c_str(), ScaleDpiF(13.5f, dpi_),
                         Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font ver_font(font_name.c_str(), ScaleDpiF(11.5f, dpi_),
                         Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::StringFormat center_fmt;
  center_fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
  center_fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

  const std::wstring app_name = Localized(model_.language, "app.name");
  Gdiplus::SolidBrush primary_brush(palette_.text_primary);
  Gdiplus::RectF name_rect(0.0f, logo_rect.GetBottom() + ScaleDpiF(6.0f, dpi_),
                           sidebar_w, ScaleDpiF(18.0f, dpi_));
  g.DrawString(app_name.c_str(), static_cast<INT>(app_name.size()), &app_font,
               name_rect, &center_fmt, &primary_brush);

  const std::wstring ver_str = LocalizedFormat(
      model_.language, "settings.sidebar.version", model_.current_version);
  Gdiplus::SolidBrush secondary_brush(palette_.text_secondary);
  Gdiplus::RectF ver_rect(0.0f, name_rect.GetBottom() + ScaleDpiF(1.0f, dpi_),
                          sidebar_w, ScaleDpiF(16.0f, dpi_));
  g.DrawString(ver_str.c_str(), static_cast<INT>(ver_str.size()), &ver_font,
               ver_rect, &center_fmt, &secondary_brush);
}

void SettingsWindow::PaintDetailPane(Gdiplus::Graphics& g, int width,
                                     int height) {
  const float sidebar_w = static_cast<float>(ScaleDpi(kSidebarWidthDip, dpi_));
  Gdiplus::SolidBrush bg_brush(palette_.window_bg);
  g.FillRectangle(&bg_brush, sidebar_w, 0.0f,
                  static_cast<float>(width) - sidebar_w,
                  static_cast<float>(height));

  const float x = sidebar_w + ScaleDpiF(24.0f, dpi_);
  const float w = std::max(ScaleDpiF(320.0f, dpi_),
                           static_cast<float>(width) - sidebar_w -
                               ScaleDpiF(48.0f, dpi_));

  const std::wstring font_name = UiFontName();
  Gdiplus::Font title_font(font_name.c_str(), ScaleDpiF(20.0f, dpi_),
                           Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

  std::string_view title_key = "settings.section.general";
  if (section_ == SettingsSection::kCorrection) {
    title_key = "settings.section.correction";
  } else if (section_ == SettingsSection::kAdvanced) {
    title_key = "settings.section.advanced";
  }

  Gdiplus::RectF title_rect(x, ScaleDpiF(16.0f, dpi_), w,
                            ScaleDpiF(30.0f, dpi_));
  DrawTextLeft(g, Localized(model_.language, title_key), title_font,
               palette_.text_primary, title_rect);

  const float body_y = ScaleDpiF(58.0f, dpi_);
  switch (section_) {
    case SettingsSection::kGeneral:
      PaintGeneralPane(g, x, body_y, w);
      break;
    case SettingsSection::kCorrection:
      PaintCorrectionPane(g, x, body_y, w);
      break;
    case SettingsSection::kAdvanced:
      PaintAdvancedPane(g, x, body_y, w);
      break;
  }
}

void SettingsWindow::PaintGeneralPane(Gdiplus::Graphics& g, float x, float y,
                                      float w) {
  const std::wstring font_name = UiFontName();
  Gdiplus::Font hdr_font(font_name.c_str(), ScaleDpiF(12.5f, dpi_),
                         Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font row_title_font(font_name.c_str(), ScaleDpiF(13.5f, dpi_),
                               Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::Font row_title_bold(font_name.c_str(), ScaleDpiF(13.5f, dpi_),
                               Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font desc_font(font_name.c_str(), ScaleDpiF(11.5f, dpi_),
                          Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

  const float card_r = ScaleDpiF(8.0f, dpi_);
  const float pad = ScaleDpiF(16.0f, dpi_);
  const float toggle_w = ScaleDpiF(40.0f, dpi_);
  const float toggle_h = ScaleDpiF(20.0f, dpi_);
  Gdiplus::Pen div_pen(palette_.divider, 1.0f);

  // --- Section 1: System ---
  DrawTextLeft(g, Localized(model_.language, "settings.general.system.header"),
               hdr_font, palette_.text_secondary,
               Gdiplus::RectF(x + ScaleDpiF(4.0f, dpi_), y, w,
                              ScaleDpiF(20.0f, dpi_)));
  y += ScaleDpiF(24.0f, dpi_);

  const float row1_h = ScaleDpiF(70.0f, dpi_);
  const float row2_h = ScaleDpiF(64.0f, dpi_);
  Gdiplus::RectF sys_card(x, y, w, row1_h + row2_h);
  FillRoundedRect(g, sys_card, card_r, palette_.card_bg);
  StrokeRoundedRect(g, sys_card, card_r, palette_.card_border, 1.0f);

  // Row 1: Input Method (ComboBox sits at right).
  const float text_w1 = w - pad * 2.0f - ScaleDpiF(134.0f, dpi_);
  DrawTextLeft(g, Localized(model_.language, "menu.input_style"),
               row_title_font, palette_.text_primary,
               Gdiplus::RectF(x + pad, y + ScaleDpiF(11.0f, dpi_), text_w1,
                              ScaleDpiF(20.0f, dpi_)));
  DrawTextLeft(
      g,
      Localized(model_.language, "settings.general.input_style.description"),
      desc_font, palette_.text_secondary,
      Gdiplus::RectF(x + pad, y + ScaleDpiF(32.0f, dpi_), text_w1,
                     ScaleDpiF(34.0f, dpi_)),
      /*wrap=*/true);

  const float div1_y = y + row1_h;
  g.DrawLine(&div_pen, x + pad, div1_y, x + w - pad, div1_y);

  // Row 2: Launch at Login toggle.
  const float text_w2 = w - pad * 2.0f - toggle_w - ScaleDpiF(16.0f, dpi_);
  DrawTextLeft(g, Localized(model_.language, "menu.launch_at_login"),
               row_title_font, palette_.text_primary,
               Gdiplus::RectF(x + pad, div1_y + ScaleDpiF(11.0f, dpi_), text_w2,
                              ScaleDpiF(20.0f, dpi_)));
  DrawTextLeft(g,
               Localized(model_.language,
                         "settings.general.launch_at_login.description"),
               desc_font, palette_.text_secondary,
               Gdiplus::RectF(x + pad, div1_y + ScaleDpiF(32.0f, dpi_), text_w2,
                              ScaleDpiF(28.0f, dpi_)),
               /*wrap=*/true);
  Gdiplus::RectF login_toggle(x + w - pad - toggle_w,
                              div1_y + (row2_h - toggle_h) * 0.5f, toggle_w,
                              toggle_h);
  DrawToggleSwitch(g, login_toggle, model_.launch_at_login,
                   hovered_target_ == HitTargetKind::kToggleLaunchAtLogin,
                   palette_);
  hit_regions_.push_back({HitTargetKind::kToggleLaunchAtLogin, login_toggle});

  // --- Section 2: Updates ---
  y = sys_card.GetBottom() + ScaleDpiF(20.0f, dpi_);
  DrawTextLeft(g, Localized(model_.language, "settings.updates.header"),
               hdr_font, palette_.text_secondary,
               Gdiplus::RectF(x + ScaleDpiF(4.0f, dpi_), y, w,
                              ScaleDpiF(20.0f, dpi_)));
  y += ScaleDpiF(24.0f, dpi_);

  const float u_row1_h = ScaleDpiF(66.0f, dpi_);
  const float u_row2_h = ScaleDpiF(58.0f, dpi_);
  const float u_row3_h = ScaleDpiF(66.0f, dpi_);
  Gdiplus::RectF upd_card(x, y, w, u_row1_h + u_row2_h + u_row3_h);
  FillRoundedRect(g, upd_card, card_r, palette_.card_bg);
  StrokeRoundedRect(g, upd_card, card_r, palette_.card_border, 1.0f);

  // Update Row 1: Auto-check toggle
  DrawTextLeft(
      g, Localized(model_.language, "settings.updates.auto_check.title"),
      row_title_font, palette_.text_primary,
      Gdiplus::RectF(x + pad, y + ScaleDpiF(10.0f, dpi_), text_w2,
                     ScaleDpiF(20.0f, dpi_)));
  DrawTextLeft(
      g, Localized(model_.language, "settings.updates.auto_check.description"),
      desc_font, palette_.text_secondary,
      Gdiplus::RectF(x + pad, y + ScaleDpiF(31.0f, dpi_), text_w2,
                     ScaleDpiF(32.0f, dpi_)),
      /*wrap=*/true);
  Gdiplus::RectF auto_toggle(x + w - pad - toggle_w,
                             y + (u_row1_h - toggle_h) * 0.5f, toggle_w,
                             toggle_h);
  DrawToggleSwitch(g, auto_toggle, model_.auto_check_updates,
                   hovered_target_ == HitTargetKind::kToggleAutoCheck,
                   palette_);
  hit_regions_.push_back({HitTargetKind::kToggleAutoCheck, auto_toggle});

  const float u_div1_y = y + u_row1_h;
  g.DrawLine(&div_pen, x + pad, u_div1_y, x + w - pad, u_div1_y);

  // Update Row 2: Status + Check / Download buttons
  const float btn_h = ScaleDpiF(28.0f, dpi_);
  const float check_btn_w = ScaleDpiF(126.0f, dpi_);
  const float btn_y = u_div1_y + (u_row2_h - btn_h) * 0.5f;
  float right_edge = x + w - pad;

  Gdiplus::RectF check_btn(right_edge - check_btn_w, btn_y, check_btn_w, btn_h);
  DrawFluentButton(
      g, check_btn,
      Localized(model_.language, "settings.sidebar.check_updates"),
      /*is_primary=*/false,
      hovered_target_ == HitTargetKind::kButtonCheckUpdates,
      model_.is_checking_updates, palette_, dpi_);
  hit_regions_.push_back({HitTargetKind::kButtonCheckUpdates, check_btn});
  right_edge = check_btn.X - ScaleDpiF(8.0f, dpi_);

  if (model_.available_release.has_value()) {
    const float dl_btn_w = ScaleDpiF(128.0f, dpi_);
    Gdiplus::RectF dl_btn(right_edge - dl_btn_w, btn_y, dl_btn_w, btn_h);
    DrawFluentButton(g, dl_btn,
                     Localized(model_.language, "action.download_update"),
                     /*is_primary=*/true,
                     hovered_target_ == HitTargetKind::kButtonDownloadUpdate,
                     /*is_disabled=*/false, palette_, dpi_);
    hit_regions_.push_back({HitTargetKind::kButtonDownloadUpdate, dl_btn});
    right_edge = dl_btn.X - ScaleDpiF(8.0f, dpi_);
  }

  const float status_w = std::max(ScaleDpiF(100.0f, dpi_), right_edge - x - pad);
  const bool has_sub =
      model_.is_checking_updates || model_.did_confirm_up_to_date;
  const float main_y =
      has_sub ? (u_div1_y + ScaleDpiF(10.0f, dpi_)) : (u_div1_y + ScaleDpiF(18.0f, dpi_));

  if (model_.available_release.has_value()) {
    const std::wstring avail = LocalizedFormat(
        model_.language, "settings.updates.available_status",
        Utf8ToWide(model_.available_release->display_version()));
    DrawTextLeft(g, avail, row_title_bold, palette_.text_primary,
                 Gdiplus::RectF(x + pad, main_y, status_w,
                                ScaleDpiF(20.0f, dpi_)));
  } else {
    const std::wstring cur = LocalizedFormat(
        model_.language, "settings.updates.current_version",
        model_.current_version);
    DrawTextLeft(g, cur, row_title_font, palette_.text_primary,
                 Gdiplus::RectF(x + pad, main_y, status_w,
                                ScaleDpiF(20.0f, dpi_)));
  }

  if (model_.is_checking_updates) {
    DrawTextLeft(
        g, Localized(model_.language, "settings.sidebar.checking_updates"),
        desc_font, palette_.text_secondary,
        Gdiplus::RectF(x + pad, u_div1_y + ScaleDpiF(32.0f, dpi_), status_w,
                       ScaleDpiF(18.0f, dpi_)));
  } else if (model_.did_confirm_up_to_date) {
    DrawTextLeft(
        g, Localized(model_.language, "settings.sidebar.up_to_date"),
        desc_font, palette_.accent_green,
        Gdiplus::RectF(x + pad, u_div1_y + ScaleDpiF(32.0f, dpi_), status_w,
                       ScaleDpiF(18.0f, dpi_)));
  }

  const float u_div2_y = u_div1_y + u_row2_h;
  g.DrawLine(&div_pen, x + pad, u_div2_y, x + w - pad, u_div2_y);

  // Update Row 3: Manual downloads + GitHub Releases link
  const float link_w = ScaleDpiF(122.0f, dpi_);
  const float text_w3 = w - pad * 2.0f - link_w - ScaleDpiF(12.0f, dpi_);
  DrawTextLeft(
      g, Localized(model_.language, "settings.updates.manual_download.title"),
      row_title_font, palette_.text_primary,
      Gdiplus::RectF(x + pad, u_div2_y + ScaleDpiF(10.0f, dpi_), text_w3,
                     ScaleDpiF(20.0f, dpi_)));
  DrawTextLeft(
      g,
      Localized(model_.language,
                "settings.updates.manual_download.description"),
      desc_font, palette_.text_secondary,
      Gdiplus::RectF(x + pad, u_div2_y + ScaleDpiF(31.0f, dpi_), text_w3,
                     ScaleDpiF(32.0f, dpi_)),
      /*wrap=*/true);

  Gdiplus::RectF link_rect(x + w - pad - link_w,
                           u_div2_y + (u_row3_h - ScaleDpiF(24.0f, dpi_)) * 0.5f,
                           link_w, ScaleDpiF(24.0f, dpi_));
  const std::wstring link_text =
      Localized(model_.language, "settings.updates.manual_download.link") +
      L" ↗";
  const bool link_hovered =
      (hovered_target_ == HitTargetKind::kLinkManualDownload);
  Gdiplus::Font link_font(
      font_name.c_str(), ScaleDpiF(12.5f, dpi_),
      link_hovered ? Gdiplus::FontStyleUnderline : Gdiplus::FontStyleRegular,
      Gdiplus::UnitPixel);
  Gdiplus::SolidBrush link_brush(palette_.accent_teal);
  Gdiplus::StringFormat right_fmt;
  right_fmt.SetAlignment(Gdiplus::StringAlignmentFar);
  right_fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  g.DrawString(link_text.c_str(), static_cast<INT>(link_text.size()),
               &link_font, link_rect, &right_fmt, &link_brush);
  hit_regions_.push_back({HitTargetKind::kLinkManualDownload, link_rect});
}

void SettingsWindow::PaintCorrectionPane(Gdiplus::Graphics& g, float x, float y,
                                         float w) {
  const std::wstring font_name = UiFontName();
  Gdiplus::Font hdr_font(font_name.c_str(), ScaleDpiF(12.5f, dpi_),
                         Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font row_title_font(font_name.c_str(), ScaleDpiF(13.5f, dpi_),
                               Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::Font desc_font(font_name.c_str(), ScaleDpiF(11.5f, dpi_),
                          Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

  const float card_r = ScaleDpiF(8.0f, dpi_);
  const float pad = ScaleDpiF(16.0f, dpi_);
  const float toggle_w = ScaleDpiF(40.0f, dpi_);
  const float toggle_h = ScaleDpiF(20.0f, dpi_);
  const float text_w = w - pad * 2.0f - toggle_w - ScaleDpiF(16.0f, dpi_);
  Gdiplus::Pen div_pen(palette_.divider, 1.0f);

  auto draw_toggle_row = [&](float row_y, float row_h, std::string_view title_k,
                             std::string_view desc_k, bool value,
                             HitTargetKind target) {
    DrawTextLeft(g, Localized(model_.language, title_k), row_title_font,
                 palette_.text_primary,
                 Gdiplus::RectF(x + pad, row_y + ScaleDpiF(10.0f, dpi_), text_w,
                                ScaleDpiF(20.0f, dpi_)));
    DrawTextLeft(g, Localized(model_.language, desc_k), desc_font,
                 palette_.text_secondary,
                 Gdiplus::RectF(x + pad, row_y + ScaleDpiF(31.0f, dpi_), text_w,
                                row_h - ScaleDpiF(36.0f, dpi_)),
                 /*wrap=*/true);
    Gdiplus::RectF t_rect(x + w - pad - toggle_w,
                          row_y + (row_h - toggle_h) * 0.5f, toggle_w,
                          toggle_h);
    DrawToggleSwitch(g, t_rect, value, hovered_target_ == target, palette_);
    hit_regions_.push_back({target, t_rect});
  };

  // Section 1: Tone Placement
  DrawTextLeft(g, Localized(model_.language, "settings.orthography.header"),
               hdr_font, palette_.text_secondary,
               Gdiplus::RectF(x + ScaleDpiF(4.0f, dpi_), y, w,
                              ScaleDpiF(20.0f, dpi_)));
  y += ScaleDpiF(22.0f, dpi_);

  const float h_tone = ScaleDpiF(68.0f, dpi_);
  Gdiplus::RectF tone_card(x, y, w, h_tone);
  FillRoundedRect(g, tone_card, card_r, palette_.card_bg);
  StrokeRoundedRect(g, tone_card, card_r, palette_.card_border, 1.0f);
  draw_toggle_row(y, h_tone, "settings.orthography.modern_tone.title",
                  "settings.orthography.modern_tone.description",
                  model_.modern_tone, HitTargetKind::kToggleModernTone);

  // Section 2: Non-Vietnamese Words
  y = tone_card.GetBottom() + ScaleDpiF(14.0f, dpi_);
  DrawTextLeft(g, Localized(model_.language, "settings.english.header"),
               hdr_font, palette_.text_secondary,
               Gdiplus::RectF(x + ScaleDpiF(4.0f, dpi_), y, w,
                              ScaleDpiF(20.0f, dpi_)));
  y += ScaleDpiF(22.0f, dpi_);

  const float h_eng = ScaleDpiF(74.0f, dpi_);
  Gdiplus::RectF eng_card(x, y, w, h_eng);
  FillRoundedRect(g, eng_card, card_r, palette_.card_bg);
  StrokeRoundedRect(g, eng_card, card_r, palette_.card_border, 1.0f);
  draw_toggle_row(y, h_eng, "settings.english.restore_invalid.title",
                  "settings.english.restore_invalid.description",
                  model_.restore_invalid_words,
                  HitTargetKind::kToggleRestoreInvalid);

  // Section 3: Spelling & Diphthongs
  y = eng_card.GetBottom() + ScaleDpiF(14.0f, dpi_);
  DrawTextLeft(g, Localized(model_.language, "settings.correction.header"),
               hdr_font, palette_.text_secondary,
               Gdiplus::RectF(x + ScaleDpiF(4.0f, dpi_), y, w,
                              ScaleDpiF(20.0f, dpi_)));
  y += ScaleDpiF(22.0f, dpi_);

  const float h_row = ScaleDpiF(64.0f, dpi_);
  Gdiplus::RectF corr_card(x, y, w, h_row * 3.0f);
  FillRoundedRect(g, corr_card, card_r, palette_.card_bg);
  StrokeRoundedRect(g, corr_card, card_r, palette_.card_border, 1.0f);

  draw_toggle_row(y, h_row, "settings.correction.uo_horn.title",
                  "settings.correction.uo_horn.description", model_.uo_horn,
                  HitTargetKind::kToggleUoHorn);
  g.DrawLine(&div_pen, x + pad, y + h_row, x + w - pad, y + h_row);

  draw_toggle_row(y + h_row, h_row, "settings.correction.ng_to_ngh.title",
                  "settings.correction.ng_to_ngh.description",
                  model_.ng_to_ngh, HitTargetKind::kToggleNgToNgh);
  g.DrawLine(&div_pen, x + pad, y + h_row * 2.0f, x + w - pad,
             y + h_row * 2.0f);

  draw_toggle_row(y + h_row * 2.0f, h_row,
                  "settings.correction.ngh_to_ng.title",
                  "settings.correction.ngh_to_ng.description",
                  model_.ngh_to_ng, HitTargetKind::kToggleNghToNg);
}

void SettingsWindow::PaintAdvancedPane(Gdiplus::Graphics& g, float x, float y,
                                       float w) {
  const std::wstring font_name = UiFontName();
  Gdiplus::Font hdr_font(font_name.c_str(), ScaleDpiF(12.5f, dpi_),
                         Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font row_title_font(font_name.c_str(), ScaleDpiF(13.5f, dpi_),
                               Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::Font desc_font(font_name.c_str(), ScaleDpiF(11.5f, dpi_),
                          Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

  const float card_r = ScaleDpiF(8.0f, dpi_);
  const float pad = ScaleDpiF(16.0f, dpi_);

  DrawTextLeft(g, Localized(model_.language, "settings.advanced.header"),
               hdr_font, palette_.text_secondary,
               Gdiplus::RectF(x + ScaleDpiF(4.0f, dpi_), y, w,
                              ScaleDpiF(20.0f, dpi_)));
  y += ScaleDpiF(24.0f, dpi_);

  const float card_h = ScaleDpiF(86.0f, dpi_);
  Gdiplus::RectF card(x, y, w, card_h);
  FillRoundedRect(g, card, card_r, palette_.card_bg);
  StrokeRoundedRect(g, card, card_r, palette_.card_border, 1.0f);

  const float text_w = w - pad * 2.0f - ScaleDpiF(96.0f, dpi_);
  DrawTextLeft(
      g, Localized(model_.language, "settings.advanced.buffer_size.title"),
      row_title_font, palette_.text_primary,
      Gdiplus::RectF(x + pad, y + ScaleDpiF(12.0f, dpi_), text_w,
                     ScaleDpiF(20.0f, dpi_)));
  DrawTextLeft(
      g,
      Localized(model_.language, "settings.advanced.buffer_size.description"),
      desc_font, palette_.text_secondary,
      Gdiplus::RectF(x + pad, y + ScaleDpiF(34.0f, dpi_), text_w,
                     ScaleDpiF(46.0f, dpi_)),
      /*wrap=*/true);
}

SettingsWindow::HitTargetKind SettingsWindow::HitTest(int x, int y) const {
  const float fx = static_cast<float>(x);
  const float fy = static_cast<float>(y);
  for (const auto& region : hit_regions_) {
    if (region.rect.Contains(fx, fy)) {
      return region.kind;
    }
  }
  return HitTargetKind::kNone;
}

void SettingsWindow::HandleClick(HitTargetKind target) {
  switch (target) {
    case HitTargetKind::kNavGeneral:
      if (section_ != SettingsSection::kGeneral) {
        section_ = SettingsSection::kGeneral;
        LayoutChildControls();
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      break;
    case HitTargetKind::kNavCorrection:
      if (section_ != SettingsSection::kCorrection) {
        section_ = SettingsSection::kCorrection;
        LayoutChildControls();
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      break;
    case HitTargetKind::kNavAdvanced:
      if (section_ != SettingsSection::kAdvanced) {
        section_ = SettingsSection::kAdvanced;
        LayoutChildControls();
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      break;
    case HitTargetKind::kToggleLaunchAtLogin:
      if (callbacks_.on_set_launch_at_login) {
        callbacks_.on_set_launch_at_login(!model_.launch_at_login);
      }
      break;
    case HitTargetKind::kToggleAutoCheck:
      if (callbacks_.on_set_auto_check_updates) {
        callbacks_.on_set_auto_check_updates(!model_.auto_check_updates);
      }
      break;
    case HitTargetKind::kButtonDownloadUpdate:
      if (callbacks_.on_download_update) callbacks_.on_download_update();
      break;
    case HitTargetKind::kButtonCheckUpdates:
      if (!model_.is_checking_updates && callbacks_.on_check_updates_now) {
        callbacks_.on_check_updates_now();
      }
      break;
    case HitTargetKind::kLinkManualDownload:
      if (callbacks_.on_open_releases_page) callbacks_.on_open_releases_page();
      break;
    case HitTargetKind::kToggleModernTone:
      if (callbacks_.on_set_modern_tone) {
        callbacks_.on_set_modern_tone(!model_.modern_tone);
      }
      break;
    case HitTargetKind::kToggleRestoreInvalid:
      if (callbacks_.on_set_restore_invalid) {
        callbacks_.on_set_restore_invalid(!model_.restore_invalid_words);
      }
      break;
    case HitTargetKind::kToggleUoHorn:
      if (callbacks_.on_set_uo_horn) {
        callbacks_.on_set_uo_horn(!model_.uo_horn);
      }
      break;
    case HitTargetKind::kToggleNgToNgh:
      if (callbacks_.on_set_ng_to_ngh) {
        callbacks_.on_set_ng_to_ngh(!model_.ng_to_ngh);
      }
      break;
    case HitTargetKind::kToggleNghToNg:
      if (callbacks_.on_set_ngh_to_ng) {
        callbacks_.on_set_ngh_to_ng(!model_.ngh_to_ng);
      }
      break;
    case HitTargetKind::kNone:
      break;
  }
}

LRESULT CALLBACK SettingsWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wparam,
                                            LPARAM lparam) {
  SettingsWindow* self = nullptr;
  if (msg == WM_NCCREATE) {
    const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<SettingsWindow*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = hwnd;
  } else {
    self = reinterpret_cast<SettingsWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (self != nullptr) {
    return self->HandleMessage(msg, wparam, lparam);
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT SettingsWindow::HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT:
      Paint();
      return 0;

    case WM_SIZE:
      LayoutChildControls();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;

    case WM_GETMINMAXINFO: {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
      mmi->ptMinTrackSize.x = ScaleDpi(kMinWidthDip, dpi_);
      mmi->ptMinTrackSize.y = ScaleDpi(kMinHeightDip, dpi_);
      return 0;
    }

    case WM_DPICHANGED: {
      dpi_ = HIWORD(wparam);
      UpdateFontsAndBrushes();
      const auto* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left,
                   suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      LayoutChildControls();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }

    case WM_MOUSEMOVE: {
      if (!tracking_mouse_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        tracking_mouse_ = true;
      }
      const HitTargetKind next =
          HitTest(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (next != hovered_target_) {
        hovered_target_ = next;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;
    }

    case WM_MOUSELEAVE:
      tracking_mouse_ = false;
      if (hovered_target_ != HitTargetKind::kNone) {
        hovered_target_ = HitTargetKind::kNone;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;

    case WM_SETCURSOR:
      if (LOWORD(lparam) == HTCLIENT &&
          hovered_target_ != HitTargetKind::kNone) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
      break;

    case WM_LBUTTONUP: {
      const HitTargetKind clicked =
          HitTest(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (clicked != HitTargetKind::kNone) {
        HandleClick(clicked);
      }
      return 0;
    }

    case WM_KEYDOWN:
      if (wparam == VK_ESCAPE) {
        Close();
        return 0;
      }
      break;

    case WM_COMMAND: {
      if (updating_controls_) return 0;
      const int id = LOWORD(wparam);
      const int code = HIWORD(wparam);
      if (id == kIdComboLanguage && code == CBN_SELCHANGE) {
        const LRESULT idx = SendMessageW(combo_language_, CB_GETCURSEL, 0, 0);
        if (idx >= 0 && idx <= 2 && callbacks_.on_select_language) {
          callbacks_.on_select_language(static_cast<AppLanguage>(idx));
        }
        return 0;
      }
      if (id == kIdComboStyle && code == CBN_SELCHANGE) {
        const LRESULT idx = SendMessageW(combo_style_, CB_GETCURSEL, 0, 0);
        if (idx >= 0 && idx <= 3 && callbacks_.on_select_style) {
          callbacks_.on_select_style(static_cast<InputStyle>(idx));
        }
        return 0;
      }
      if (id == kIdEditBufferSize &&
          (code == EN_KILLFOCUS || code == EN_CHANGE)) {
        wchar_t buf[32] = {};
        GetWindowTextW(edit_buffer_size_, buf, 32);
        if (buf[0] != L'\0') {
          const int32_t val = static_cast<int32_t>(_wtoi(buf));
          const int32_t clamped = std::clamp(val, model_.min_buffer_size,
                                             model_.max_allowed_buffer_size);
          if (code == EN_KILLFOCUS && clamped != val) {
            updating_controls_ = true;
            SetWindowTextW(edit_buffer_size_,
                           std::to_wstring(clamped).c_str());
            updating_controls_ = false;
          }
          if (clamped != model_.max_buffer_size &&
              callbacks_.on_set_max_buffer_size) {
            callbacks_.on_set_max_buffer_size(clamped);
          }
        }
        return 0;
      }
      break;
    }

    case WM_NOTIFY: {
      const auto* hdr = reinterpret_cast<const NMHDR*>(lparam);
      if (hdr != nullptr && hdr->idFrom == kIdUpDownBufferSize &&
          hdr->code == UDN_DELTAPOS) {
        const auto* ud = reinterpret_cast<const NMUPDOWN*>(lparam);
        // Step by 8 matching macOS Stepper(..., step: 8).
        const int32_t step = ud->iDelta > 0 ? 8 : -8;
        const int32_t next = std::clamp(model_.max_buffer_size + step,
                                        model_.min_buffer_size,
                                        model_.max_allowed_buffer_size);
        if (callbacks_.on_set_max_buffer_size) {
          callbacks_.on_set_max_buffer_size(next);
        }
        return 1;  // Suppress default +/-1 increment.
      }
      break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
      HDC dc = reinterpret_cast<HDC>(wparam);
      SetTextColor(dc, palette_.win32_text_primary);
      SetBkColor(dc, palette_.win32_card_bg);
      return reinterpret_cast<LRESULT>(brush_card_);
    }

    case WM_CLOSE:
      DestroyWindow(hwnd_);
      return 0;

    case WM_NCDESTROY:
      hwnd_ = nullptr;
      combo_language_ = nullptr;
      combo_style_ = nullptr;
      edit_buffer_size_ = nullptr;
      updown_buffer_size_ = nullptr;
      hovered_target_ = HitTargetKind::kNone;
      tracking_mouse_ = false;
      return 0;

    default:
      break;
  }

  return DefWindowProcW(hwnd_, msg, wparam, lparam);
}

}  // namespace okeytre::win
