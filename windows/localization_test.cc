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
  EXPECT_EQ(Localized(AppLanguage::kEnglish, "missing.key.example"),
            L"missing.key.example");
}

TEST(LocalizationTest, AppliesWindowsOverridesOverSharedStrings) {
  EXPECT_NE(Localized(AppLanguage::kEnglish,
                      "settings.general.input_style.description")
                .find(L"Left Ctrl + Right Ctrl"),
            std::wstring::npos);
  EXPECT_NE(Localized(AppLanguage::kVietnamese,
                      "settings.general.input_style.description")
                .find(L"Ctrl Trái + Ctrl Phải"),
            std::wstring::npos);
  EXPECT_EQ(Localized(AppLanguage::kEnglish,
                      "settings.orthography.modern_tone.title"),
            L"Use modern Vietnamese orthography (\"oà, oé, uý\")");
}

TEST(LocalizationTest, LanguageRawRoundTrip) {
  EXPECT_EQ(AppLanguageFromRaw(AppLanguageToRaw(AppLanguage::kSystem)),
            AppLanguage::kSystem);
  EXPECT_EQ(AppLanguageFromRaw(AppLanguageToRaw(AppLanguage::kVietnamese)),
            AppLanguage::kVietnamese);
  EXPECT_EQ(AppLanguageFromRaw(AppLanguageToRaw(AppLanguage::kEnglish)),
            AppLanguage::kEnglish);
}

TEST(LocalizationTest, Utf8WideRoundTrip) {
  EXPECT_EQ(Utf8ToWide(""), L"");
  EXPECT_EQ(WideToUtf8(L""), "");
  const std::string utf8 = "ÔKêyTre tiếng Việt ✨";
  const std::wstring wide = Utf8ToWide(utf8);
  EXPECT_EQ(wide, L"ÔKêyTre tiếng Việt ✨");
  EXPECT_EQ(WideToUtf8(wide), utf8);
}

}  // namespace
}  // namespace okeytre::win
