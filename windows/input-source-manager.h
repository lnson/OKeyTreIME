// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_INPUT_SOURCE_MANAGER_H_
#define OKEYTRE_WINDOWS_INPUT_SOURCE_MANAGER_H_

#include <functional>
#include <string>

#include "windows/localization.h"

namespace okeytre::win {

// Monitors the active Windows keyboard layout (HKL), detects non-English
// layouts that may conflict with Vietnamese typing, and provides one-click
// switching to English (U.S.).
class InputSourceManager {
 public:
  using ChangedFn = std::function<void()>;

  InputSourceManager();

  void SetOnChanged(ChangedFn callback) { on_changed_ = std::move(callback); }

  bool is_english() const noexcept { return is_english_; }
  const std::wstring& current_source_name() const noexcept {
    return current_source_name_;
  }

  // Fast, allocation-free check of the active foreground thread's keyboard
  // layout (`HKL`), catching both non-English input languages (e.g. `vi-VN`)
  // and non-English keyboard layouts/IMEs added under an English language pack
  // (e.g. Windows built-in Vietnamese Telex `0001042a` under `en-US`).
  static bool IsForegroundKeyboardEnglish() noexcept;

  // Re-queries the foreground window's keyboard layout. Returns true if the
  // English state or layout name changed.
  bool Refresh(AppLanguage language);

  // Attempts to switch the foreground window (and current thread) to the
  // English (United States) QWERTY keyboard layout ("00000409").
  bool SwitchToEnglishKeyboard(AppLanguage language);

  // Displays a non-English keyboard warning dialog on startup if the active
  // keyboard layout is not English.
  void CheckAndWarnOnStartup(AppLanguage language);

 private:
  bool is_english_ = true;
  std::wstring current_source_name_ = L"English (United States)";
  ChangedFn on_changed_;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_INPUT_SOURCE_MANAGER_H_
