// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_IME_CONTROLLER_H_
#define OKEYTRE_WINDOWS_IME_CONTROLLER_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "okeytre-engine.h"
#include "windows/settings-store.h"

namespace okeytre::win {

struct SurroundingWord {
  std::vector<uint16_t> word_utf16;
  int32_t caret_offset = 0;
};

struct ImeEditAction {
  int32_t backspace_count = 0;
  std::u16string text;

  bool operator==(const ImeEditAction& other) const = default;
};

// Pure helper extracting the word surrounding `caret_location` in `text` using
// `okeytre_is_word_boundary`.
SurroundingWord ExtractSurroundingWord(const uint16_t* text, int32_t text_len,
                                       int32_t text_offset,
                                       int32_t caret_location);

// Coordinates `OKTInputMethodBuffer`, persisted `SettingsStore`, caret context
// seeding, and UI state change notifications.
class ImeController {
 public:
  using CaretReaderFn = std::function<std::optional<SurroundingWord>()>;
  using StateChangedFn = std::function<void()>;

  explicit ImeController(SettingsStore& settings,
                         CaretReaderFn caret_reader = nullptr);
  ~ImeController();

  ImeController(const ImeController&) = delete;
  ImeController& operator=(const ImeController&) = delete;

  void SetOnStateChanged(StateChangedFn callback) {
    on_state_changed_ = std::move(callback);
  }

  InputStyle input_style() const noexcept { return input_style_; }
  bool is_vietnamese_enabled() const noexcept {
    return input_style_ != InputStyle::kOff;
  }
  bool is_system_keyboard_compatible() const noexcept {
    return system_keyboard_compatible_;
  }
  bool is_effectively_active() const noexcept {
    return is_vietnamese_enabled() && system_keyboard_compatible_;
  }
  InputStyle last_active_style() const noexcept { return last_active_style_; }

  void SetInputStyle(InputStyle style);
  void ToggleVietnameseEnabled();
  void SetSystemKeyboardCompatible(bool compatible) noexcept;

  bool modern_tone() const noexcept;
  void SetModernTone(bool enabled);

  bool uo_horn() const noexcept;
  void SetUoHorn(bool enabled);

  bool ng_to_ngh() const noexcept;
  void SetNgToNgh(bool enabled);

  bool ngh_to_ng() const noexcept;
  void SetNghToNg(bool enabled);

  bool restore_invalid_words() const noexcept;
  void SetRestoreInvalidWords(bool enabled);

  int32_t max_buffer_size() const noexcept;
  void SetMaxBufferSize(int32_t size);

  void ResetBuffer() noexcept;

  std::optional<ImeEditAction> HandleInput(const uint16_t* input,
                                           int32_t input_len,
                                           uint32_t modifiers = OKT_MOD_NONE);
  std::optional<ImeEditAction> RestoreCurrentWord();

 private:
  static OKTInputMethodStyle ToCStyle(InputStyle style) noexcept;
  void SeedCaretIfNeeded();
  void NotifyStateChanged();

  SettingsStore& settings_;
  CaretReaderFn caret_reader_;
  StateChangedFn on_state_changed_;
  OKTInputMethodBuffer* raw_buffer_ = nullptr;
  InputStyle input_style_ = InputStyle::kTelex;
  InputStyle last_active_style_ = InputStyle::kTelex;
  bool system_keyboard_compatible_ = true;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_IME_CONTROLLER_H_
