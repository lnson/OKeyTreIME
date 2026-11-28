// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/shortcut-detectors.h"

namespace okeytre::win {

void DualShiftDetector::Reset() noexcept {
  is_left_shift_pressed_ = false;
  is_right_shift_pressed_ = false;
  has_triggered_for_chord_ = false;
}

bool DualShiftDetector::OnKeyEvent(uint32_t vk_code, bool is_key_down,
                                   const ModifierSnapshot& mods) noexcept {
  if (vk_code != kVkLeftShift && vk_code != kVkRightShift) return false;

  if (vk_code == kVkLeftShift) {
    is_left_shift_pressed_ = is_key_down;
  } else {
    is_right_shift_pressed_ = is_key_down;
  }

  if (!is_left_shift_pressed_ || !is_right_shift_pressed_) {
    has_triggered_for_chord_ = false;
    return false;
  }

  if (mods.control || mods.alt || mods.win) return false;
  if (has_triggered_for_chord_) return false;

  has_triggered_for_chord_ = true;
  return true;
}

void DualControlDetector::Reset() noexcept {
  is_left_ctrl_pressed_ = false;
  is_right_ctrl_pressed_ = false;
  is_disrupted_ = false;
  has_triggered_for_chord_ = false;
}

void DualControlDetector::NoteNonModifierKeyDown() noexcept {
  if (is_left_ctrl_pressed_ || is_right_ctrl_pressed_) is_disrupted_ = true;
}

bool DualControlDetector::OnKeyEvent(uint32_t vk_code, bool is_key_down,
                                     const ModifierSnapshot& mods) noexcept {
  if (vk_code != kVkLeftControl && vk_code != kVkRightControl) {
    if (is_key_down && (is_left_ctrl_pressed_ || is_right_ctrl_pressed_)) {
      is_disrupted_ = true;
    }
    return false;
  }

  if (mods.shift || mods.alt || mods.win) is_disrupted_ = true;

  if (vk_code == kVkLeftControl) {
    is_left_ctrl_pressed_ = is_key_down;
  } else {
    is_right_ctrl_pressed_ = is_key_down;
  }

  if (!is_left_ctrl_pressed_ && !is_right_ctrl_pressed_) {
    Reset();
    return false;
  }

  if (!is_left_ctrl_pressed_ || !is_right_ctrl_pressed_) {
    has_triggered_for_chord_ = false;
    return false;
  }

  if (is_disrupted_ || has_triggered_for_chord_) return false;

  has_triggered_for_chord_ = true;
  return true;
}

}  // namespace okeytre::win
