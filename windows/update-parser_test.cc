// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/update-parser.h"

#include "gtest/gtest.h"

namespace okeytre::win {
namespace {

TEST(UpdateParserTest, ValidatesGitHubUrls) {
  EXPECT_TRUE(IsValidatedGitHubUrl(
      "https://github.com/lnson/OKeyTreIME/releases/tag/v0.4"));
  EXPECT_TRUE(IsValidatedGitHubUrl(
      "https://GITHUB.COM/lnson/OKeyTreIME/releases/tag/v0.4"));
  EXPECT_TRUE(IsValidatedGitHubUrl(
      "https://objects.githubusercontent.com/release/OKeyTre.exe"));
  EXPECT_TRUE(IsValidatedGitHubUrl("https://github.com?tab=releases"));
  EXPECT_FALSE(IsValidatedGitHubUrl("http://github.com/lnson/OKeyTreIME"));
  EXPECT_FALSE(IsValidatedGitHubUrl("https://evil.example.com/github.com"));
  EXPECT_FALSE(IsValidatedGitHubUrl("https://evil.example.com\\github.com"));
  EXPECT_FALSE(IsValidatedGitHubUrl("https://evil.example.com\\.github.com"));
  EXPECT_FALSE(IsValidatedGitHubUrl("https://user@github.com/lnson/repo"));
  EXPECT_FALSE(IsValidatedGitHubUrl("https://github.com:8443/lnson/repo"));
}

TEST(UpdateParserTest, ParsesNewerReleaseWithWindowsAsset) {
  constexpr std::string_view kPayload = R"json({
    "tag_name": "v0.4.0",
    "html_url": "https://github.com/lnson/OKeyTreIME/releases/tag/v0.4.0",
    "draft": false,
    "prerelease": false,
    "assets": [
      {
        "name": "OKeyTre-v0.4.0.dmg",
        "browser_download_url": "https://github.com/lnson/OKeyTreIME/releases/download/v0.4.0/OKeyTre-v0.4.0.dmg"
      },
      {
        "name": "OKeyTre-v0.4.0-windows-x64.zip",
        "browser_download_url": "https://github.com/lnson/OKeyTreIME/releases/download/v0.4.0/OKeyTre-v0.4.0-windows-x64.zip"
      }
    ]
  })json";

  auto current = AppVersion::Parse("0.3");
  ASSERT_TRUE(current.has_value());

  auto release = ParseLatestGitHubRelease(kPayload, *current);
  ASSERT_TRUE(release.has_value());
  EXPECT_EQ(release->display_version(), "0.4.0");
  EXPECT_EQ(release->release_page_url,
            "https://github.com/lnson/OKeyTreIME/releases/tag/v0.4.0");
  ASSERT_TRUE(release->windows_download_url.has_value());
  EXPECT_EQ(
      *release->windows_download_url,
      "https://github.com/lnson/OKeyTreIME/releases/download/v0.4.0/OKeyTre-v0.4.0-windows-x64.zip");
}

TEST(UpdateParserTest, IgnoresDraftPrereleaseAndOlderVersions) {
  auto current = AppVersion::Parse("0.3");
  ASSERT_TRUE(current.has_value());

  EXPECT_FALSE(ParseLatestGitHubRelease(
                   R"({"tag_name":"v0.3.0","html_url":"https://github.com/lnson/OKeyTreIME"})",
                   *current)
                   .has_value());
  EXPECT_FALSE(ParseLatestGitHubRelease(
                   R"({"tag_name":"v0.4","draft":true,"html_url":"https://github.com/lnson/OKeyTreIME"})",
                   *current)
                   .has_value());
  EXPECT_FALSE(ParseLatestGitHubRelease(
                   R"({"tag_name":"v0.4","prerelease":true,"html_url":"https://github.com/lnson/OKeyTreIME"})",
                   *current)
                   .has_value());
}

}  // namespace
}  // namespace okeytre::win
