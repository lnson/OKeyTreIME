// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/input-source-manager.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <iterator>

namespace okeytre::win {

namespace {

HKL GetActiveKeyboardLayout() {
  HWND fg = GetForegroundWindow();
  DWORD thread_id = fg != nullptr ? GetWindowThreadProcessId(fg, nullptr) : 0;
  HKL hkl = GetKeyboardLayout(thread_id);
  if (hkl == nullptr) {
    hkl = GetKeyboardLayout(0);
  }
  return hkl;
}

bool IsEnglishKeyboardLayout(HKL hkl) {
  if (hkl == nullptr) {
    return true;
  }
  const LANGID lang_id = LOWORD(reinterpret_cast<uintptr_t>(hkl));
  return PRIMARYLANGID(lang_id) == LANG_ENGLISH;
}

std::wstring GetKeyboardLayoutDisplayName(HKL hkl, AppLanguage language) {
  if (hkl == nullptr) {
    return Localized(language, "input_source.unknown");
  }
  const LANGID lang_id = LOWORD(reinterpret_cast<uintptr_t>(hkl));
  const LCID lcid = MAKELCID(lang_id, SORT_DEFAULT);
  wchar_t locale_name[LOCALE_NAME_MAX_LENGTH] = {};
  if (LCIDToLocaleName(lcid, locale_name, LOCALE_NAME_MAX_LENGTH, 0) > 0) {
    wchar_t display_name[128] = {};
    if (GetLocaleInfoEx(locale_name, LOCALE_SLOCALIZEDDISPLAYNAME, display_name,
                        128) > 0) {
      return std::wstring(display_name);
    }
  }
  return Localized(language, "input_source.unknown");
}

}  // namespace

InputSourceManager::InputSourceManager() { Refresh(AppLanguage::kSystem); }

bool InputSourceManager::Refresh(AppLanguage language) {
  const HKL hkl = GetActiveKeyboardLayout();
  const bool next_english = IsEnglishKeyboardLayout(hkl);
  const std::wstring next_name = GetKeyboardLayoutDisplayName(hkl, language);

  if (next_english == is_english_ && next_name == current_source_name_) {
    return false;
  }

  is_english_ = next_english;
  current_source_name_ = next_name;
  if (on_changed_) {
    on_changed_();
  }
  return true;
}

bool InputSourceManager::SwitchToEnglishKeyboard(AppLanguage language) {
  HKL us_hkl = LoadKeyboardLayoutW(L"00000409", KLF_ACTIVATE);
  if (us_hkl == nullptr) {
    return false;
  }
  ActivateKeyboardLayout(us_hkl, KLF_SETFORPROCESS);
  if (HWND fg = GetForegroundWindow(); fg != nullptr) {
    PostMessageW(fg, WM_INPUTLANGCHANGEREQUEST, 0,
                 reinterpret_cast<LPARAM>(us_hkl));
  }
  is_english_ = true;
  current_source_name_ = GetKeyboardLayoutDisplayName(us_hkl, language);
  if (on_changed_) {
    on_changed_();
  }
  return true;
}

void InputSourceManager::CheckAndWarnOnStartup(AppLanguage language) {
  Refresh(language);
  if (is_english_) {
    return;
  }

  const std::wstring title =
      Localized(language, "alert.non_english_keyboard.title");
  const std::wstring message = LocalizedFormat(
      language, "alert.non_english_keyboard.message", current_source_name_);
  const std::wstring btn_switch = Localized(language, "action.switch_to_us");
  const std::wstring btn_settings = Localized(
      language, "alert.non_english_keyboard.open_keyboard_settings");
  const std::wstring btn_ignore =
      Localized(language, "alert.non_english_keyboard.ignore");

  constexpr int kBtnSwitchId = 1001;
  constexpr int kBtnSettingsId = 1002;
  constexpr int kBtnIgnoreId = 1003;

  const TASKDIALOG_BUTTON buttons[] = {
      {kBtnSwitchId, btn_switch.c_str()},
      {kBtnSettingsId, btn_settings.c_str()},
      {kBtnIgnoreId, btn_ignore.c_str()},
  };

  TASKDIALOGCONFIG cfg{};
  cfg.cbSize = sizeof(cfg);
  cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
  cfg.pszWindowTitle = L"ÔKêyTre";
  cfg.pszMainIcon = TD_WARNING_ICON;
  cfg.pszMainInstruction = title.c_str();
  cfg.pszContent = message.c_str();
  cfg.cButtons = static_cast<UINT>(std::size(buttons));
  cfg.pButtons = buttons;
  cfg.nDefaultButton = kBtnSwitchId;

  int clicked = 0;
  if (SUCCEEDED(TaskDialogIndirect(&cfg, &clicked, nullptr, nullptr))) {
    if (clicked == kBtnSwitchId) {
      SwitchToEnglishKeyboard(language);
    } else if (clicked == kBtnSettingsId) {
      ShellExecuteW(nullptr, L"open", L"ms-settings:regionlanguage", nullptr,
                    nullptr, SW_SHOWNORMAL);
    }
  }
}

}  // namespace okeytre::win
