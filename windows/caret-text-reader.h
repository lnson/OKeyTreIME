// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_CARET_TEXT_READER_H_
#define OKEYTRE_WINDOWS_CARET_TEXT_READER_H_

#include <cstdint>
#include <optional>

#include "windows/ime-controller.h"

struct IUIAutomation;

namespace okeytre::win {

// Reads surrounding word text and selection state around the focused caret on
// Windows via UI Automation (IUIAutomationTextPattern2 / TextPattern), with a
// fast fallback to Win32 EM_GETSEL / WM_GETTEXT for classic Edit controls.
class CaretTextReader {
 public:
  CaretTextReader();
  ~CaretTextReader();

  CaretTextReader(const CaretTextReader&) = delete;
  CaretTextReader& operator=(const CaretTextReader&) = delete;

  // Returns the word surrounding the focused caret and the caret offset within
  // that word, or std::nullopt if unavailable or unsupported.
  std::optional<SurroundingWord> GetSurroundingText();

  // Returns the length of any non-empty text selection in the focused control
  // (e.g. browser address bar inline autocomplete), or 0 if no selection.
  int32_t GetSelectedRangeLength();

 private:
  void EnsureAutomation();
  std::optional<SurroundingWord> ReadViaUIAutomation();
  std::optional<SurroundingWord> ReadViaWin32Edit();
  int32_t SelectionLengthViaUIAutomation();
  int32_t SelectionLengthViaWin32Edit();

  IUIAutomation* automation_ = nullptr;
  bool automation_init_attempted_ = false;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_CARET_TEXT_READER_H_
