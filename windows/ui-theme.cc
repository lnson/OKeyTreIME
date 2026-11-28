// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/ui-theme.h"

#include <dwmapi.h>
#include <shlwapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "windows/resources/resource.h"

namespace okeytre::win {

namespace {

constexpr wchar_t kPersonalizeSubKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

bool QueryPersonalizeLightFlag(const wchar_t* value_name,
                               bool default_light = true) {
  HKEY hkey = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kPersonalizeSubKey, 0, KEY_READ,
                    &hkey) != ERROR_SUCCESS) {
    return default_light;
  }
  DWORD val = default_light ? 1u : 0u;
  DWORD size = sizeof(val);
  DWORD type = 0;
  const LONG rc = RegQueryValueExW(hkey, value_name, nullptr, &type,
                                   reinterpret_cast<LPBYTE>(&val), &size);
  RegCloseKey(hkey);
  if (rc != ERROR_SUCCESS || type != REG_DWORD) {
    return default_light;
  }
  return val != 0;
}

}  // namespace

const wchar_t* PreferredUiFontFamily() {
  static const wchar_t* const kFamily = []() -> const wchar_t* {
    Gdiplus::FontFamily var_family(L"Segoe UI Variable Text");
    if (var_family.IsAvailable()) {
      return L"Segoe UI Variable Text";
    }
    return L"Segoe UI";
  }();
  return kFamily;
}

bool IsAppDarkMode() {
  return !QueryPersonalizeLightFlag(L"AppsUseLightTheme", true);
}

bool IsTaskbarDarkMode() {
  return !QueryPersonalizeLightFlag(L"SystemUsesLightTheme", false);
}

ThemePalette GetThemePalette(bool dark_mode) {
  ThemePalette p;
  p.dark = dark_mode;
  if (!dark_mode) {
    p.window_bg = Gdiplus::Color(255, 243, 243, 243);
    p.sidebar_bg = Gdiplus::Color(255, 236, 236, 238);
    p.card_bg = Gdiplus::Color(255, 255, 255, 255);
    p.card_border = Gdiplus::Color(255, 226, 226, 230);
    p.divider = Gdiplus::Color(255, 235, 235, 239);
    p.hover_bg = Gdiplus::Color(255, 232, 232, 236);
    p.selected_bg = Gdiplus::Color(255, 220, 222, 228);
    p.control_bg = Gdiplus::Color(255, 250, 250, 252);
    p.control_border = Gdiplus::Color(255, 200, 200, 206);

    p.text_primary = Gdiplus::Color(255, 26, 26, 28);
    p.text_secondary = Gdiplus::Color(255, 96, 96, 104);
    p.text_on_accent = Gdiplus::Color(255, 255, 255, 255);

    p.accent_teal = Gdiplus::Color(255, 13, 148, 136);
    p.accent_teal_hover = Gdiplus::Color(255, 15, 118, 110);
    p.accent_teal_soft = Gdiplus::Color(36, 13, 148, 136);
    p.accent_blue = Gdiplus::Color(255, 0, 103, 192);
    p.accent_gray = Gdiplus::Color(255, 110, 114, 122);
    p.accent_orange = Gdiplus::Color(255, 217, 119, 6);
    p.accent_orange_soft = Gdiplus::Color(28, 245, 158, 11);
    p.accent_orange_border = Gdiplus::Color(80, 217, 119, 6);
    p.accent_green = Gdiplus::Color(255, 22, 163, 74);
    p.accent_green_soft = Gdiplus::Color(26, 34, 197, 94);
    p.accent_green_border = Gdiplus::Color(70, 22, 163, 74);

    p.win32_window_bg = RGB(243, 243, 243);
    p.win32_sidebar_bg = RGB(236, 236, 238);
    p.win32_card_bg = RGB(255, 255, 255);
    p.win32_text_primary = RGB(26, 26, 28);
    p.win32_text_secondary = RGB(96, 96, 104);
  } else {
    p.window_bg = Gdiplus::Color(255, 32, 32, 34);
    p.sidebar_bg = Gdiplus::Color(255, 25, 25, 27);
    p.card_bg = Gdiplus::Color(255, 43, 43, 47);
    p.card_border = Gdiplus::Color(255, 58, 58, 64);
    p.divider = Gdiplus::Color(255, 56, 56, 62);
    p.hover_bg = Gdiplus::Color(255, 52, 52, 58);
    p.selected_bg = Gdiplus::Color(255, 62, 62, 68);
    p.control_bg = Gdiplus::Color(255, 50, 50, 55);
    p.control_border = Gdiplus::Color(255, 80, 80, 88);

    p.text_primary = Gdiplus::Color(255, 245, 245, 247);
    p.text_secondary = Gdiplus::Color(255, 172, 172, 180);
    p.text_on_accent = Gdiplus::Color(255, 255, 255, 255);

    p.accent_teal = Gdiplus::Color(255, 20, 184, 166);
    p.accent_teal_hover = Gdiplus::Color(255, 45, 212, 191);
    p.accent_teal_soft = Gdiplus::Color(44, 45, 212, 191);
    p.accent_blue = Gdiplus::Color(255, 96, 165, 250);
    p.accent_gray = Gdiplus::Color(255, 128, 132, 142);
    p.accent_orange = Gdiplus::Color(255, 245, 158, 11);
    p.accent_orange_soft = Gdiplus::Color(36, 245, 158, 11);
    p.accent_orange_border = Gdiplus::Color(92, 245, 158, 11);
    p.accent_green = Gdiplus::Color(255, 34, 197, 94);
    p.accent_green_soft = Gdiplus::Color(34, 34, 197, 94);
    p.accent_green_border = Gdiplus::Color(84, 34, 197, 94);

    p.win32_window_bg = RGB(32, 32, 34);
    p.win32_sidebar_bg = RGB(25, 25, 27);
    p.win32_card_bg = RGB(43, 43, 47);
    p.win32_text_primary = RGB(245, 245, 247);
    p.win32_text_secondary = RGB(172, 172, 180);
  }
  return p;
}

