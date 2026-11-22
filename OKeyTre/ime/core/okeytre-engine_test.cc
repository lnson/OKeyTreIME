// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "okeytre-engine.h"

#include <cstdint>

#include "gtest/gtest.h"
#include "input-method-buffer.h"

namespace okeytre {
namespace {

static_assert(sizeof(TelexAutomaton) <= 4);
static_assert(sizeof(VNIAutomaton) <= 4);
static_assert(sizeof(VIQRAutomaton) <= 4);

TEST(OkeytreEngineTest, GeneratedAutomataDefaultsAndRewrite) {
  TelexAutomaton telex;
  EXPECT_FALSE(telex.modernTone);
  EXPECT_FALSE(telex.uoHorn);
  EXPECT_FALSE(telex.ngToNgh);
  EXPECT_FALSE(telex.nghToNg);

  VNIAutomaton vni;
  EXPECT_FALSE(vni.modernTone);
  EXPECT_FALSE(vni.uoHorn);
  EXPECT_FALSE(vni.ngToNgh);
  EXPECT_FALSE(vni.nghToNg);

  VIQRAutomaton viqr;
  EXPECT_FALSE(viqr.modernTone);
  EXPECT_FALSE(viqr.uoHorn);
  EXPECT_FALSE(viqr.ngToNgh);
  EXPECT_FALSE(viqr.nghToNg);

  // "dd" -> "đ" (0x0111)
  const uint16_t word_d[] = {'d'};
  uint16_t out[16] = {};
  int32_t len = telex.RewriteToBuffer(word_d, 1, 'd', out, 16);
  ASSERT_EQ(len, 1);
  EXPECT_EQ(out[0], 0x0111);
}

TEST(OkeytreEngineTest, CAbiStatefulBufferFeedsKeysAndSynthesizesLcpActions) {
  OKTInputMethodBuffer* buf =
      okeytre_buffer_create(OKT_STYLE_TELEX, false, false, false, false, 16);
  ASSERT_NE(buf, nullptr);

  int32_t backspaces = 0;
  uint16_t text[32] = {};
  int32_t text_len = 0;

  // 'c' -> pass-through
  const uint16_t key_c = 'c';
  EXPECT_FALSE(okeytre_buffer_handle_input(buf, &key_c, 1, OKT_MOD_NONE,
                                           &backspaces, text, 32, &text_len));
  // 'o' -> pass-through
  const uint16_t key_o = 'o';
  EXPECT_FALSE(okeytre_buffer_handle_input(buf, &key_o, 1, OKT_MOD_NONE,
                                           &backspaces, text, 32, &text_len));
  // 's' -> replaces 'o' (1 backspace, not 2!) with 'ó' (0x00F3) via LCP
  const uint16_t key_s = 's';
  ASSERT_TRUE(okeytre_buffer_handle_input(buf, &key_s, 1, OKT_MOD_NONE,
                                          &backspaces, text, 32, &text_len));
  EXPECT_EQ(backspaces, 1);
  ASSERT_EQ(text_len, 1);
  EXPECT_EQ(text[0], 0x00F3);

  okeytre_buffer_destroy(buf);
}

}  // namespace
}  // namespace okeytre
