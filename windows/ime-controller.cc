// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/ime-controller.h"

#include <algorithm>
#include <array>

namespace okeytre::win {

bool IsCaretWordBoundary(uint16_t ch) noexcept {
  return ch < 0x0020 || ch == 0x200B || ch == 0xFEFF || ch == 0xFFFC ||
         okeytre_is_word_boundary(ch);
}

SurroundingWord ExtractSurroundingWord(const uint16_t* text, int32_t text_len,
                                       int32_t text_offset,
                                       int32_t caret_location) {
  SurroundingWord result;
  if (text == nullptr || text_len <= 0) {
    return result;
  }
  const int32_t clamped_caret =
      std::clamp(caret_location - text_offset, 0, text_len);

  int32_t start = clamped_caret;
  while (start > 0 && !IsCaretWordBoundary(text[start - 1])) {
    --start;
  }

  int32_t end = clamped_caret;
  while (end < text_len && !IsCaretWordBoundary(text[end])) {
    ++end;
  }

  result.word_utf16.assign(text + start, text + end);
  result.caret_offset = clamped_caret - start;
  return result;
}

OKTInputMethodStyle ImeController::ToCStyle(InputStyle style) noexcept {
  switch (style) {
    case InputStyle::kTelex:
    case InputStyle::kOff:
      return OKT_STYLE_TELEX;
    case InputStyle::kVNI:
      return OKT_STYLE_VNI;
    case InputStyle::kVIQR:
      return OKT_STYLE_VIQR;
  }
  return OKT_STYLE_TELEX;
}

ImeController::ImeController(SettingsStore& settings,
                             CaretReaderFn caret_reader)
    : settings_(settings),
      caret_reader_(std::move(caret_reader)),
      input_style_(settings.GetInputStyle()),
      last_active_style_(settings.GetLastActiveInputStyle()) {
  raw_buffer_ = okeytre_buffer_create(
      ToCStyle(input_style_), settings_.GetModernTone(), settings_.GetUoHorn(),
      settings_.GetNgToNgh(), settings_.GetNghToNg(),
      settings_.GetMaxBufferSize());
  okeytre_buffer_set_restore_invalid_words(raw_buffer_,
                                           settings_.GetRestoreInvalidWords());
}

ImeController::~ImeController() {
  if (raw_buffer_ != nullptr) {
    okeytre_buffer_destroy(raw_buffer_);
    raw_buffer_ = nullptr;
  }
}

void ImeController::SetInputStyle(InputStyle style) {
  input_style_ = style;
  settings_.SetInputStyle(style);
  if (style == InputStyle::kOff) {
    okeytre_buffer_reset(raw_buffer_);
  } else {
    last_active_style_ = style;
    settings_.SetLastActiveInputStyle(style);
    okeytre_buffer_set_style(raw_buffer_, ToCStyle(style));
  }
  NotifyStateChanged();
}

void ImeController::ToggleVietnameseEnabled() {
  if (input_style_ == InputStyle::kOff) {
    SetInputStyle(last_active_style_);
  } else {
    SetInputStyle(InputStyle::kOff);
  }
}

void ImeController::SetSystemKeyboardCompatible(bool compatible) noexcept {
  if (system_keyboard_compatible_ == compatible) {
    return;
  }
  system_keyboard_compatible_ = compatible;
  if (raw_buffer_ != nullptr) {
    okeytre_buffer_reset(raw_buffer_);
  }
}

bool ImeController::modern_tone() const noexcept {
  return okeytre_buffer_get_modern_tone(raw_buffer_);
}

void ImeController::SetModernTone(bool enabled) {
  okeytre_buffer_set_modern_tone(raw_buffer_, enabled);
  settings_.SetModernTone(enabled);
  NotifyStateChanged();
}

bool ImeController::uo_horn() const noexcept {
  return okeytre_buffer_get_uo_horn(raw_buffer_);
}

void ImeController::SetUoHorn(bool enabled) {
  okeytre_buffer_set_uo_horn(raw_buffer_, enabled);
  settings_.SetUoHorn(enabled);
  NotifyStateChanged();
}

bool ImeController::ng_to_ngh() const noexcept {
  return okeytre_buffer_get_ng_to_ngh(raw_buffer_);
}

void ImeController::SetNgToNgh(bool enabled) {
  okeytre_buffer_set_ng_to_ngh(raw_buffer_, enabled);
  settings_.SetNgToNgh(enabled);
  NotifyStateChanged();
}

bool ImeController::ngh_to_ng() const noexcept {
  return okeytre_buffer_get_ngh_to_ng(raw_buffer_);
}

void ImeController::SetNghToNg(bool enabled) {
  okeytre_buffer_set_ngh_to_ng(raw_buffer_, enabled);
  settings_.SetNghToNg(enabled);
  NotifyStateChanged();
}

bool ImeController::restore_invalid_words() const noexcept {
  return okeytre_buffer_get_restore_invalid_words(raw_buffer_);
}

void ImeController::SetRestoreInvalidWords(bool enabled) {
  okeytre_buffer_set_restore_invalid_words(raw_buffer_, enabled);
  settings_.SetRestoreInvalidWords(enabled);
  NotifyStateChanged();
}

int32_t ImeController::max_buffer_size() const noexcept {
  return okeytre_buffer_get_max_size(raw_buffer_);
}

void ImeController::SetMaxBufferSize(int32_t size) {
  const int32_t clamped =
      std::clamp(size, OKT_BUFFER_MIN_SIZE, OKT_BUFFER_MAX_SIZE);
  okeytre_buffer_set_max_size(raw_buffer_, clamped);
  settings_.SetMaxBufferSize(clamped);
  NotifyStateChanged();
}

void ImeController::ResetBuffer() noexcept { okeytre_buffer_reset(raw_buffer_); }

void ImeController::SeedCaretIfNeeded() {
  if (!caret_reader_) {
    return;
  }
  if (!okeytre_buffer_consume_caret_read_request(raw_buffer_)) {
    return;
  }
  const auto surrounding = caret_reader_();
  if (!surrounding.has_value() || surrounding->word_utf16.empty() ||
      surrounding->caret_offset <= 0) {
    return;
  }
  okeytre_buffer_seed_with_surrounding_text(
      raw_buffer_, surrounding->word_utf16.data(),
      static_cast<int32_t>(surrounding->word_utf16.size()),
      surrounding->caret_offset);
}

namespace {

bool ShouldSkipCaretSeedForInput(const uint16_t* input, int32_t input_len,
                                 uint32_t modifiers) noexcept {
  if (input == nullptr || input_len <= 0) {
    return true;
  }
  if (input_len == 1) {
    const uint16_t ch = input[0];
    switch (ch) {
      case 0x0009:  // Tab
      case 0x000A:  // LF
      case 0x000D:  // CR
      case 0x001B:  // Escape
      case 0xF700:  // Up
      case 0xF701:  // Down
      case 0xF729:  // Home
      case 0xF72B:  // End
      case 0xF72C:  // PageUp
      case 0xF72D:  // PageDown
        return true;
      case 0xF702:  // Left
      case 0xF703:  // Right
        return (modifiers & OKT_MOD_SHIFT) != 0;
      default:
        break;
    }
  }
  return false;
}

}  // namespace

std::optional<ImeEditAction> ImeController::HandleInput(const uint16_t* input,
                                                        int32_t input_len,
                                                        uint32_t modifiers) {
  if (!is_effectively_active()) {
    if (raw_buffer_ != nullptr) {
      okeytre_buffer_reset(raw_buffer_);
    }
    return std::nullopt;
  }

  constexpr uint32_t kDisrupting =
      OKT_MOD_CONTROL | OKT_MOD_OPTION | OKT_MOD_COMMAND;
  if ((modifiers & kDisrupting) == 0 &&
      !ShouldSkipCaretSeedForInput(input, input_len, modifiers)) {
    SeedCaretIfNeeded();
  }

  std::array<uint16_t, 1024> out_buf{};
  int32_t backspace_count = 0;
  int32_t out_len = 0;
  if (!okeytre_buffer_handle_input(
          raw_buffer_, input, input_len, modifiers, &backspace_count,
          out_buf.data(), static_cast<int32_t>(out_buf.size()), &out_len)) {
    return std::nullopt;
  }

  ImeEditAction action;
  action.backspace_count = backspace_count;
  if (out_len > 0) {
    action.text.assign(
        reinterpret_cast<const char16_t*>(out_buf.data()),
        static_cast<size_t>(
            std::min(out_len, static_cast<int32_t>(out_buf.size()))));
  }
  return action;
}

std::optional<ImeEditAction> ImeController::RestoreCurrentWord() {
  if (!is_effectively_active()) {
    if (raw_buffer_ != nullptr) {
      okeytre_buffer_reset(raw_buffer_);
    }
    return std::nullopt;
  }

  std::array<uint16_t, 1024> out_buf{};
  int32_t backspace_count = 0;
  int32_t out_len = 0;
  if (!okeytre_buffer_restore_current_word(
          raw_buffer_, &backspace_count, out_buf.data(),
          static_cast<int32_t>(out_buf.size()), &out_len)) {
    return std::nullopt;
  }

  ImeEditAction action;
  action.backspace_count = backspace_count;
  if (out_len > 0) {
    action.text.assign(
        reinterpret_cast<const char16_t*>(out_buf.data()),
        static_cast<size_t>(
            std::min(out_len, static_cast<int32_t>(out_buf.size()))));
  }
  return action;
}

void ImeController::NotifyStateChanged() {
  if (on_state_changed_) {
    on_state_changed_();
  }
}

}  // namespace okeytre::win
