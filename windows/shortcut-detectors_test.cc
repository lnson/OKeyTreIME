// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/shortcut-detectors.h"

#include "gtest/gtest.h"

namespace okeytre::win {
namespace {

TEST(DualShiftDetectorTest, TriggersOncePerCleanDualShiftChord) {
  DualShiftDetector detector;
  ModifierSnapshot shift_only{.shift = true};

  EXPECT_FALSE(detector.OnKeyEvent(kVkLeftShift, true, shift_only));
  EXPECT_TRUE(detector.OnKeyEvent(kVkRightShift, true, shift_only));
  // Auto-repeat while held must not re-trigger.
  EXPECT_FALSE(detector.OnKeyEvent(kVkRightShift, true, shift_only));
  EXPECT_FALSE(detector.OnKeyEvent(kVkRightShift, false, shift_only));
  // Pressing Right Shift again while Left Shift is held triggers again.
  EXPECT_TRUE(detector.OnKeyEvent(kVkRightShift, true, shift_only));
}

TEST(DualShiftDetectorTest, IgnoresWhenDisruptingModifierHeld) {
  DualShiftDetector detector;
  ModifierSnapshot ctrl_shift{.shift = true, .control = true};

  EXPECT_FALSE(detector.OnKeyEvent(kVkLeftShift, true, ctrl_shift));
  EXPECT_FALSE(detector.OnKeyEvent(kVkRightShift, true, ctrl_shift));
}

TEST(DualControlDetectorTest, TriggersOncePerCleanDualCtrlChord) {
  DualControlDetector detector;
  ModifierSnapshot ctrl_only{.control = true};

  EXPECT_FALSE(detector.OnKeyEvent(kVkLeftControl, true, ctrl_only));
  EXPECT_TRUE(detector.OnKeyEvent(kVkRightControl, true, ctrl_only));
  // Auto-repeat does not double-toggle.
  EXPECT_FALSE(detector.OnKeyEvent(kVkRightControl, true, ctrl_only));
  EXPECT_FALSE(detector.OnKeyEvent(kVkRightControl, false, ctrl_only));
  EXPECT_FALSE(detector.OnKeyEvent(kVkLeftControl, false, ModifierSnapshot{}));

  // Second clean chord works.
  EXPECT_FALSE(detector.OnKeyEvent(kVkRightControl, true, ctrl_only));
  EXPECT_TRUE(detector.OnKeyEvent(kVkLeftControl, true, ctrl_only));
}

TEST(DualControlDetectorTest, DisruptedByOtherKeyOrModifier) {
  DualControlDetector detector;
  ModifierSnapshot ctrl_only{.control = true};

  EXPECT_FALSE(detector.OnKeyEvent(kVkLeftControl, true, ctrl_only));
  detector.NoteNonModifierKeyDown();  // e.g. Ctrl+C
  EXPECT_FALSE(detector.OnKeyEvent(kVkRightControl, true, ctrl_only));

  // Releasing both resets disruption.
  EXPECT_FALSE(detector.OnKeyEvent(kVkRightControl, false, ctrl_only));
  EXPECT_FALSE(detector.OnKeyEvent(kVkLeftControl, false, ModifierSnapshot{}));
  EXPECT_FALSE(detector.is_disrupted());

  EXPECT_FALSE(detector.OnKeyEvent(kVkLeftControl, true, ctrl_only));
  EXPECT_TRUE(detector.OnKeyEvent(kVkRightControl, true, ctrl_only));
}

}  // namespace
}  // namespace okeytre::win
