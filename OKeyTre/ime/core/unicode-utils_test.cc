// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "unicode-utils.h"

#include "gtest/gtest.h"

namespace okeytre {
namespace {

TEST(UnicodeUtilsTest, AsciiCaseConversions) {
  EXPECT_TRUE(IsUpper('A'));
  EXPECT_TRUE(IsUpper('Z'));
  EXPECT_FALSE(IsUpper('a'));
  EXPECT_FALSE(IsUpper('z'));
  EXPECT_FALSE(IsUpper('0'));

  EXPECT_EQ(ToLower('A'), 'a');
  EXPECT_EQ(ToLower('Z'), 'z');
  EXPECT_EQ(ToLower('a'), 'a');
  EXPECT_EQ(ToUpper('a'), 'A');
  EXPECT_EQ(ToUpper('z'), 'Z');
  EXPECT_EQ(ToUpper('A'), 'A');
}

TEST(UnicodeUtilsTest, VietnameseAndLatinCaseConversions) {
  // Đ (0x0110) / đ (0x0111)
  EXPECT_TRUE(IsUpper(0x0110));
  EXPECT_FALSE(IsUpper(0x0111));
  EXPECT_EQ(ToLower(0x0110), 0x0111);
  EXPECT_EQ(ToUpper(0x0111), 0x0110);

  // Ơ (0x01A0) / ơ (0x01A1), Ư (0x01AF) / ư (0x01B0)
  EXPECT_TRUE(IsUpper(0x01A0));
  EXPECT_FALSE(IsUpper(0x01A1));
  EXPECT_EQ(ToLower(0x01A0), 0x01A1);
  EXPECT_EQ(ToUpper(0x01A1), 0x01A0);

  EXPECT_TRUE(IsUpper(0x01AF));
  EXPECT_FALSE(IsUpper(0x01B0));
  EXPECT_EQ(ToLower(0x01AF), 0x01B0);
  EXPECT_EQ(ToUpper(0x01B0), 0x01AF);

  // Ệ (0x1EC6) / ệ (0x1EC7)
  EXPECT_TRUE(IsUpper(0x1EC6));
  EXPECT_FALSE(IsUpper(0x1EC7));
  EXPECT_EQ(ToLower(0x1EC6), 0x1EC7);
  EXPECT_EQ(ToUpper(0x1EC7), 0x1EC6);

  // All Vietnamese Extended Additional pairs (U+1EA0..U+1EF9)
  for (uint16_t u = 0x1EA0; u <= 0x1EF8; u += 2) {
    const uint16_t l = static_cast<uint16_t>(u + 1);
    EXPECT_TRUE(IsUpper(u));
    EXPECT_FALSE(IsUpper(l));
    EXPECT_EQ(ToLower(u), l);
    EXPECT_EQ(ToUpper(l), u);
  }
}

TEST(UnicodeUtilsTest, WordBoundaries) {
  EXPECT_TRUE(IsWordBoundary(' '));
  EXPECT_TRUE(IsWordBoundary('\t'));
  EXPECT_TRUE(IsWordBoundary('\n'));
  EXPECT_TRUE(IsWordBoundary('.'));
  EXPECT_TRUE(IsWordBoundary(','));
  EXPECT_TRUE(IsWordBoundary(0x00A0));  // NBSP
  EXPECT_TRUE(IsWordBoundary(0x2014));  // —
  EXPECT_TRUE(IsWordBoundary(0x201C));  // “
  EXPECT_TRUE(IsWordBoundary(0x3000));  // Ideographic space
  EXPECT_FALSE(IsWordBoundary('a'));
  EXPECT_FALSE(IsWordBoundary(0x0111));  // đ
  EXPECT_FALSE(IsWordBoundary('+'));
  EXPECT_FALSE(IsWordBoundary('^'));
  EXPECT_FALSE(IsWordBoundary('~'));
  EXPECT_FALSE(IsWordBoundary('`'));
}

}  // namespace
}  // namespace okeytre
