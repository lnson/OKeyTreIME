// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/localization.h"

#include "gtest/gtest.h"

namespace okeytre::win {
namespace {

TEST(LocalizationTest, ResolvesPreferredLanguages) {
  EXPECT_EQ(ResolveLanguageCode(AppLanguage::kEnglish, {"vi-VN"}), "en");
  EXPECT_EQ(ResolveLanguageCode(AppLanguage::kVietnamese, {"en-US"}), "vi");
  EXPECT_EQ(ResolveLanguageCode(AppLanguage::kSystem, {"fr-FR", "en-US"}),
            "en");
  EXPECT_EQ(ResolveLanguageCode(AppLanguage::kSystem, {"ja-JP", "vi-VN"}),
            "vi");
  EXPECT_EQ(ResolveLanguageCode(AppLanguage::kSystem, {"de-DE"}), "vi");
}

TEST(LocalizationTest, FormatsLocalizedStrings) {
  EXPECT_EQ(Localized(AppLanguage::kEnglish, "menu.input_style.off"), L"Off");
  EXPECT_EQ(Localized(AppLanguage::kVietnamese, "menu.input_style.off"),
            L"Tắt");
  EXPECT_EQ(
      LocalizedFormat(AppLanguage::kEnglish, "menu.tooltip.mode", L"Telex"),
      L"ÔKêyTre: Telex");
  EXPECT_EQ(LocalizedFormat(AppLanguage::kVietnamese, "settings.sidebar.version",
                            L"0.3"),
            L"Phiên bản 0.3");
}

TEST(LocalizationTest, LanguageRawRoundTrip) {
  EXPECT_EQ(AppLanguageFromRaw(AppLanguageToRaw(AppLanguage::kSystem)),
            AppLanguage::kSystem);
  EXPECT_EQ(AppLanguageFromRaw(AppLanguageToRaw(AppLanguage::kVietnamese)),
            AppLanguage::kVietnamese);
  EXPECT_EQ(AppLanguageFromRaw(AppLanguageToRaw(AppLanguage::kEnglish)),
            AppLanguage::kEnglish);
}

}  // namespace
}  // namespace okeytre::win