void ApplyWindowThemeAttributes(HWND hwnd, bool dark_mode) {
  if (hwnd == nullptr) {
    return;
  }
  // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 20H1+ & Windows 11).
  constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
  const BOOL dark = dark_mode ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark));

  // DWMWA_WINDOW_CORNER_PREFERENCE = 33 (Windows 11).
  constexpr DWORD kDwmwaWindowCornerPreference = 33;
  constexpr DWORD kDwmwcpRound = 2;
  DwmSetWindowAttribute(hwnd, kDwmwaWindowCornerPreference, &kDwmwcpRound,
                        sizeof(kDwmwcpRound));
}

void ApplyControlTheme(HWND hwnd, bool dark_mode) {
  if (hwnd == nullptr) {
    return;
  }
  SetWindowTheme(hwnd, dark_mode ? L"DarkMode_CFD" : L"Explorer", nullptr);
}

void RefreshNativeMenuTheme(bool dark_mode) {
  HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll");
  if (uxtheme == nullptr) {
    uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr,
                             LOAD_LIBRARY_SEARCH_SYSTEM32);
  }
  if (uxtheme == nullptr) {
    return;
  }

  enum class PreferredAppMode : int {
    kDefault = 0,
    kAllowDark = 1,
    kForceDark = 2,
    kForceLight = 3,
    kMax = 4,
  };
  using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
  using FlushMenuThemesFn = void(WINAPI*)();

  auto set_preferred_app_mode = reinterpret_cast<SetPreferredAppModeFn>(
      GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
  auto flush_menu_themes = reinterpret_cast<FlushMenuThemesFn>(
      GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
  if (set_preferred_app_mode != nullptr) {
    set_preferred_app_mode(dark_mode ? PreferredAppMode::kForceDark
                                     : PreferredAppMode::kForceLight);
  }
  if (flush_menu_themes != nullptr) {
    flush_menu_themes();
  }
}

int ScaleDpi(int value, UINT dpi) noexcept {
  return MulDiv(value, static_cast<int>(dpi), 96);
}

float ScaleDpiF(float value, UINT dpi) noexcept {
  return value * static_cast<float>(dpi) / 96.0f;
}

HFONT CreateUiFont(int point_size, int weight, UINT dpi) {
  LOGFONTW lf{};
  lf.lfHeight = -MulDiv(point_size, static_cast<int>(dpi), 72);
  lf.lfWeight = weight;
  lf.lfQuality = CLEARTYPE_QUALITY;
  const wchar_t* family = PreferredUiFontFamily();
  wcsncpy_s(lf.lfFaceName, family, _TRUNCATE);
  return CreateFontIndirectW(&lf);
}

Gdiplus::Bitmap* GetAppLogoBitmap() {
  static std::unique_ptr<Gdiplus::Bitmap> logo = []() {
    HMODULE mod = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(IDR_APP_LOGO_PNG),
                              RT_RCDATA);
    if (res == nullptr) {
      return std::unique_ptr<Gdiplus::Bitmap>();
    }
    const DWORD size = SizeofResource(mod, res);
    HGLOBAL loaded = LoadResource(mod, res);
    if (loaded == nullptr || size == 0) {
      return std::unique_ptr<Gdiplus::Bitmap>();
    }
    const void* data = LockResource(loaded);
    if (data == nullptr) {
      return std::unique_ptr<Gdiplus::Bitmap>();
    }
    IStream* stream = SHCreateMemStream(static_cast<const BYTE*>(data), size);
    if (stream == nullptr) {
      return std::unique_ptr<Gdiplus::Bitmap>();
    }
    std::unique_ptr<Gdiplus::Bitmap> bmp(Gdiplus::Bitmap::FromStream(stream));
    stream->Release();
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
      return std::unique_ptr<Gdiplus::Bitmap>();
    }
    return bmp;
  }();
  return logo.get();
}

