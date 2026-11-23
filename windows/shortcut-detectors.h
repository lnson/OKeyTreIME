// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_SHORTCUT_DETECTORS_H_
#define OKEYTRE_WINDOWS_SHORTCUT_DETECTORS_H_

#include <cstdint>

namespace okeytre::win {

// Windows virtual-key constants (duplicated cleanly so portable tests compile
// on any OS without pulling in <windows.h>).
inline constexpr uint32_t kVkLeftShift = 0xA0;
inline constexpr uint32_t kVkRightShift = 0xA1;
inline constexpr uint32_t kVkLeftControl = 0xA2;
inline constexpr uint32_t kVkRightControl = 0xA3;
inline constexpr uint32_t kVkLeftAlt = 0xA4;
inline constexpr uint32_t kVkRightAlt = 0xA5;
inline constexpr uint32_t kVkLeftWin = 0x5B;
inline constexpr uint32_t kVkRightWin = 0x5C;

struct ModifierSnapshot {
  bool shift = false;
  bool control = false;
  bool alt = false;
  bool win = false;
};

// Detects simultaneous Left Shift + Right Shift presses (with no Ctrl/Alt/Win
// held) to restore the active word to its raw typed keystrokes.
class DualShiftDetector {
 public:
  void Reset() noexcept;

  bool is_left_shift_pressed() const noexcept { return is_left_shift_pressed_; }
  bool is_right_shift_pressed() const noexcept {
    return is_right_shift_pressed_;
  }

  // Called on key-down or key-up for Shift keys. Returns true once when both
  // Left Shift and Right Shift become simultaneously held without disrupting
  // modifiers.
  bool OnKeyEvent(uint32_t vk_code, bool is_key_down,
                  const ModifierSnapshot& mods) noexcept;

 private:
  bool is_left_shift_pressed_ = false;
  bool is_right_shift_pressed_ = false;
  bool has_triggered_for_chord_ = false;
};

// Detects simultaneous Left Ctrl + Right Ctrl presses (with no other keys or
// modifiers pressed during the chord) to toggle Vietnamese input On/Off.
class DualControlDetector {
 public:
  void Reset() noexcept;

  bool is_left_ctrl_pressed() const noexcept { return is_left_ctrl_pressed_; }
  bool is_right_ctrl_pressed() const noexcept { return is_right_ctrl_pressed_; }
  bool is_disrupted() const noexcept { return is_disrupted_; }

  // Called whenever any non-Ctrl key is pressed down so combos like Ctrl+C
  // followed by the other Ctrl do not spuriously trigger the toggle.
  void NoteNonModifierKeyDown() noexcept;

  // Called on key-down or key-up for Ctrl or other modifier keys. Returns true
  // once per clean Left Ctrl + Right Ctrl chord.
  bool OnKeyEvent(uint32_t vk_code, bool is_key_down,
                  const ModifierSnapshot& mods) noexcept;

 private:
  bool is_left_ctrl_pressed_ = false;
  bool is_right_ctrl_pressed_ = false;
  bool is_disrupted_ = false;
  bool has_triggered_for_chord_ = false;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_SHORTCUT_DETECTORS_H_
