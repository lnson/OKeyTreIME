// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_SETTINGS_WINDOW_H_
#define OKEYTRE_WINDOWS_SETTINGS_WINDOW_H_

#include <windows.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "windows/localization.h"
#include "windows/settings-store.h"
#include "windows/ui-theme.h"
#include "windows/update-parser.h"

namespace okeytre::win {

enum class SettingsSection {
  kGeneral = 0,
  kCorrection = 1,
  kAdvanced = 2,
};

struct SettingsViewModel {
  AppLanguage language = AppLanguage::kSystem;
  InputStyle style = InputStyle::kTelex;
  bool launch_at_login = false;
  bool auto_check_updates = true;
  bool is_checking_updates = false;
  bool did_confirm_up_to_date = false;
  std::optional<UpdateRelease> available_release;
  std::wstring current_version = L"0.3.2";

  bool modern_tone = true;
  bool restore_invalid_words = true;
  bool uo_horn = false;
  bool ng_to_ngh = false;
  bool ngh_to_ng = false;

  int32_t max_buffer_size = 16;
  int32_t min_buffer_size = 10;
  int32_t max_allowed_buffer_size = 512;
};

struct SettingsCallbacks {
  std::function<void(AppLanguage)> on_select_language;
  std::function<void(InputStyle)> on_select_style;
  std::function<void(bool)> on_set_launch_at_login;
  std::function<void(bool)> on_set_auto_check_updates;
  std::function<void()> on_check_updates_now;
  std::function<void()> on_download_update;
  std::function<void()> on_open_releases_page;
  std::function<void(bool)> on_set_modern_tone;
  std::function<void(bool)> on_set_restore_invalid;
  std::function<void(bool)> on_set_uo_horn;
  std::function<void(bool)> on_set_ng_to_ngh;
  std::function<void(bool)> on_set_ngh_to_ng;
  std::function<void(int32_t)> on_set_max_buffer_size;
};

// Native Win32 + GDI+ Windows 11 Fluent Settings window mirroring macOS
// `ContentView` (sidebar navigation + General / Tones & Spelling / Advanced).
class SettingsWindow {
 public:
  explicit SettingsWindow(SettingsCallbacks callbacks);
  ~SettingsWindow();

  SettingsWindow(const SettingsWindow&) = delete;
  SettingsWindow& operator=(const SettingsWindow&) = delete;

  void Show(const SettingsViewModel& model);
  void Update(const SettingsViewModel& model);
  void RefreshTheme();
  void Close();
  bool IsVisible() const;

 private:
  enum class HitTargetKind {
    kNone,
    kNavGeneral,
    kNavCorrection,
    kNavAdvanced,
    kToggleLaunchAtLogin,
    kToggleAutoCheck,
    kButtonDownloadUpdate,
    kButtonCheckUpdates,
    kLinkManualDownload,
    kToggleModernTone,
    kToggleRestoreInvalid,
    kToggleUoHorn,
    kToggleNgToNgh,
    kToggleNghToNg,
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
  void PaintSidebar(Gdiplus::Graphics& g, int width, int height);
  void PaintDetailPane(Gdiplus::Graphics& g, int width, int height);
  void PaintGeneralPane(Gdiplus::Graphics& g, float x, float y, float w);
  void PaintCorrectionPane(Gdiplus::Graphics& g, float x, float y, float w);
  void PaintAdvancedPane(Gdiplus::Graphics& g, float x, float y, float w);

  HitTargetKind HitTest(int x, int y) const;
  void HandleClick(HitTargetKind target);

  SettingsCallbacks callbacks_;
  SettingsViewModel model_;
  SettingsSection section_ = SettingsSection::kGeneral;
  ThemePalette palette_;
  UINT dpi_ = 96;

  HWND hwnd_ = nullptr;
  HWND combo_language_ = nullptr;
  HWND combo_style_ = nullptr;
  HWND edit_buffer_size_ = nullptr;
  HWND updown_buffer_size_ = nullptr;

  HFONT ui_font_ = nullptr;
  HBRUSH brush_sidebar_ = nullptr;
  HBRUSH brush_card_ = nullptr;

  std::vector<HitRegion> hit_regions_;
  HitTargetKind hovered_target_ = HitTargetKind::kNone;
  bool updating_controls_ = false;
  bool tracking_mouse_ = false;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_SETTINGS_WINDOW_H_
