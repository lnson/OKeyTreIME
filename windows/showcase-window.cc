// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/showcase-window.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <string>

#include "windows/resources/resource.h"

namespace okeytre::win {

namespace {

constexpr wchar_t kShowcaseClassName[] = L"OKeyTreShowcaseWindowClass";

enum ControlId : int {
  kIdComboLanguage = 3001,
  kIdEditPractice = 3002,
};

constexpr int kWindowWidthDip = 640;
constexpr int kWindowHeightDip = 592;

std::wstring UiFontName() {
  static const std::wstring kName = []() -> std::wstring {
    Gdiplus::FontFamily ff(L"Segoe UI Variable Text");
    return ff.IsAvailable() ? L"Segoe UI Variable Text" : L"Segoe UI";
  }();
  return kName;
}

std::wstring MonoFontName() {
  static const std::wstring kName = []() -> std::wstring {
    Gdiplus::FontFamily ff(L"Cascadia Mono");
    return ff.IsAvailable() ? L"Cascadia Mono" : L"Consolas";
  }();
  return kName;
}

float MeasureTextWidth(Gdiplus::Graphics& g, std::wstring_view text,
                       const Gdiplus::Font& font) {
  Gdiplus::RectF bounds;
  Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
  fmt.SetFormatFlags(Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
  g.MeasureString(text.data(), static_cast<INT>(text.size()), &font,
                  Gdiplus::PointF(0.0f, 0.0f), &fmt, &bounds);
  return bounds.Width;
}

void DrawCenteredText(Gdiplus::Graphics& g, std::wstring_view text,
                      const Gdiplus::Font& font, const Gdiplus::Color& color,
                      const Gdiplus::RectF& rect) {
  Gdiplus::SolidBrush brush(color);
  Gdiplus::StringFormat fmt;
  fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
  g.DrawString(text.data(), static_cast<INT>(text.size()), &font, rect, &fmt,
               &brush);
}

enum class TokenKind {
  kTypedPill,
  kKeycap,
  kResultPill,
  kOperator,
};

struct DemoToken {
  std::wstring_view text;
  TokenKind kind;
};

void DrawDemoTokenRow(Gdiplus::Graphics& g, float x, float y, float height,
                      const std::vector<DemoToken>& tokens,
                      const ThemePalette& palette, UINT dpi) {
  const std::wstring mono = MonoFontName();
  const std::wstring ui = UiFontName();
  Gdiplus::Font mono_font(mono.c_str(), ScaleDpiF(11.5f, dpi),
                          Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::Font mono_bold(mono.c_str(), ScaleDpiF(11.5f, dpi),
                          Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font key_font(ui.c_str(), ScaleDpiF(10.5f, dpi),
                         Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

  const float radius = ScaleDpiF(5.0f, dpi);
  const float gap = ScaleDpiF(4.5f, dpi);
  const float pad_x = ScaleDpiF(6.0f, dpi);

  float cur_x = x;
  for (const auto& tok : tokens) {
    switch (tok.kind) {
      case TokenKind::kOperator: {
        const float w = MeasureTextWidth(g, tok.text, mono_font) +
                        ScaleDpiF(3.0f, dpi);
        DrawCenteredText(g, tok.text, mono_font, palette.text_secondary,
                         Gdiplus::RectF(cur_x, y, w, height));
        cur_x += w + gap;
        break;
      }
      case TokenKind::kTypedPill: {
        const float w = MeasureTextWidth(g, tok.text, mono_font) + pad_x * 2.0f;
        Gdiplus::RectF r(cur_x, y, w, height);
        const Gdiplus::Color fill = palette.dark
                                        ? Gdiplus::Color(22, 255, 255, 255)
                                        : Gdiplus::Color(14, 0, 0, 0);
        const Gdiplus::Color dash_col = palette.dark
                                            ? Gdiplus::Color(70, 255, 255, 255)
                                            : Gdiplus::Color(60, 0, 0, 0);
        FillRoundedRect(g, r, radius, fill);
        Gdiplus::GraphicsPath path;
        AddRoundedRectPath(path, r, radius);
        Gdiplus::Pen dash_pen(dash_col, 1.0f);
        dash_pen.SetDashStyle(Gdiplus::DashStyleDash);
        g.DrawPath(&dash_pen, &path);
        DrawCenteredText(g, tok.text, mono_font, palette.text_primary, r);
        cur_x += w + gap;
        break;
      }
      case TokenKind::kKeycap: {
        const float w = MeasureTextWidth(g, tok.text, key_font) + pad_x * 2.0f;
        Gdiplus::RectF shadow(cur_x, y + ScaleDpiF(1.2f, dpi), w, height);
        FillRoundedRect(g, shadow, radius, Gdiplus::Color(35, 0, 0, 0));
        Gdiplus::RectF r(cur_x, y, w, height);
        FillRoundedRect(g, r, radius, palette.window_bg);
        StrokeRoundedRect(g, r, radius, palette.control_border, 1.0f);
        DrawCenteredText(g, tok.text, key_font, palette.text_primary, r);
        cur_x += w + gap;
        break;
      }
      case TokenKind::kResultPill: {
        const float w =
            MeasureTextWidth(g, tok.text, mono_bold) + pad_x * 2.2f;
        Gdiplus::RectF r(cur_x, y, w, height);
        FillRoundedRect(g, r, radius, palette.accent_teal_soft);
        DrawCenteredText(g, tok.text, mono_bold, palette.accent_teal, r);
        cur_x += w + gap;
        break;
      }
    }
  }
}

}  // namespace

ShowcaseWindow::ShowcaseWindow(ShowcaseCallbacks callbacks)
    : callbacks_(std::move(callbacks)),
      palette_(GetThemePalette(IsAppDarkMode())) {}

ShowcaseWindow::~ShowcaseWindow() {
  Close();
  if (ui_font_ != nullptr) DeleteObject(ui_font_);
  if (practice_font_ != nullptr) DeleteObject(practice_font_);
  if (brush_card_ != nullptr) DeleteObject(brush_card_);
}

void ShowcaseWindow::EnsureWindowClass() {
  static bool registered = false;
  if (registered) return;

  HINSTANCE hinst = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &ShowcaseWindow::WindowProc;
  wc.hInstance = hinst;
  wc.hIcon = LoadIconW(hinst, MAKEINTRESOURCEW(IDI_APP_ICON));
  wc.hIconSm = wc.hIcon;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kShowcaseClassName;
  RegisterClassExW(&wc);
  registered = true;
}

std::wstring ShowcaseWindow::ResolveWindowTitle() const {
  if (model_.mode == ShowcaseMode::kFeatureUpdate) {
    return LocalizedFormat(model_.language, L10nKey::kShowcaseWhatsNewTitle,
                           model_.milestone_version);
  }
  return std::wstring(Localized(model_.language, L10nKey::kShowcaseWindowTitle));
}

void ShowcaseWindow::Show(const ShowcaseViewModel& model) {
  model_ = model;
  palette_ = GetThemePalette(IsAppDarkMode());

  if (hwnd_ == nullptr) {
    EnsureWindowClass();
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    dpi_ = GetDpiForSystem();

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                        WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT wr{0, 0, ScaleDpi(kWindowWidthDip, dpi_),
            ScaleDpi(kWindowHeightDip, dpi_)};
    AdjustWindowRectExForDpi(&wr, style, FALSE, 0, dpi_);

    const int win_w = wr.right - wr.left;
    const int win_h = wr.bottom - wr.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - win_w) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - win_h) / 2;

    const std::wstring title = ResolveWindowTitle();
    hwnd_ = CreateWindowExW(0, kShowcaseClassName, title.c_str(), style, x, y,
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

void ShowcaseWindow::Update(const ShowcaseViewModel& model) {
  model_ = model;
  if (hwnd_ == nullptr) {
    return;
  }
  SetWindowTextW(hwnd_, ResolveWindowTitle().c_str());
  SyncControlsFromModel();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void ShowcaseWindow::RefreshTheme() {
  palette_ = GetThemePalette(IsAppDarkMode());
  if (hwnd_ == nullptr) {
    return;
  }
  ApplyWindowThemeAttributes(hwnd_, palette_.dark);
  UpdateFontsAndBrushes();
  InvalidateRect(hwnd_, nullptr, TRUE);
}

void ShowcaseWindow::Close() {
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

bool ShowcaseWindow::IsVisible() const {
  return hwnd_ != nullptr && IsWindowVisible(hwnd_);
}

void ShowcaseWindow::UpdateFontsAndBrushes() {
  if (ui_font_ != nullptr) DeleteObject(ui_font_);
  if (practice_font_ != nullptr) DeleteObject(practice_font_);
  if (brush_card_ != nullptr) DeleteObject(brush_card_);

  ui_font_ = CreateUiFont(9, FW_NORMAL, dpi_);
  practice_font_ = CreateUiFont(11, FW_NORMAL, dpi_);
  brush_card_ = CreateSolidBrush(palette_.win32_card_bg);

  if (combo_language_ != nullptr) {
    SendMessageW(combo_language_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(ui_font_), TRUE);
  }
  if (edit_practice_ != nullptr) {
    SendMessageW(edit_practice_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(practice_font_), TRUE);
  }
}

void ShowcaseWindow::CreateChildControls() {
  HINSTANCE hinst = GetModuleHandleW(nullptr);

  combo_language_ = CreateWindowExW(
      0, WC_COMBOBOXW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 128, 160,
      hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdComboLanguage)),
      hinst, nullptr);

  edit_practice_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, WC_EDITW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 560, 34, hwnd_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEditPractice)), hinst,
      nullptr);

  UpdateFontsAndBrushes();
}

void ShowcaseWindow::SyncControlsFromModel() {
  if (hwnd_ == nullptr) return;
  updating_controls_ = true;

  if (combo_language_ != nullptr) {
    SendMessageW(combo_language_, CB_RESETCONTENT, 0, 0);
    const std::wstring_view s_sys =
        LanguageDisplayName(AppLanguage::kSystem, model_.language);
    const std::wstring_view s_vi =
        LanguageDisplayName(AppLanguage::kVietnamese, model_.language);
    const std::wstring_view s_en =
        LanguageDisplayName(AppLanguage::kEnglish, model_.language);
    SendMessageW(combo_language_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(s_sys.data()));
    SendMessageW(combo_language_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(s_vi.data()));
    SendMessageW(combo_language_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(s_en.data()));
    SendMessageW(combo_language_, CB_SETCURSEL,
                 static_cast<WPARAM>(model_.language), 0);
  }

  if (edit_practice_ != nullptr) {
    const std::wstring_view placeholder =
        Localized(model_.language, L10nKey::kShowcasePracticePlaceholder);
    SendMessageW(edit_practice_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(placeholder.data()));
  }

  updating_controls_ = false;
}

void ShowcaseWindow::LayoutChildControls() {
  if (hwnd_ == nullptr) return;

  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const int width = rc.right - rc.left;
  const int pad_x = ScaleDpi(26, dpi_);

  if (combo_language_ != nullptr) {
    const int cb_w = ScaleDpi(132, dpi_);
    MoveWindow(combo_language_, width - pad_x - cb_w, ScaleDpi(12, dpi_), cb_w,
               ScaleDpi(160, dpi_), TRUE);
  }

  if (edit_practice_ != nullptr) {
    MoveWindow(edit_practice_, pad_x, ScaleDpi(476, dpi_), width - pad_x * 2,
               ScaleDpi(34, dpi_), TRUE);
  }
}

void ShowcaseWindow::Paint() {
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

    Gdiplus::SolidBrush bg_brush(palette_.window_bg);
    g.FillRectangle(&bg_brush, 0, 0, width, height);

    hit_regions_.clear();

    const float pad_x = ScaleDpiF(26.0f, dpi_);
    const float content_w = static_cast<float>(width) - pad_x * 2.0f;

    // Globe icon to the left of the language combobox.
    const float globe_sz = ScaleDpiF(14.0f, dpi_);
    const float globe_x = static_cast<float>(width) - pad_x -
                          ScaleDpiF(132.0f, dpi_) - globe_sz -
                          ScaleDpiF(7.0f, dpi_);
    const float globe_y = ScaleDpiF(17.0f, dpi_);
    Gdiplus::Pen globe_pen(palette_.text_secondary, ScaleDpiF(1.2f, dpi_));
    g.DrawEllipse(&globe_pen, globe_x, globe_y, globe_sz, globe_sz);
    g.DrawEllipse(&globe_pen, globe_x + globe_sz * 0.26f, globe_y,
                  globe_sz * 0.48f, globe_sz);
    g.DrawLine(&globe_pen, globe_x, globe_y + globe_sz * 0.5f,
               globe_x + globe_sz, globe_y + globe_sz * 0.5f);

    // Hero Header
    const float logo_sz = ScaleDpiF(62.0f, dpi_);
    Gdiplus::RectF logo_rect((static_cast<float>(width) - logo_sz) * 0.5f,
                             ScaleDpiF(28.0f, dpi_), logo_sz, logo_sz);
    DrawAppLogo(g, logo_rect, ScaleDpiF(14.0f, dpi_));

    const std::wstring font_name = UiFontName();
    Gdiplus::Font hero_font(font_name.c_str(), ScaleDpiF(21.0f, dpi_),
                            Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font sub_font(font_name.c_str(), ScaleDpiF(12.5f, dpi_),
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    const std::wstring header_title =
        (model_.mode == ShowcaseMode::kFeatureUpdate)
            ? LocalizedFormat(model_.language, L10nKey::kShowcaseWhatsNewTitle,
                              model_.milestone_version)
            : std::wstring(Localized(model_.language, L10nKey::kShowcaseTitle));
    DrawCenteredText(g, header_title, hero_font, palette_.text_primary,
                     Gdiplus::RectF(pad_x, ScaleDpiF(98.0f, dpi_), content_w,
                                    ScaleDpiF(28.0f, dpi_)));

    const std::wstring_view subtitle =
        Localized(model_.language, L10nKey::kShowcaseSubtitle);
    Gdiplus::SolidBrush sub_brush(palette_.text_secondary);
    Gdiplus::StringFormat wrap_center;
    wrap_center.SetAlignment(Gdiplus::StringAlignmentCenter);
    wrap_center.SetLineAlignment(Gdiplus::StringAlignmentNear);
    g.DrawString(subtitle.data(), static_cast<INT>(subtitle.size()), &sub_font,
                 Gdiplus::RectF(pad_x + ScaleDpiF(18.0f, dpi_),
                                ScaleDpiF(130.0f, dpi_),
                                content_w - ScaleDpiF(36.0f, dpi_),
                                ScaleDpiF(36.0f, dpi_)),
                 &wrap_center, &sub_brush);

    // Status Banner
    PaintStatusBanner(g, pad_x, ScaleDpiF(176.0f, dpi_), content_w);

    // 2x2 Feature Cards Grid
    PaintFeatureGrid(g, pad_x, ScaleDpiF(232.0f, dpi_), content_w);

    // Practice Sandbox Header
    PaintPracticeHeader(g, pad_x, ScaleDpiF(450.0f, dpi_), content_w);

    // Footer Bar
    PaintFooter(g, pad_x, ScaleDpiF(528.0f, dpi_), content_w);
  }

  BitBlt(hdc, 0, 0, width, height, mem_dc, 0, 0, SRCCOPY);
  SelectObject(mem_dc, old_bmp);
  DeleteObject(mem_bmp);
  DeleteDC(mem_dc);
  EndPaint(hwnd_, &ps);
}

void ShowcaseWindow::PaintStatusBanner(Gdiplus::Graphics& g, float x, float y,
                                       float w) {
  const std::wstring font_name = UiFontName();
  Gdiplus::Font bold_font(font_name.c_str(), ScaleDpiF(12.5f, dpi_),
                          Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font sub_font(font_name.c_str(), ScaleDpiF(11.0f, dpi_),
                         Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

  const float h = ScaleDpiF(42.0f, dpi_);
  const float r = ScaleDpiF(10.0f, dpi_);
  Gdiplus::RectF box(x, y, w, h);

  const bool show_kb_warning =
      (model_.style != InputStyle::kOff && !model_.is_english_keyboard);

  if (show_kb_warning) {
    FillRoundedRect(g, box, r, palette_.accent_orange_soft);
    StrokeRoundedRect(g, box, r, palette_.accent_orange_border, 1.0f);

    const float btn_w = ScaleDpiF(118.0f, dpi_);
    const float btn_h = ScaleDpiF(26.0f, dpi_);
    Gdiplus::RectF btn(box.GetRight() - ScaleDpiF(10.0f, dpi_) - btn_w,
                       y + (h - btn_h) * 0.5f, btn_w, btn_h);

    const bool hovered = (hovered_target_ == HitTargetKind::kButtonSwitchToUs);
    FillRoundedRect(g, btn, ScaleDpiF(6.0f, dpi_),
                    hovered ? Gdiplus::Color(255, 194, 100, 4)
                            : palette_.accent_orange);
    Gdiplus::Font btn_font(font_name.c_str(), ScaleDpiF(11.5f, dpi_),
                           Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    DrawCenteredText(g, Localized(model_.language, L10nKey::kActionSwitchToUs),
                     btn_font, palette_.text_on_accent, btn);
    hit_regions_.push_back({HitTargetKind::kButtonSwitchToUs, btn});

    const float text_x = x + ScaleDpiF(12.0f, dpi_);
    const float text_w = btn.X - text_x - ScaleDpiF(8.0f, dpi_);
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

    const std::wstring title = LocalizedFormat(
        model_.language, L10nKey::kMenuInputSourceWarningTitle,
        model_.keyboard_name);
    Gdiplus::SolidBrush primary_brush(palette_.text_primary);
    g.DrawString(title.c_str(), static_cast<INT>(title.size()), &bold_font,
                 Gdiplus::RectF(text_x, y + ScaleDpiF(5.0f, dpi_), text_w,
                                ScaleDpiF(17.0f, dpi_)),
                 &fmt, &primary_brush);

    const std::wstring_view desc =
        Localized(model_.language, L10nKey::kMenuInputSourceWarningSubtitle);
    Gdiplus::SolidBrush sec_brush(palette_.text_secondary);
    g.DrawString(desc.data(), static_cast<INT>(desc.size()), &sub_font,
                 Gdiplus::RectF(text_x, y + ScaleDpiF(22.0f, dpi_), text_w,
                                ScaleDpiF(15.0f, dpi_)),
                 &fmt, &sec_brush);
  } else {
    FillRoundedRect(g, box, r, palette_.accent_green_soft);
    StrokeRoundedRect(g, box, r, palette_.accent_green_border, 1.0f);

    // Green checkmark circle
    const float icon_sz = ScaleDpiF(16.0f, dpi_);
    const float ix = x + ScaleDpiF(12.0f, dpi_);
    const float iy = y + (h - icon_sz) * 0.5f;
    Gdiplus::SolidBrush green_brush(palette_.accent_green);
    g.FillEllipse(&green_brush, ix, iy, icon_sz, icon_sz);

    Gdiplus::Pen check_pen(Gdiplus::Color(255, 255, 255, 255),
                           ScaleDpiF(1.8f, dpi_));
    check_pen.SetStartCap(Gdiplus::LineCapRound);
    check_pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&check_pen, ix + icon_sz * 0.26f, iy + icon_sz * 0.52f,
               ix + icon_sz * 0.44f, iy + icon_sz * 0.70f);
    g.DrawLine(&check_pen, ix + icon_sz * 0.44f, iy + icon_sz * 0.70f,
               ix + icon_sz * 0.75f, iy + icon_sz * 0.34f);

    const std::wstring_view ready =
        Localized(model_.language, L10nKey::kShowcaseStatusReady);
    Gdiplus::SolidBrush primary_brush(palette_.text_primary);
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    g.DrawString(ready.data(), static_cast<INT>(ready.size()), &bold_font,
                 Gdiplus::RectF(ix + icon_sz + ScaleDpiF(10.0f, dpi_), y,
                                w - icon_sz - ScaleDpiF(32.0f, dpi_), h),
                 &fmt, &primary_brush);
  }
}

void ShowcaseWindow::PaintFeatureGrid(Gdiplus::Graphics& g, float x, float y,
                                      float w) {
  const std::wstring font_name = UiFontName();
  Gdiplus::Font card_title_font(font_name.c_str(), ScaleDpiF(12.0f, dpi_),
                                Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font badge_font(font_name.c_str(), ScaleDpiF(9.0f, dpi_),
                           Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font desc_font(font_name.c_str(), ScaleDpiF(11.0f, dpi_),
                          Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

  const float gap = ScaleDpiF(12.0f, dpi_);
  const float card_w = (w - gap) * 0.5f;
  const float card_h = ScaleDpiF(96.0f, dpi_);
  const float card_r = ScaleDpiF(11.0f, dpi_);
  const float pad = ScaleDpiF(12.0f, dpi_);

  const bool is_update = (model_.mode == ShowcaseMode::kFeatureUpdate);
  const std::wstring_view new_badge =
      is_update ? Localized(model_.language, L10nKey::kShowcaseBadgeNew) : L"";

  struct CardDef {
    std::vector<DemoToken> tokens;
    L10nKey title_key;
    L10nKey desc_key;
    bool show_new_badge;
  };

  const CardDef cards[4] = {
      {{
           {L"úer", TokenKind::kTypedPill},
           {L"+", TokenKind::kOperator},
           {L"Space", TokenKind::kKeycap},
           {L"→", TokenKind::kOperator},
           {L"user␣", TokenKind::kResultPill},
       },
       L10nKey::kShowcaseCardAutoRestoreTitle,
       L10nKey::kShowcaseCardAutoRestoreDesc,
       is_update},
      {{
           {L"rồ␣", TokenKind::kTypedPill},
           {L"+", TokenKind::kOperator},
           {L"⇧ L", TokenKind::kKeycap},
           {L"⇧ R", TokenKind::kKeycap},
           {L"→", TokenKind::kOperator},
           {L"roof␣", TokenKind::kResultPill},
       },
       L10nKey::kShowcaseCardDualShiftTitle,
       L10nKey::kShowcaseCardDualShiftDesc,
       is_update},
      {{
           {L"tieng", TokenKind::kTypedPill},
           {L"+", TokenKind::kOperator},
           {L"e", TokenKind::kKeycap},
           {L"s", TokenKind::kKeycap},
           {L"→", TokenKind::kOperator},
           {L"tiếng", TokenKind::kResultPill},
       },
       L10nKey::kShowcaseCardDiacriticsTitle,
       L10nKey::kShowcaseCardDiacriticsDesc,
       false},
      {{
           {L"Ctrl L", TokenKind::kKeycap},
           {L"+", TokenKind::kOperator},
           {L"Ctrl R", TokenKind::kKeycap},
           {L"→", TokenKind::kOperator},
           {Localized(model_.language, L10nKey::kShowcaseCardToggleResult),
            TokenKind::kResultPill},
       },
       L10nKey::kShowcaseCardToggleTitle,
       L10nKey::kShowcaseCardToggleDesc,
       false},
  };

  Gdiplus::StringFormat left_fmt;
  left_fmt.SetAlignment(Gdiplus::StringAlignmentNear);
  left_fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);

  for (int i = 0; i < 4; ++i) {
    const int col = i % 2;
    const int row = i / 2;
    const float cx = x + static_cast<float>(col) * (card_w + gap);
    const float cy = y + static_cast<float>(row) * (card_h + gap);

    Gdiplus::RectF box(cx, cy, card_w, card_h);
    FillRoundedRect(g, box, card_r, palette_.card_bg);
    StrokeRoundedRect(g, box, card_r, palette_.card_border, 1.0f);

    const std::wstring_view title =
        Localized(model_.language, cards[i].title_key);
    Gdiplus::SolidBrush teal_brush(palette_.accent_teal);
    Gdiplus::StringFormat title_fmt;
    title_fmt.SetAlignment(Gdiplus::StringAlignmentNear);
    title_fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    title_fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    title_fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

    const float title_w =
        std::min(card_w - pad * 2.0f - (cards[i].show_new_badge
                                            ? ScaleDpiF(42.0f, dpi_)
                                            : 0.0f),
                 MeasureTextWidth(g, title, card_title_font) +
                     ScaleDpiF(4.0f, dpi_));
    g.DrawString(title.data(), static_cast<INT>(title.size()),
                 &card_title_font,
                 Gdiplus::RectF(cx + pad, cy + ScaleDpiF(9.0f, dpi_), title_w,
                                ScaleDpiF(16.0f, dpi_)),
                 &title_fmt, &teal_brush);

    if (cards[i].show_new_badge && !new_badge.empty()) {
      const float bw =
          MeasureTextWidth(g, new_badge, badge_font) + ScaleDpiF(10.0f, dpi_);
      const float bh = ScaleDpiF(14.0f, dpi_);
      Gdiplus::RectF b_rect(cx + pad + title_w + ScaleDpiF(5.0f, dpi_),
                            cy + ScaleDpiF(10.0f, dpi_), bw, bh);
      FillRoundedRect(g, b_rect, bh * 0.5f, palette_.accent_teal);
      DrawCenteredText(g, new_badge, badge_font, palette_.text_on_accent,
                       b_rect);
    }

    DrawDemoTokenRow(g, cx + pad, cy + ScaleDpiF(30.0f, dpi_),
                     ScaleDpiF(21.0f, dpi_), cards[i].tokens, palette_, dpi_);

    const std::wstring_view desc =
        Localized(model_.language, cards[i].desc_key);
    Gdiplus::SolidBrush sec_brush(palette_.text_secondary);
    g.DrawString(desc.data(), static_cast<INT>(desc.size()), &desc_font,
                 Gdiplus::RectF(cx + pad, cy + ScaleDpiF(56.0f, dpi_),
                                card_w - pad * 2.0f, ScaleDpiF(34.0f, dpi_)),
                 &left_fmt, &sec_brush);
  }
}

void ShowcaseWindow::PaintPracticeHeader(Gdiplus::Graphics& g, float x, float y,
                                         float w) {
  const std::wstring font_name = UiFontName();
  Gdiplus::Font label_font(font_name.c_str(), ScaleDpiF(11.5f, dpi_),
                           Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
  Gdiplus::Font pill_font(font_name.c_str(), ScaleDpiF(10.5f, dpi_),
                          Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

  const std::wstring_view label =
      Localized(model_.language, L10nKey::kShowcasePracticeLabel);
  Gdiplus::SolidBrush sec_brush(palette_.text_secondary);
  Gdiplus::StringFormat fmt;
  fmt.SetAlignment(Gdiplus::StringAlignmentNear);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  g.DrawString(label.data(), static_cast<INT>(label.size()), &label_font,
               Gdiplus::RectF(x, y, w * 0.7f, ScaleDpiF(20.0f, dpi_)), &fmt,
               &sec_brush);

  const std::wstring_view style_name =
      InputStyleDisplayName(model_.style, model_.language);
  const float pill_w =
      MeasureTextWidth(g, style_name, pill_font) + ScaleDpiF(16.0f, dpi_);
  const float pill_h = ScaleDpiF(18.0f, dpi_);
  Gdiplus::RectF pill(x + w - pill_w, y + ScaleDpiF(1.0f, dpi_), pill_w,
                      pill_h);
  const bool enabled = (model_.style != InputStyle::kOff);
  FillRoundedRect(g, pill, pill_h * 0.5f,
                  enabled ? palette_.accent_teal_soft : palette_.hover_bg);
  DrawCenteredText(g, style_name, pill_font,
                   enabled ? palette_.accent_teal : palette_.text_secondary,
                   pill);
}

void ShowcaseWindow::PaintFooter(Gdiplus::Graphics& g, float x, float y,
                                 float w) {
  const std::wstring font_name = UiFontName();
  Gdiplus::Font label_font(font_name.c_str(), ScaleDpiF(12.5f, dpi_),
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::Font seg_font(font_name.c_str(), ScaleDpiF(12.0f, dpi_),
                         Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

  const float h = ScaleDpiF(34.0f, dpi_);
  const std::wstring_view lbl =
      Localized(model_.language, L10nKey::kMenuInputStyle);
  const float lbl_w =
      MeasureTextWidth(g, lbl, label_font) + ScaleDpiF(10.0f, dpi_);

  Gdiplus::SolidBrush sec_brush(palette_.text_secondary);
  Gdiplus::StringFormat fmt;
  fmt.SetAlignment(Gdiplus::StringAlignmentNear);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  g.DrawString(lbl.data(), static_cast<INT>(lbl.size()), &label_font,
               Gdiplus::RectF(x, y, lbl_w, h), &fmt, &sec_brush);

  // Segmented control: Telex | VNI | VIQR
  const float seg_w = ScaleDpiF(58.0f, dpi_);
  const float seg_h = ScaleDpiF(30.0f, dpi_);
  const float seg_y = y + (h - seg_h) * 0.5f;
  const float seg_x = x + lbl_w + ScaleDpiF(6.0f, dpi_);
  Gdiplus::RectF seg_container(seg_x, seg_y, seg_w * 3.0f, seg_h);
  FillRoundedRect(g, seg_container, ScaleDpiF(7.0f, dpi_), palette_.card_bg);
  StrokeRoundedRect(g, seg_container, ScaleDpiF(7.0f, dpi_),
                    palette_.card_border, 1.0f);

  const InputStyle active_seg =
      (model_.style == InputStyle::kOff) ? InputStyle::kTelex : model_.style;
  struct SegDef {
    InputStyle style;
    HitTargetKind target;
    const wchar_t* name;
  };
  const SegDef segs[3] = {
      {InputStyle::kTelex, HitTargetKind::kSegmentTelex, L"Telex"},
      {InputStyle::kVNI, HitTargetKind::kSegmentVni, L"VNI"},
      {InputStyle::kVIQR, HitTargetKind::kSegmentViqr, L"VIQR"},
  };

  for (int i = 0; i < 3; ++i) {
    Gdiplus::RectF sr(seg_x + static_cast<float>(i) * seg_w, seg_y, seg_w,
                      seg_h);
    const bool selected = (active_seg == segs[i].style);
    const bool hovered = (hovered_target_ == segs[i].target);
    if (selected) {
      Gdiplus::RectF inner(sr.X + 2.0f, sr.Y + 2.0f, sr.Width - 4.0f,
                           sr.Height - 4.0f);
      FillRoundedRect(g, inner, ScaleDpiF(5.5f, dpi_), palette_.accent_teal);
    } else if (hovered) {
      Gdiplus::RectF inner(sr.X + 2.0f, sr.Y + 2.0f, sr.Width - 4.0f,
                           sr.Height - 4.0f);
      FillRoundedRect(g, inner, ScaleDpiF(5.5f, dpi_), palette_.hover_bg);
    }
    DrawCenteredText(
        g, segs[i].name, seg_font,
        selected ? palette_.text_on_accent : palette_.text_primary, sr);
    hit_regions_.push_back({segs[i].target, sr});
  }

  // Prominent "Get Started" button
  const float btn_w = ScaleDpiF(132.0f, dpi_);
  Gdiplus::RectF start_btn(x + w - btn_w, y, btn_w, h);
  DrawFluentButton(g, start_btn,
                   Localized(model_.language, L10nKey::kShowcaseActionGetStarted),
                   /*is_primary=*/true,
                   hovered_target_ == HitTargetKind::kButtonGetStarted,
                   /*is_disabled=*/false, palette_, dpi_);
  hit_regions_.push_back({HitTargetKind::kButtonGetStarted, start_btn});
}

ShowcaseWindow::HitTargetKind ShowcaseWindow::HitTest(int x, int y) const {
  const float fx = static_cast<float>(x);
  const float fy = static_cast<float>(y);
  for (const auto& region : hit_regions_) {
    if (region.rect.Contains(fx, fy)) {
      return region.kind;
    }
  }
  return HitTargetKind::kNone;
}

void ShowcaseWindow::HandleClick(HitTargetKind target) {
  switch (target) {
    case HitTargetKind::kButtonSwitchToUs:
      if (callbacks_.on_switch_to_us) callbacks_.on_switch_to_us();
      break;
    case HitTargetKind::kSegmentTelex:
      if (callbacks_.on_select_style) {
        callbacks_.on_select_style(InputStyle::kTelex);
      }
      break;
    case HitTargetKind::kSegmentVni:
      if (callbacks_.on_select_style) {
        callbacks_.on_select_style(InputStyle::kVNI);
      }
      break;
    case HitTargetKind::kSegmentViqr:
      if (callbacks_.on_select_style) {
        callbacks_.on_select_style(InputStyle::kVIQR);
      }
      break;
    case HitTargetKind::kButtonGetStarted:
      Close();
      break;
    case HitTargetKind::kNone:
      break;
  }
}

LRESULT CALLBACK ShowcaseWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wparam,
                                            LPARAM lparam) {
  ShowcaseWindow* self = nullptr;
  if (msg == WM_NCCREATE) {
    const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<ShowcaseWindow*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = hwnd;
  } else {
    self = reinterpret_cast<ShowcaseWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (self != nullptr) {
    return self->HandleMessage(msg, wparam, lparam);
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT ShowcaseWindow::HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT:
      Paint();
      return 0;

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
      edit_practice_ = nullptr;
      hovered_target_ = HitTargetKind::kNone;
      tracking_mouse_ = false;
      return 0;

    default:
      break;
  }

  return DefWindowProcW(hwnd_, msg, wparam, lparam);
}

}  // namespace okeytre::win