void AddRoundedRectPath(Gdiplus::GraphicsPath& path,
                        const Gdiplus::RectF& rect, float radius) {
  const float r =
      std::min(radius, std::min(rect.Width, rect.Height) * 0.5f);
  if (r <= 0.1f) {
    path.AddRectangle(rect);
    return;
  }
  const float d = r * 2.0f;
  path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
  path.AddArc(rect.GetRight() - d, rect.Y, d, d, 270.0f, 90.0f);
  path.AddArc(rect.GetRight() - d, rect.GetBottom() - d, d, d, 0.0f, 90.0f);
  path.AddArc(rect.X, rect.GetBottom() - d, d, d, 90.0f, 90.0f);
  path.CloseFigure();
}

void FillRoundedRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect,
                     float radius, const Gdiplus::Color& color) {
  Gdiplus::GraphicsPath path;
  AddRoundedRectPath(path, rect, radius);
  Gdiplus::SolidBrush brush(color);
  g.FillPath(&brush, &path);
}

void StrokeRoundedRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect,
                       float radius, const Gdiplus::Color& color, float width) {
  Gdiplus::GraphicsPath path;
  AddRoundedRectPath(path, rect, radius);
  Gdiplus::Pen pen(color, width);
  g.DrawPath(&pen, &path);
}

void DrawToggleSwitch(Gdiplus::Graphics& g, const Gdiplus::RectF& rect,
                      bool is_on, bool is_hovered,
                      const ThemePalette& palette) {
  const float radius = rect.Height * 0.5f;
  if (is_on) {
    const Gdiplus::Color fill =
        is_hovered ? palette.accent_teal_hover : palette.accent_teal;
    FillRoundedRect(g, rect, radius, fill);
  } else {
    const Gdiplus::Color fill =
        is_hovered ? palette.hover_bg : palette.control_bg;
    FillRoundedRect(g, rect, radius, fill);
    StrokeRoundedRect(g, rect, radius, palette.control_border, 1.2f);
  }

  const float pad = rect.Height * 0.18f;
  const float knob_d = rect.Height - pad * 2.0f;
  const float knob_x = is_on ? (rect.GetRight() - pad - knob_d) : (rect.X + pad);
  const Gdiplus::Color knob_color =
      is_on ? palette.text_on_accent : palette.text_secondary;
  Gdiplus::SolidBrush knob_brush(knob_color);
  g.FillEllipse(&knob_brush, knob_x, rect.Y + pad, knob_d, knob_d);
}

