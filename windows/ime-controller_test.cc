// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/ime-controller.h"

#include <memory>
#include <string>

#include "gtest/gtest.h"

namespace okeytre::win {
namespace {

std::u16string TypeString(ImeController& controller, std::u16string_view keys) {
  std::u16string screen;
  for (char16_t ch : keys) {
    const uint16_t unit = static_cast<uint16_t>(ch);
    auto action = controller.HandleInput(&unit, 1, OKT_MOD_NONE);
    if (action.has_value()) {
      for (int32_t i = 0; i < action->backspace_count && !screen.empty(); ++i) {
        screen.pop_back();
      }
      screen += action->text;
    } else {
      screen.push_back(ch);
    }
  }
  return screen;
}

TEST(ImeControllerTest, TelexTypingAndToggleOffOn) {
  SettingsStore store(std::make_unique<InMemorySettingsBackend>());
  ImeController controller(store);

  EXPECT_EQ(TypeString(controller, u"vieetj"), u"vi\u1ec7t");

  controller.ToggleVietnameseEnabled();
  EXPECT_EQ(controller.input_style(), InputStyle::kOff);
  EXPECT_EQ(TypeString(controller, u"vieetj"), u"vieetj");

  controller.ToggleVietnameseEnabled();
  EXPECT_EQ(controller.input_style(), InputStyle::kTelex);
  EXPECT_EQ(TypeString(controller, u"tieengs"), u"ti\u1ebfng");
}

TEST(ImeControllerTest, AutoRestoreAndManualRestore) {
  SettingsStore store(std::make_unique<InMemorySettingsBackend>());
  ImeController controller(store);

  // Auto-restore non-Vietnamese word on space.
  EXPECT_EQ(TypeString(controller, u"user "), u"user ");

  // Manual restore via RestoreCurrentWord().
  std::u16string screen = TypeString(controller, u"roof ");
  EXPECT_EQ(screen, u"r\u1ed3 ");
  auto restore = controller.RestoreCurrentWord();
  ASSERT_TRUE(restore.has_value());
  for (int32_t i = 0; i < restore->backspace_count && !screen.empty(); ++i) {
    screen.pop_back();
  }
  screen += restore->text;
  EXPECT_EQ(screen, u"roof ");
}

TEST(ImeControllerTest, ExtractsSurroundingWordAtCaret) {
  const std::u16string text = u"hello tieng world";
  const SurroundingWord word =
      ExtractSurroundingWord(reinterpret_cast<const uint16_t*>(text.data()),
                             static_cast<int32_t>(text.size()), 0, 11);
  ASSERT_EQ(word.word_utf16.size(), 5u);
  EXPECT_EQ(word.caret_offset, 5);
}

TEST(ImeControllerTest,
     SuspendsTransformsAndReversionWhenSystemKeyboardIncompatible) {
  SettingsStore store(std::make_unique<InMemorySettingsBackend>());
  ImeController controller(store);

  EXPECT_TRUE(controller.is_vietnamese_enabled());
  EXPECT_TRUE(controller.is_effectively_active());
  EXPECT_EQ(TypeString(controller, u"roof "), u"r\u1ed3 ");

  // Switching to a system IME / non-English layout resets the buffer and blocks
  // both keystroke transforms/reversions and manual Dual-Shift restoration.
  controller.SetSystemKeyboardCompatible(false);
  EXPECT_TRUE(controller.is_vietnamese_enabled());
  EXPECT_FALSE(controller.is_effectively_active());
  EXPECT_FALSE(controller.RestoreCurrentWord().has_value());

  for (char16_t ch : std::u16string_view(u"tieengs user, window ")) {
    const uint16_t unit = static_cast<uint16_t>(ch);
    EXPECT_FALSE(controller.HandleInput(&unit, 1, OKT_MOD_NONE).has_value());
  }

  // Switching back to an English keyboard layout resumes Telex automatically.
  controller.SetSystemKeyboardCompatible(true);
  EXPECT_TRUE(controller.is_effectively_active());
  EXPECT_EQ(TypeString(controller, u"vieetj "), u"vi\u1ec7t ");
}

}  // namespace
}  // namespace okeytre::win
