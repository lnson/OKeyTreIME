// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_SHOWCASE_WINDOW_H_
#define OKEYTRE_WINDOWS_SHOWCASE_WINDOW_H_

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "windows/localization.h"
#include "windows/settings-store.h"
#include "windows/ui-theme.h"

namespace okeytre::win {

struct ShowcaseViewModel {
  ShowcaseMode mode = ShowcaseMode::kFirstInstall;
  AppLanguage language = AppLanguage::kSystem;
  InputStyle style = InputStyle::kTelex;
  bool is_english_keyboard = true;
  std::wstring keyboard_name = L"English (United States)";
  std::wstring milestone_version = L"0.3";
};

struct ShowcaseCallbacks {
  std::function<void(AppLanguage)> on_select_language;
  std::function<void(InputStyle)> on_select_style;
  std::function<void()> on_switch_to_us;
};

// Native Win32 + GDI+ Feature Showcase window mirroring macOS `ShowcaseView`
// (hero header, live status banner, 2x2 interactive shortcut cards, practice
// typing box, Telex/VNI/VIQR segmented selector, and Get Started action).
class ShowcaseWindow {
 public:
  explicit ShowcaseWindow(ShowcaseCallbacks callbacks);
  ~ShowcaseWindow();

  ShowcaseWindow(const ShowcaseWindow&) = delete;
  ShowcaseWindow& operator=(const ShowcaseWindow&) = delete;

  void Show(const ShowcaseViewModel& model);
  void Update(const ShowcaseViewModel& model);
  void RefreshTheme();
  void Close();
  bool IsVisible() const;

 private:
  enum class HitTargetKind {
    kNone,
    kButtonSwitchToUs,
    kSegmentTelex,
    kSegmentVni,
    kSegmentViqr,
    kButtonGetStarted,
  };

  struct HitRegion {
    HitTargetKind kind = HitTargetKind::kNone;
    Gdiplus::RectF rect;
  };

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam,
                                     LPARAM lparam);
  LRESULT HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam);

  void EnsureWindowClass();
  void CreateChildControls();
  void UpdateFontsAndBrushes();
  void SyncControlsFromModel();
  void LayoutChildControls();
  void Paint();
  void PaintStatusBanner(Gdiplus::Graphics& g, float x, float y, float w);
  void PaintFeatureGrid(Gdiplus::Graphics& g, float x, float y, float w);
  void PaintPracticeHeader(Gdiplus::Graphics& g, float x, float y, float w);
  void PaintFooter(Gdiplus::Graphics& g, float x, float y, float w);

  HitTargetKind HitTest(int x, int y) const;
  void HandleClick(HitTargetKind target);
  std::wstring ResolveWindowTitle() const;

  ShowcaseCallbacks callbacks_;
  ShowcaseViewModel model_;
  ThemePalette palette_;
  UINT dpi_ = 96;

  HWND hwnd_ = nullptr;
  HWND combo_language_ = nullptr;
  HWND edit_practice_ = nullptr;

  HFONT ui_font_ = nullptr;
  HFONT practice_font_ = nullptr;
  HBRUSH brush_card_ = nullptr;

  std::vector<HitRegion> hit_regions_;
  HitTargetKind hovered_target_ = HitTargetKind::kNone;
  bool updating_controls_ = false;
  bool tracking_mouse_ = false;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_SHOWCASE_WINDOW_H_
