// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "input-method-buffer.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "unicode-utils.h"

namespace okeytre {
namespace {

std::u16string SimulateTyping(InputMethodBuffer& ime,
                              std::u16string_view keys) {
  std::u16string screen;
  for (char16_t raw_ch : keys) {
    const uint16_t ch = static_cast<uint16_t>(raw_ch);
    TextEditAction action = ime.HandleInput(&ch, 1);
    if (!action.IsNoOp()) {
      if (action.backspace_count > 0) {
        screen.erase(screen.size() -
                     static_cast<size_t>(action.backspace_count));
      }
      screen.append(action.text_to_insert);
    } else if (ch == kBackspace || ch == kBackspaceCtrl) {
      if (!screen.empty()) {
        screen.pop_back();
      }
    } else {
      screen.push_back(raw_ch);
    }
  }
  return screen;
}

TEST(InputMethodBufferTest,
     WordBoundaryClassificationMatchesPunctuationAndSpace) {
  EXPECT_TRUE(IsWordBoundary(' '));
  EXPECT_TRUE(IsWordBoundary('\n'));
  EXPECT_TRUE(IsWordBoundary('\t'));
  EXPECT_TRUE(IsWordBoundary('.'));
  EXPECT_TRUE(IsWordBoundary(','));
  EXPECT_TRUE(IsWordBoundary('('));
  EXPECT_TRUE(IsWordBoundary('*'));
  EXPECT_TRUE(IsWordBoundary('\''));
  EXPECT_TRUE(IsWordBoundary('?'));

  // Letters and VIQR non-punctuation symbols (+, ^, ~, `)
  EXPECT_FALSE(IsWordBoundary('a'));
  EXPECT_FALSE(IsWordBoundary(0x0111));  // đ
  EXPECT_FALSE(IsWordBoundary('+'));
  EXPECT_FALSE(IsWordBoundary('^'));
  EXPECT_FALSE(IsWordBoundary('~'));
  EXPECT_FALSE(IsWordBoundary('`'));
}

TEST(InputMethodBufferTest, SequentialTypingAcrossStylesAndFlags) {
  InputMethodBuffer ime(InputMethodStyle::kTelex, /*modern_tone=*/false,
                        /*uo_horn=*/true, /*ng_to_ngh=*/true,
                        /*ngh_to_ng=*/true);
  EXPECT_EQ(SimulateTyping(ime, u"tieengs vieetj"), u"tiếng việt");

  ime.Reset();
  EXPECT_EQ(SimulateTyping(ime, u"hoaf khoer thuys hoafn"),
            u"hòa khỏe thúy hoàn");

  ime.SetModernTone(true);
  ime.Reset();
  EXPECT_EQ(SimulateTyping(ime, u"hoaf khoer thuys hoafn"),
            u"hoà khoẻ thuý hoàn");

  ime.Reset();
  EXPECT_EQ(SimulateTyping(ime, u"ngieengs nghas nuwocs"), u"nghiếng ngá nước");

  ime.SetStyle(InputMethodStyle::kVNI);
  EXPECT_TRUE(ime.modern_tone());
  EXPECT_EQ(SimulateTyping(ime, u"tieng61 viet65 hoa2"), u"tiếng việt hoà");

  ime.SetModernTone(false);
  ime.Reset();
  EXPECT_EQ(SimulateTyping(ime, u"hoa2 khoe3 thuy1 hoa2n"),
            u"hòa khỏe thúy hoàn");

  ime.SetStyle(InputMethodStyle::kVIQR);
  EXPECT_EQ(SimulateTyping(ime, u"tie^'ng vie^.t"), u"tiếng việt");
}

TEST(InputMethodBufferTest,
     RestoreCurrentWordReplacesWithRawKeystrokesAndSuppressesUntilBoundary) {
  InputMethodBuffer ime(InputMethodStyle::kTelex);
  std::u16string screen = SimulateTyping(ime, u"tieengs");
  ASSERT_EQ(screen, u"tiếng");

  TextEditAction restore = ime.RestoreCurrentWord();
  ASSERT_FALSE(restore.IsNoOp());
  screen.erase(screen.size() - static_cast<size_t>(restore.backspace_count));
  screen.append(restore.text_to_insert);
  EXPECT_EQ(screen, u"tieengs");

  // Subsequent 's' in the same word stays literal; next word after space
  // transforms.
  const uint16_t ch_s = 's';
  EXPECT_TRUE(ime.HandleInput(&ch_s, 1).IsNoOp());
  screen.push_back(u's');
  EXPECT_EQ(screen, u"tieengss");

  screen.append(SimulateTyping(ime, u" vieet"));
  EXPECT_EQ(screen, u"tieengss viêt");
}

TEST(InputMethodBufferTest,
     RestoreCurrentWordWorksAfterBackspacingSpaceOrChars) {
  InputMethodBuffer ime(InputMethodStyle::kTelex);

  // 1. Type word + space ("tieengs " -> "tiếng "), backspace space, then
  // restore -> "tieengs" and stays literal until next word boundary.
  std::u16string screen = SimulateTyping(ime, u"tieengs \x7F");
  ASSERT_EQ(screen, u"tiếng");

  TextEditAction restore = ime.RestoreCurrentWord();
  ASSERT_FALSE(restore.IsNoOp());
  screen.erase(screen.size() - static_cast<size_t>(restore.backspace_count));
  screen.append(restore.text_to_insert);
  EXPECT_EQ(screen, u"tieengs");

  screen.append(SimulateTyping(ime, u"s vieet"));
  EXPECT_EQ(screen, u"tieengss viêt");

  // 2. Backspace across next word + space back to previous word ("rồ va" ->
  // backspace 3 -> "rồ"), then restore -> "roof".
  ime.Reset();
  screen = SimulateTyping(ime, u"roof va\x7F\x7F\x7F");
  ASSERT_EQ(screen, u"rồ");
  restore = ime.RestoreCurrentWord();
  ASSERT_FALSE(restore.IsNoOp());
  screen.erase(screen.size() - static_cast<size_t>(restore.backspace_count));
  screen.append(restore.text_to_insert);
  EXPECT_EQ(screen, u"roof");

  // 3. Backspace inside a transformed word ("tiếng" -> backspace -> "tiến"),
  // then restore -> "tieens".
  ime.Reset();
  screen = SimulateTyping(ime, u"tieengs\x7F");
  ASSERT_EQ(screen, u"tiến");
  restore = ime.RestoreCurrentWord();
  ASSERT_FALSE(restore.IsNoOp());
  screen.erase(screen.size() - static_cast<size_t>(restore.backspace_count));
  screen.append(restore.text_to_insert);
  EXPECT_EQ(screen, u"tieens");
}

TEST(InputMethodBufferTest, BackspaceAcrossWordsRespectsMaxBufferSize) {
  InputMethodBuffer ime(InputMethodStyle::kTelex, false, false, false, false,
                        /*max_buffer_size=*/10);
  EXPECT_EQ(SimulateTyping(ime, u"co \x7Fs"), u"có");

  ime.Reset();
  EXPECT_EQ(
      SimulateTyping(
          ime, u"co 1234567890\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7Fs"),
      u"cos");
}

TEST(InputMethodBufferTest, MaxLength512CharWordToneUndoEmits513CharsSafely) {
  std::vector<uint16_t> word;
  word.reserve(InputMethodBuffer::kMaxAllowedBufferSize);
  word.push_back('n');
  word.push_back('g');
  word.push_back('h');
  for (int32_t i = 0; i < 250; ++i) {
    word.push_back('a');
  }
  word.push_back(0x1EBF);  // 'ế'
  for (int32_t i = 0; i < 256; ++i) {
    word.push_back('u');
  }
  word.push_back('n');
  word.push_back('g');
  ASSERT_EQ(static_cast<int32_t>(word.size()),
            InputMethodBuffer::kMaxAllowedBufferSize);

  InputMethodBuffer ime(InputMethodStyle::kTelex, /*modern_tone=*/true,
                        /*uo_horn=*/true, /*ng_to_ngh=*/true,
                        /*ngh_to_ng=*/true,
                        InputMethodBuffer::kMaxAllowedBufferSize);
  ime.SeedWithSurroundingText(word.data(), 512, 512);

  const uint16_t key_s = 's';
  const TextEditAction action = ime.HandleInput(&key_s, 1);
  ASSERT_EQ(action.backspace_count, 259);
  ASSERT_EQ(action.text_to_insert.size(), 260u);
  EXPECT_EQ(action.text_to_insert.front(), u'\u00EA');
  EXPECT_EQ(action.text_to_insert.back(), u's');
}

}  // namespace
}  // namespace okeytre
