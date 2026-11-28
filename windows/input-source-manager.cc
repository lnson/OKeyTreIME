// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/input-source-manager.h"

// clang-format off
#include <windows.h>
// clang-format on
#include <commctrl.h>
#include <shellapi.h>

#include <cwchar>
#include <iterator>

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"

namespace okeytre::win {

namespace {

struct SpecialLayoutInfo {
  std::wstring layout_text;
  LANGID lang_id = 0;
};

// Maps the 12-bit Windows `"Layout Id"` (stored in `HIWORD(hkl) & 0x0FFF` when
// `(HIWORD(hkl) & 0xF000) == 0xF000`) to its underlying KLID language and name.
// This detects IME/keyboard variants such as Windows built-in Vietnamese Telex
// (`0001042a`) or VNI (`0002042a`) even when installed under `en-US` (`0409`).
const absl::flat_hash_map<uint16_t, SpecialLayoutInfo>& GetSpecialLayoutMap() {
  static const absl::NoDestructor<
      absl::flat_hash_map<uint16_t, SpecialLayoutInfo>>
      kMap([]() {
        absl::flat_hash_map<uint16_t, SpecialLayoutInfo> table;
        HKEY layouts_key = nullptr;
        if (RegOpenKeyExW(
                HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts", 0,
                KEY_READ, &layouts_key) != ERROR_SUCCESS ||
            layouts_key == nullptr) {
          return table;
        }

        wchar_t subkey_name[32] = {};
        for (DWORD index = 0;; ++index) {
          DWORD name_len = static_cast<DWORD>(std::size(subkey_name));
          if (RegEnumKeyExW(layouts_key, index, subkey_name, &name_len, nullptr,
                            nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
          }
          wchar_t* end_ptr = nullptr;
          const unsigned long klid_val = wcstoul(subkey_name, &end_ptr, 16);
          if (end_ptr == subkey_name) {
            continue;
          }

          HKEY subkey = nullptr;
          if (RegOpenKeyExW(layouts_key, subkey_name, 0, KEY_READ, &subkey) !=
                  ERROR_SUCCESS ||
              subkey == nullptr) {
            continue;
          }

          wchar_t layout_id_buf[16] = {};
          DWORD id_bytes = sizeof(layout_id_buf);
          DWORD type = 0;
          if (RegQueryValueExW(subkey, L"Layout Id", nullptr, &type,
                               reinterpret_cast<LPBYTE>(layout_id_buf),
                               &id_bytes) == ERROR_SUCCESS &&
              (type == REG_SZ || type == REG_EXPAND_SZ)) {
            wchar_t* id_end = nullptr;
            const unsigned long special_id =
                wcstoul(layout_id_buf, &id_end, 16);
            if (id_end != layout_id_buf) {
              SpecialLayoutInfo info;
              info.lang_id = static_cast<LANGID>(klid_val & 0xFFFFu);

              wchar_t text_buf[128] = {};
              DWORD text_bytes = sizeof(text_buf);
              DWORD text_type = 0;
              if (RegQueryValueExW(subkey, L"Layout Text", nullptr, &text_type,
                                   reinterpret_cast<LPBYTE>(text_buf),
                                   &text_bytes) == ERROR_SUCCESS &&
                  (text_type == REG_SZ || text_type == REG_EXPAND_SZ)) {
                info.layout_text.assign(text_buf);
              }
              table.try_emplace(static_cast<uint16_t>(special_id & 0x0FFFu),
                                std::move(info));
            }
          }
          RegCloseKey(subkey);
        }
        RegCloseKey(layouts_key);
        return table;
      }());
  return *kMap;
}

HKL GetActiveKeyboardLayout() noexcept {
  HWND fg = GetForegroundWindow();
  const DWORD thread_id =
      fg != nullptr ? GetWindowThreadProcessId(fg, nullptr) : 0;
  HKL hkl = GetKeyboardLayout(thread_id);
  if (hkl == nullptr) hkl = GetKeyboardLayout(0);

  return hkl;
}

LANGID ResolveLayoutLangId(HKL hkl, const std::wstring** out_special_text) {
  if (out_special_text != nullptr) *out_special_text = nullptr;

  const uintptr_t raw = reinterpret_cast<uintptr_t>(hkl);
  const LANGID input_lang = LOWORD(raw);
  const uint16_t device_id = HIWORD(raw);
  if (device_id == 0) return input_lang;

  if ((device_id & 0xF000u) != 0xF000u) return static_cast<LANGID>(device_id);

  const absl::flat_hash_map<uint16_t, SpecialLayoutInfo>& special_map =
      GetSpecialLayoutMap();
  const auto it = special_map.find(static_cast<uint16_t>(device_id & 0x0FFFu));
  if (it != special_map.end()) {
    if (out_special_text != nullptr && !it->second.layout_text.empty()) {
      *out_special_text = &it->second.layout_text;
    }
    return it->second.lang_id;
  }
  return input_lang;
}

bool IsEnglishKeyboardLayout(HKL hkl) noexcept {
  if (hkl == nullptr) return true;

  const LANGID input_lang = LOWORD(reinterpret_cast<uintptr_t>(hkl));
  if (PRIMARYLANGID(input_lang) != LANG_ENGLISH) return false;

  const LANGID layout_lang = ResolveLayoutLangId(hkl, nullptr);
  return PRIMARYLANGID(layout_lang) == LANG_ENGLISH;
}

std::wstring GetKeyboardLayoutDisplayName(HKL hkl, AppLanguage language) {
  if (hkl == nullptr) {
    return std::wstring(Localized(language, L10nKey::kInputSourceUnknown));
  }
  const std::wstring* special_text = nullptr;
  const LANGID input_lang = LOWORD(reinterpret_cast<uintptr_t>(hkl));
  const LANGID layout_lang = ResolveLayoutLangId(hkl, &special_text);
  if (special_text != nullptr && PRIMARYLANGID(layout_lang) != LANG_ENGLISH) {
    return *special_text;
  }

  const LANGID display_lang =
      PRIMARYLANGID(input_lang) != LANG_ENGLISH ? input_lang : layout_lang;
  const LCID lcid = MAKELCID(display_lang, SORT_DEFAULT);
  wchar_t locale_name[LOCALE_NAME_MAX_LENGTH] = {};
  if (LCIDToLocaleName(lcid, locale_name, LOCALE_NAME_MAX_LENGTH, 0) > 0) {
    wchar_t display_name[128] = {};
    if (GetLocaleInfoEx(locale_name, LOCALE_SLOCALIZEDDISPLAYNAME, display_name,
                        128) > 0) {
      return std::wstring(display_name);
    }
  }
  return std::wstring(Localized(language, L10nKey::kInputSourceUnknown));
}

}  // namespace

InputSourceManager::InputSourceManager() { Refresh(AppLanguage::kSystem); }

bool InputSourceManager::IsForegroundKeyboardEnglish() noexcept {
  return IsEnglishKeyboardLayout(GetActiveKeyboardLayout());
}

bool InputSourceManager::Refresh(AppLanguage language) {
  const HKL hkl = GetActiveKeyboardLayout();
  const bool next_english = IsEnglishKeyboardLayout(hkl);
  const std::wstring next_name = GetKeyboardLayoutDisplayName(hkl, language);

  if (next_english == is_english_ && next_name == current_source_name_) {
    return false;
  }

  is_english_ = next_english;
  current_source_name_ = next_name;
  if (on_changed_) on_changed_();

  return true;
}

bool InputSourceManager::SwitchToEnglishKeyboard(AppLanguage language) {
  HKL us_hkl = LoadKeyboardLayoutW(L"00000409", KLF_ACTIVATE);
  if (us_hkl == nullptr) return false;

  ActivateKeyboardLayout(us_hkl, KLF_SETFORPROCESS);
  if (HWND fg = GetForegroundWindow(); fg != nullptr) {
    PostMessageW(fg, WM_INPUTLANGCHANGEREQUEST, 0,
                 reinterpret_cast<LPARAM>(us_hkl));
  }
  is_english_ = true;
  current_source_name_ = GetKeyboardLayoutDisplayName(us_hkl, language);
  if (on_changed_) on_changed_();

  return true;
}

void InputSourceManager::CheckAndWarnOnStartup(AppLanguage language) {
  Refresh(language);
  if (is_english_) return;

  const std::wstring_view title =
      Localized(language, L10nKey::kAlertNonEnglishKeyboardTitle);
  const std::wstring message = LocalizedFormat(
      language, L10nKey::kAlertNonEnglishKeyboardMessage, current_source_name_);
  const std::wstring_view btn_switch =
      Localized(language, L10nKey::kActionSwitchToUs);
  const std::wstring_view btn_settings = Localized(
      language, L10nKey::kAlertNonEnglishKeyboardOpenKeyboardSettings);
  const std::wstring_view btn_ignore =
      Localized(language, L10nKey::kAlertNonEnglishKeyboardIgnore);

  constexpr int kBtnSwitchId = 1001;
  constexpr int kBtnSettingsId = 1002;
  constexpr int kBtnIgnoreId = 1003;

  const TASKDIALOG_BUTTON buttons[] = {
      {kBtnSwitchId, btn_switch.data()},
      {kBtnSettingsId, btn_settings.data()},
      {kBtnIgnoreId, btn_ignore.data()},
  };

  TASKDIALOGCONFIG cfg{};
  cfg.cbSize = sizeof(cfg);
  cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
  cfg.pszWindowTitle = L"ÔKêyTre";
  cfg.pszMainIcon = TD_WARNING_ICON;
  cfg.pszMainInstruction = title.data();
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