void DrawFluentButton(Gdiplus::Graphics& g, const Gdiplus::RectF& rect,
                      std::wstring_view text, bool is_primary, bool is_hovered,
                      bool is_disabled, const ThemePalette& palette, UINT dpi) {
  const float radius = ScaleDpiF(6.0f, dpi);
  Gdiplus::Color fill;
  Gdiplus::Color border;
  Gdiplus::Color text_color;

  if (is_primary) {
    fill = is_disabled ? palette.accent_gray
                       : (is_hovered ? palette.accent_teal_hover
                                     : palette.accent_teal);
    border = fill;
    text_color = palette.text_on_accent;
  } else {
    fill = is_hovered && !is_disabled ? palette.hover_bg : palette.control_bg;
    border = palette.control_border;
    text_color = is_disabled ? palette.text_secondary : palette.text_primary;
  }

  FillRoundedRect(g, rect, radius, fill);
  if (!is_primary) {
    StrokeRoundedRect(g, rect, radius, border, 1.0f);
  }

  const wchar_t* family = PreferredUiFontFamily();
  Gdiplus::Font font(family, ScaleDpiF(12.5f, dpi),
                     is_primary ? Gdiplus::FontStyleBold
                                : Gdiplus::FontStyleRegular,
                     Gdiplus::UnitPixel);
  Gdiplus::SolidBrush brush(text_color);
  Gdiplus::StringFormat fmt;
  fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
  fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
  g.DrawString(text.data(), static_cast<INT>(text.size()), &font, rect, &fmt,
               &brush);
}

