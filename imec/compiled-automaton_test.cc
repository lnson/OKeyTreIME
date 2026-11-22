// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "compiled-automaton.h"

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

namespace okeytre {
namespace {

TEST(CompiledAutomatonTest, ECBitSetLowAndHighWords) {
  ECBitSet bits;
  EXPECT_FALSE(bits.Contains(0));
  EXPECT_FALSE(bits.Contains(63));
  EXPECT_FALSE(bits.Contains(64));
  EXPECT_FALSE(bits.Contains(127));

  bits.Insert(0);
  bits.Insert(63);
  bits.Insert(64);
  bits.Insert(127);
  bits.Insert(128);  // Out of range, ignored.
  bits.Insert(-1);   // Out of range, ignored.

  EXPECT_TRUE(bits.Contains(0));
  EXPECT_TRUE(bits.Contains(63));
  EXPECT_TRUE(bits.Contains(64));
  EXPECT_TRUE(bits.Contains(127));
  EXPECT_FALSE(bits.Contains(1));
  EXPECT_FALSE(bits.Contains(65));
  EXPECT_FALSE(bits.Contains(-1));
  EXPECT_FALSE(bits.Contains(128));

  EXPECT_EQ(bits.LowWord(), (1ULL << 0) | (1ULL << 63));
  EXPECT_EQ(bits.HighWord(), (1ULL << 0) | (1ULL << 63));

  constexpr ECBitSet kFromWords((1ULL << 0) | (1ULL << 63),
                                (1ULL << 0) | (1ULL << 63));
  EXPECT_EQ(bits, kFromWords);
}

TEST(CompiledAutomatonTest, SliceOffsetResolution) {
  SliceOffset from_start{false, 2};
  EXPECT_EQ(from_start.Resolve(5), 2);

  SliceOffset from_end{true, -1};
  EXPECT_EQ(from_end.Resolve(5), 4);
}

TEST(CompiledAutomatonTest, RuleMatcherPikeVMSubmatchExtraction) {
  // Pattern: (a+)(b) where EC 1 = 'a', EC 2 = 'b'
  RuleMatcher matcher;
  matcher.num_tags = 4;
  ECBitSet ec_a;
  ec_a.Insert(1);
  ECBitSet ec_b;
  ec_b.Insert(2);

  matcher.instructions = {
      PikeVMInst::MakeSaveTag(0),  PikeVMInst::MakeCharClass(ec_a),
      PikeVMInst::MakeSplit(1, 3), PikeVMInst::MakeSaveTag(1),
      PikeVMInst::MakeSaveTag(2),  PikeVMInst::MakeCharClass(ec_b),
      PikeVMInst::MakeSaveTag(3),  PikeVMInst::MakeMatch(),
  };

  const uint16_t slice[] = {'a', 'a', 'b'};
  std::vector<int> tags;
  bool matched = matcher.MatchSliceWithClassifier(
      slice, 3,
      [](uint16_t c) -> int {
        if (c == 'a') return 1;
        if (c == 'b') return 2;
        return 0;
      },
      tags);
  ASSERT_TRUE(matched);
  ASSERT_EQ(tags.size(), 4u);
  EXPECT_EQ(tags[0], 0);
  EXPECT_EQ(tags[1], 2);
  EXPECT_EQ(tags[2], 2);
  EXPECT_EQ(tags[3], 3);
}

}  // namespace
}  // namespace okeytre
