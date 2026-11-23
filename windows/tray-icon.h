// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_TRAY_ICON_H_
#define OKEYTRE_WINDOWS_TRAY_ICON_H_

#include <windows.h>

#include <functional>
#include <optional>
#include <string>

#include "windows/localization.h"
#include "windows/settings-store.h"
#include "windows/ui-theme.h"
#include "windows/update-parser.h"

namespace okeytre::win {

struct TrayState {
  InputStyle style = InputStyle::kTelex;
  AppLanguage language = AppLanguage::kSystem;
  bool restore_invalid_words = true;
  bool launch_at_login = false;
  bool is_english_keyboard = true;
  std::wstring keyboard_name = L"English (United States)";
  std::optional<UpdateRelease> visible_update;
};

struct TrayCallbacks {
  std::function<void(InputStyle)> on_select_style;
  std::function<void()> on_toggle_restore_invalid;
  std::function<void()> on_toggle_launch_at_login;
  std::function<void(AppLanguage)> on_select_language;
  std::function<void()> on_switch_to_us;
  std::function<void()> on_download_update;
  std::function<void()> on_skip_update;
  std::function<void()> on_open_showcase;
  std::function<void()> on_open_settings;
  std::function<void()> on_quit;
};

// Manages the Windows notification area (system tray) icon, dynamic vector
// glyph, tooltip, and native context menu matching the macOS menu bar.
class TrayIcon {
 public:
  static constexpr UINT kTrayIconId = 1;

  TrayIcon(HWND owner_hwnd, UINT callback_msg, TrayCallbacks callbacks);
  ~TrayIcon();

  TrayIcon(const TrayIcon&) = delete;
  TrayIcon& operator=(const TrayIcon&) = delete;

  bool Create(const TrayState& initial_state);
  void Update(const TrayState& state);
  void RecreateAfterExplorerRestart();
  void Destroy();

  // Handles `callback_msg` notifications from Shell_NotifyIconW.
  void HandleTrayMessage(WPARAM wparam, LPARAM lparam);

 private:
  void ShowContextMenu();
  void ApplyIconAndTooltip();
  static std::wstring BuildTooltip(const TrayState& state);
  static TrayIconKind ResolveIconKind(const TrayState& state);

  HWND owner_hwnd_ = nullptr;
  UINT callback_msg_ = 0;
  TrayCallbacks callbacks_;
  TrayState state_;
  HICON current_icon_ = nullptr;
  bool installed_ = false;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_TRAY_ICON_H_