void DrawFluentDropdown(Gdiplus::Graphics& g, const Gdiplus::RectF& rect,
                        std::wstring_view text, bool is_hovered,
                        const ThemePalette& palette, UINT dpi) {
  const float radius = ScaleDpiF(6.0f, dpi);
  const Gdiplus::Color fill =
      is_hovered ? palette.hover_bg : palette.control_bg;
  FillRoundedRect(g, rect, radius, fill);
  StrokeRoundedRect(g, rect, radius, palette.control_border, 1.0f);

  const float pad_l = ScaleDpiF(10.0f, dpi);
  const float chev_area = ScaleDpiF(22.0f, dpi);
  const wchar_t* family = PreferredUiFontFamily();
  Gdiplus::Font font(family, ScaleDpiF(12.0f, dpi),
                     Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush text_brush(palette.text_primary);
  Gdiplus::StringFormat fmt;
  fmt.SetAlignment(Gdiplus::StringAlignmentNear);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
  fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
  Gdiplus::RectF text_rect(rect.X + pad_l, rect.Y,
                           std::max(0.0f, rect.Width - pad_l - chev_area),
                           rect.Height);
  g.DrawString(text.data(), static_cast<INT>(text.size()), &font, text_rect,
               &fmt, &text_brush);

  // Subtle downward chevron.
  const float cx = rect.GetRight() - ScaleDpiF(12.0f, dpi);
  const float cy = rect.Y + rect.Height * 0.5f;
  const float hw = ScaleDpiF(3.5f, dpi);
  const float hh = ScaleDpiF(2.0f, dpi);
  Gdiplus::Pen pen(palette.text_secondary, ScaleDpiF(1.3f, dpi));
  pen.SetStartCap(Gdiplus::LineCapRound);
  pen.SetEndCap(Gdiplus::LineCapRound);
  pen.SetLineJoin(Gdiplus::LineJoinRound);
  g.DrawLine(&pen, cx - hw, cy - hh, cx, cy + hh);
  g.DrawLine(&pen, cx, cy + hh, cx + hw, cy - hh);
}

void DrawAppLogo(Gdiplus::Graphics& g, const Gdiplus::RectF& rect,
                 float corner_radius) {
  Gdiplus::Bitmap* bmp = GetAppLogoBitmap();
  if (bmp == nullptr) {
    FillRoundedRect(g, rect, corner_radius, Gdiplus::Color(255, 13, 148, 136));
    return;
  }
  Gdiplus::GraphicsState saved = g.Save();
  Gdiplus::GraphicsPath clip;
  AddRoundedRectPath(clip, rect, corner_radius);
  g.SetClip(&clip);
  g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  g.DrawImage(bmp, rect);
  g.Restore(saved);
}

HICON CreateTrayStatusIcon(TrayIconKind kind, bool dark_taskbar, UINT dpi) {
  const int sm_icon = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
  const int size = std::max({16, sm_icon, ScaleDpi(16, dpi)});

  if (kind == TrayIconKind::kVietnameseActive || kind == TrayIconKind::kOff) {
    const WORD res_id = (kind == TrayIconKind::kVietnameseActive)
                            ? IDI_TRAY_VI_ICON
                            : IDI_TRAY_OFF_ICON;
    HICON loaded = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(res_id), IMAGE_ICON, size,
        size, LR_DEFAULTCOLOR));
    if (loaded != nullptr) {
      return loaded;
    }
  }

  Gdiplus::Bitmap bmp(size, size, PixelFormat32bppARGB);
  Gdiplus::Graphics g(&bmp);
  g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
  g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
  g.Clear(Gdiplus::Color(0, 0, 0, 0));

  const float s = static_cast<float>(size);
  const Gdiplus::Color fg = dark_taskbar ? Gdiplus::Color(255, 245, 245, 245)
                                         : Gdiplus::Color(255, 28, 28, 30);

  switch (kind) {
    case TrayIconKind::kVietnameseActive:
      break;

    case TrayIconKind::kOff: {
      // Clean slashed circle ("nosign").
      const float stroke = std::max(1.6f, s * 0.11f);
      Gdiplus::Pen pen(fg, stroke);
      pen.SetStartCap(Gdiplus::LineCapRound);
      pen.SetEndCap(Gdiplus::LineCapRound);
      const float pad = s * 0.16f;
      g.DrawEllipse(&pen, pad, pad, s - pad * 2.0f, s - pad * 2.0f);
      const float offset = s * 0.27f;
      g.DrawLine(&pen, offset, offset, s - offset, s - offset);
      break;
    }

    case TrayIconKind::kWarning: {
      // Amber warning triangle with exclamation mark.
      Gdiplus::PointF pts[3] = {
          {s * 0.50f, s * 0.10f},
          {s * 0.92f, s * 0.88f},
          {s * 0.08f, s * 0.88f},
      };
      Gdiplus::SolidBrush fill(Gdiplus::Color(255, 245, 158, 11));
      g.FillPolygon(&fill, pts, 3);

      const float stroke = std::max(1.5f, s * 0.10f);
      Gdiplus::Pen bang_pen(Gdiplus::Color(255, 24, 24, 24), stroke);
      bang_pen.SetStartCap(Gdiplus::LineCapRound);
      bang_pen.SetEndCap(Gdiplus::LineCapRound);
      g.DrawLine(&bang_pen, s * 0.50f, s * 0.36f, s * 0.50f, s * 0.62f);

      Gdiplus::SolidBrush dot_brush(Gdiplus::Color(255, 24, 24, 24));
      const float dot_r = std::max(1.0f, s * 0.07f);
      g.FillEllipse(&dot_brush, s * 0.50f - dot_r, s * 0.74f - dot_r,
                    dot_r * 2.0f, dot_r * 2.0f);
      break;
    }
  }

  HICON hicon = nullptr;
  bmp.GetHICON(&hicon);
  return hicon;
}

}  // namespace okeytre::win
