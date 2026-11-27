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
  EXPECT_EQ(Localized(AppLanguage::kEnglish, L10nKey::kMenuInputStyleOff),
            L"Off");
  EXPECT_EQ(Localized(AppLanguage::kVietnamese, L10nKey::kMenuInputStyleOff),
            L"Tắt");
  EXPECT_EQ(LocalizedFormat(AppLanguage::kEnglish, L10nKey::kMenuTooltipMode,
                            L"Telex"),
            L"ÔKêyTre: Telex");
  EXPECT_EQ(LocalizedFormat(AppLanguage::kVietnamese,
                            L10nKey::kSettingsSidebarVersion, L"0.3"),
            L"Phiên bản 0.3");
  EXPECT_EQ(Localized(AppLanguage::kEnglish, L10nKey::kCount), L"");
}

TEST(LocalizationTest, AppliesWindowsOverridesOverSharedStrings) {
  EXPECT_NE(Localized(AppLanguage::kEnglish,
                      L10nKey::kSettingsGeneralInputStyleDescription)
                .find(L"Left Ctrl + Right Ctrl"),
            std::wstring::npos);
  EXPECT_NE(Localized(AppLanguage::kVietnamese,
                      L10nKey::kSettingsGeneralInputStyleDescription)
                .find(L"Ctrl Trái + Ctrl Phải"),
            std::wstring::npos);
  EXPECT_EQ(Localized(AppLanguage::kEnglish,
                      L10nKey::kSettingsOrthographyModernToneTitle),
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
