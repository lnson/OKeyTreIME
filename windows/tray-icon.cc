// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/tray-icon.h"

#include <shellapi.h>

#include <algorithm>

namespace okeytre::win {

namespace {

enum TrayCommandId : UINT {
  kCmdDownloadUpdate = 40001,
  kCmdSkipUpdate = 40002,
  kCmdSwitchToUs = 40003,
  kCmdStyleOff = 40010,
  kCmdStyleTelex = 40011,
  kCmdStyleVni = 40012,
  kCmdStyleViqr = 40013,
  kCmdToggleRestoreInvalid = 40020,
  kCmdToggleLaunchAtLogin = 40021,
  kCmdLangSystem = 40030,
  kCmdLangVietnamese = 40031,
  kCmdLangEnglish = 40032,
  kCmdShowcase = 40040,
  kCmdSettings = 40041,
  kCmdQuit = 40042,
};

UINT GetSystemTrayDpi(HWND hwnd) {
  if (hwnd != nullptr) {
    const UINT dpi = GetDpiForWindow(hwnd);
    if (dpi > 0) return dpi;
  }
  return GetDpiForSystem();
}

}  // namespace

TrayIcon::TrayIcon(HWND owner_hwnd, UINT callback_msg, TrayCallbacks callbacks)
    : callbacks_(std::move(callbacks)),
      owner_hwnd_(owner_hwnd),
      callback_msg_(callback_msg) {}

TrayIcon::~TrayIcon() { Destroy(); }

bool TrayIcon::Create(const TrayState& initial_state) {
  state_ = initial_state;
  Destroy();

  const UINT dpi = GetSystemTrayDpi(owner_hwnd_);
  current_icon_ =
      CreateTrayStatusIcon(ResolveIconKind(state_), IsTaskbarDarkMode(), dpi);

  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = owner_hwnd_;
  nid.uID = kTrayIconId;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  nid.uCallbackMessage = callback_msg_;
  nid.hIcon = current_icon_;

  const std::wstring tip = BuildTooltip(state_);
  wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);

  if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
    return false;
  }

  nid.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &nid);
  installed_ = true;
  return true;
}

void TrayIcon::Update(const TrayState& state) {
  state_ = state;
  if (!installed_) {
    Create(state_);
    return;
  }
  ApplyIconAndTooltip();
}

void TrayIcon::RecreateAfterExplorerRestart() {
  installed_ = false;
  Create(state_);
}

void TrayIcon::Destroy() {
  if (installed_) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner_hwnd_;
    nid.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    installed_ = false;
  }
  if (current_icon_ != nullptr) {
    DestroyIcon(current_icon_);
    current_icon_ = nullptr;
  }
}

void TrayIcon::ApplyIconAndTooltip() {
  const UINT dpi = GetSystemTrayDpi(owner_hwnd_);
  HICON next_icon =
      CreateTrayStatusIcon(ResolveIconKind(state_), IsTaskbarDarkMode(), dpi);

  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = owner_hwnd_;
  nid.uID = kTrayIconId;
  nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  nid.hIcon = next_icon;

  const std::wstring tip = BuildTooltip(state_);
  wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);

  Shell_NotifyIconW(NIM_MODIFY, &nid);

  if (current_icon_ != nullptr) {
    DestroyIcon(current_icon_);
  }
  current_icon_ = next_icon;
}

std::wstring TrayIcon::BuildTooltip(const TrayState& state) {
  std::wstring base = LocalizedFormat(
      state.language, "menu.tooltip.mode",
      InputStyleDisplayName(state.style, state.language));
  if (state.style != InputStyle::kOff && !state.is_english_keyboard) {
    base += L" · ";
    base += LocalizedFormat(state.language, "menu.input_source_warning.title",
                            state.keyboard_name);
  }
  return base;
}

TrayIconKind TrayIcon::ResolveIconKind(const TrayState& state) {
  if (state.style == InputStyle::kOff) {
    return TrayIconKind::kOff;
  }
  if (!state.is_english_keyboard) {
    return TrayIconKind::kWarning;
  }
  return TrayIconKind::kVietnameseActive;
}

