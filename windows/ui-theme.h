// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_UI_THEME_H_
#define OKEYTRE_WINDOWS_UI_THEME_H_

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <string_view>

namespace okeytre::win {

struct ThemePalette {
  bool dark = false;

  Gdiplus::Color window_bg;
  Gdiplus::Color sidebar_bg;
  Gdiplus::Color card_bg;
  Gdiplus::Color card_border;
  Gdiplus::Color divider;
  Gdiplus::Color hover_bg;
  Gdiplus::Color selected_bg;
  Gdiplus::Color control_bg;
  Gdiplus::Color control_border;

  Gdiplus::Color text_primary;
  Gdiplus::Color text_secondary;
  Gdiplus::Color text_on_accent;

  Gdiplus::Color accent_teal;
  Gdiplus::Color accent_teal_hover;
  Gdiplus::Color accent_teal_soft;
  Gdiplus::Color accent_blue;
  Gdiplus::Color accent_gray;
  Gdiplus::Color accent_orange;
  Gdiplus::Color accent_orange_soft;
  Gdiplus::Color accent_orange_border;
  Gdiplus::Color accent_green;
  Gdiplus::Color accent_green_soft;
  Gdiplus::Color accent_green_border;

  COLORREF win32_window_bg = RGB(243, 243, 243);
  COLORREF win32_sidebar_bg = RGB(236, 236, 236);
  COLORREF win32_card_bg = RGB(255, 255, 255);
  COLORREF win32_text_primary = RGB(27, 27, 27);
  COLORREF win32_text_secondary = RGB(95, 95, 95);
};

enum class TrayIconKind {
  kVietnameseActive = 0,
  kOff = 1,
  kWarning = 2,
};

// Queries Windows Personalize registry keys for Light/Dark mode.
bool IsAppDarkMode();
bool IsTaskbarDarkMode();

ThemePalette GetThemePalette(bool dark_mode);

// Applies Windows 11 dark titlebar and rounded corners when supported.
void ApplyWindowThemeAttributes(HWND hwnd, bool dark_mode);

int ScaleDpi(int value, UINT dpi) noexcept;
float ScaleDpiF(float value, UINT dpi) noexcept;

// Creates a Segoe UI Variable (or Segoe UI fallback) HFONT for native Win32
// child controls at the given DPI.
HFONT CreateUiFont(int point_size, int weight, UINT dpi);

// Loads the embedded high-resolution app icon PNG resource (cached).
Gdiplus::Bitmap* GetAppLogoBitmap();

// GDI+ Fluent drawing primitives.
void AddRoundedRectPath(Gdiplus::GraphicsPath& path,
                        const Gdiplus::RectF& rect, float radius);
void FillRoundedRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect,
                     float radius, const Gdiplus::Color& color);
void StrokeRoundedRect(Gdiplus::Graphics& g, const Gdiplus::RectF& rect,
                       float radius, const Gdiplus::Color& color,
                       float width = 1.0f);

void DrawToggleSwitch(Gdiplus::Graphics& g, const Gdiplus::RectF& rect,
                      bool is_on, bool is_hovered, const ThemePalette& palette);

void DrawFluentButton(Gdiplus::Graphics& g, const Gdiplus::RectF& rect,
                      std::wstring_view text, bool is_primary, bool is_hovered,
                      bool is_disabled, const ThemePalette& palette, UINT dpi);

void DrawAppLogo(Gdiplus::Graphics& g, const Gdiplus::RectF& rect,
                 float corner_radius);

// Renders a crisp DPI-aware vector tray HICON ("Vi |" active cursor, slashed
// Off icon, or amber Warning triangle). Caller owns the returned HICON.
HICON CreateTrayStatusIcon(TrayIconKind kind, bool dark_taskbar, UINT dpi);

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_UI_THEME_H_