void TrayIcon::HandleTrayMessage(WPARAM /*wparam*/, LPARAM lparam) {
  const UINT event = LOWORD(lparam);
  switch (event) {
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
    case WM_LBUTTONUP:
    case NIN_SELECT:
    case NIN_KEYSELECT:
      ShowContextMenu();
      break;
    case WM_LBUTTONDBLCLK:
      if (callbacks_.on_open_settings) {
        callbacks_.on_open_settings();
      }
      break;
    default:
      break;
  }
}

void TrayIcon::ShowContextMenu() {
  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) {
    return;
  }

  const AppLanguage lang = state_.language;

  // 1. Optional Update Available banner section
  if (state_.visible_update.has_value()) {
    const std::wstring title = LocalizedFormat(
        lang, "menu.update_available.title",
        Utf8ToWide(state_.visible_update->display_version()));
    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, title.c_str());
    AppendMenuW(menu, MF_STRING, kCmdDownloadUpdate,
                Localized(lang, "action.download_update").c_str());
    AppendMenuW(menu, MF_STRING, kCmdSkipUpdate,
                Localized(lang, "action.skip_version").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  }

  // 2. Optional Non-English keyboard warning section
  if (state_.style != InputStyle::kOff && !state_.is_english_keyboard) {
    const std::wstring warn_title = LocalizedFormat(
        lang, "menu.input_source_warning.title", state_.keyboard_name);
    const std::wstring warn_sub =
        Localized(lang, "menu.input_source_warning.subtitle");
    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0,
                warn_title.c_str());
    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, warn_sub.c_str());
    AppendMenuW(menu, MF_STRING, kCmdSwitchToUs,
                Localized(lang, "action.switch_to_us").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  }

  // 3. Inline Input Method radio items (Off / Telex / VNI / VIQR)
  AppendMenuW(menu, MF_STRING, kCmdStyleOff,
              InputStyleDisplayName(InputStyle::kOff, lang).c_str());
  AppendMenuW(menu, MF_STRING, kCmdStyleTelex,
              InputStyleDisplayName(InputStyle::kTelex, lang).c_str());
  AppendMenuW(menu, MF_STRING, kCmdStyleVni,
              InputStyleDisplayName(InputStyle::kVNI, lang).c_str());
  AppendMenuW(menu, MF_STRING, kCmdStyleViqr,
              InputStyleDisplayName(InputStyle::kVIQR, lang).c_str());

  UINT checked_style_cmd = kCmdStyleTelex;
  switch (state_.style) {
    case InputStyle::kOff:
      checked_style_cmd = kCmdStyleOff;
      break;
    case InputStyle::kTelex:
      checked_style_cmd = kCmdStyleTelex;
      break;
    case InputStyle::kVNI:
      checked_style_cmd = kCmdStyleVni;
      break;
    case InputStyle::kVIQR:
      checked_style_cmd = kCmdStyleViqr;
      break;
  }
  CheckMenuRadioItem(menu, kCmdStyleOff, kCmdStyleViqr, checked_style_cmd,
                     MF_BYCOMMAND);

  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  // 4. Quick Toggles & Language Submenu
  AppendMenuW(menu,
              MF_STRING | (state_.restore_invalid_words ? MF_CHECKED : 0),
              kCmdToggleRestoreInvalid,
              Localized(lang, "menu.restore_invalid_words").c_str());

  AppendMenuW(menu, MF_STRING | (state_.launch_at_login ? MF_CHECKED : 0),
              kCmdToggleLaunchAtLogin,
              Localized(lang, "menu.launch_at_login").c_str());

  HMENU lang_menu = CreatePopupMenu();
  if (lang_menu != nullptr) {
    AppendMenuW(lang_menu, MF_STRING, kCmdLangSystem,
                LanguageDisplayName(AppLanguage::kSystem, lang).c_str());
    AppendMenuW(lang_menu, MF_STRING, kCmdLangVietnamese,
                LanguageDisplayName(AppLanguage::kVietnamese, lang).c_str());
    AppendMenuW(lang_menu, MF_STRING, kCmdLangEnglish,
                LanguageDisplayName(AppLanguage::kEnglish, lang).c_str());

    UINT checked_lang_cmd = kCmdLangSystem;
    if (lang == AppLanguage::kVietnamese) checked_lang_cmd = kCmdLangVietnamese;
    if (lang == AppLanguage::kEnglish) checked_lang_cmd = kCmdLangEnglish;
    CheckMenuRadioItem(lang_menu, kCmdLangSystem, kCmdLangEnglish,
                       checked_lang_cmd, MF_BYCOMMAND);

    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(lang_menu),
                Localized(lang, "menu.language").c_str());
  }

  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  // 5. Feature Showcase & Settings
  AppendMenuW(menu, MF_STRING, kCmdShowcase,
              Localized(lang, "menu.showcase").c_str());
  AppendMenuW(menu, MF_STRING, kCmdSettings,
              Localized(lang, "menu.settings").c_str());

  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  // 6. Quit
  AppendMenuW(menu, MF_STRING, kCmdQuit, Localized(lang, "menu.quit").c_str());

  POINT pt{};
  GetCursorPos(&pt);

  // Required by Win32 shell guidelines so clicking outside dismisses the menu.
  SetForegroundWindow(owner_hwnd_);
  const UINT cmd = static_cast<UINT>(TrackPopupMenuEx(
      menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y,
      owner_hwnd_, nullptr));
  PostMessageW(owner_hwnd_, WM_NULL, 0, 0);
  DestroyMenu(menu);

  switch (cmd) {
    case kCmdDownloadUpdate:
      if (callbacks_.on_download_update) callbacks_.on_download_update();
      break;
    case kCmdSkipUpdate:
      if (callbacks_.on_skip_update) callbacks_.on_skip_update();
      break;
    case kCmdSwitchToUs:
      if (callbacks_.on_switch_to_us) callbacks_.on_switch_to_us();
      break;
    case kCmdStyleOff:
      if (callbacks_.on_select_style) callbacks_.on_select_style(InputStyle::kOff);
      break;
    case kCmdStyleTelex:
      if (callbacks_.on_select_style) callbacks_.on_select_style(InputStyle::kTelex);
      break;
    case kCmdStyleVni:
      if (callbacks_.on_select_style) callbacks_.on_select_style(InputStyle::kVNI);
      break;
    case kCmdStyleViqr:
      if (callbacks_.on_select_style) callbacks_.on_select_style(InputStyle::kVIQR);
      break;
    case kCmdToggleRestoreInvalid:
      if (callbacks_.on_toggle_restore_invalid) {
        callbacks_.on_toggle_restore_invalid();
      }
      break;
    case kCmdToggleLaunchAtLogin:
      if (callbacks_.on_toggle_launch_at_login) {
        callbacks_.on_toggle_launch_at_login();
      }
      break;
    case kCmdLangSystem:
      if (callbacks_.on_select_language) {
        callbacks_.on_select_language(AppLanguage::kSystem);
      }
      break;
    case kCmdLangVietnamese:
      if (callbacks_.on_select_language) {
        callbacks_.on_select_language(AppLanguage::kVietnamese);
      }
      break;
    case kCmdLangEnglish:
      if (callbacks_.on_select_language) {
        callbacks_.on_select_language(AppLanguage::kEnglish);
      }
      break;
    case kCmdShowcase:
      if (callbacks_.on_open_showcase) callbacks_.on_open_showcase();
      break;
    case kCmdSettings:
      if (callbacks_.on_open_settings) callbacks_.on_open_settings();
      break;
    case kCmdQuit:
      if (callbacks_.on_quit) callbacks_.on_quit();
      break;
    default:
      break;
  }
}

}  // namespace okeytre::win
