// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/app-version.h"

#include "gtest/gtest.h"

namespace okeytre::win {
namespace {

TEST(AppVersionTest, ParsesValidVersions) {
  auto v1 = AppVersion::Parse("0.3");
  ASSERT_TRUE(v1.has_value());
  EXPECT_EQ(v1->description(), "0.3");

  auto v2 = AppVersion::Parse("v0.3.0");
  ASSERT_TRUE(v2.has_value());
  EXPECT_EQ(*v1, *v2);

  auto v3 = AppVersion::Parse("  V1.2.3-beta.2+build.45 ");
  ASSERT_TRUE(v3.has_value());
  EXPECT_EQ(v3->description(), "1.2.3-beta.2");
}

TEST(AppVersionTest, RejectsInvalidVersions) {
  EXPECT_FALSE(AppVersion::Parse("").has_value());
  EXPECT_FALSE(AppVersion::Parse("v").has_value());
  EXPECT_FALSE(AppVersion::Parse("1..2").has_value());
  EXPECT_FALSE(AppVersion::Parse("1.2-").has_value());
  EXPECT_FALSE(AppVersion::Parse("abc").has_value());
}

TEST(AppVersionTest, SemVerOrderingAndPrereleasePrecedence) {
  auto v02 = AppVersion::Parse("0.2");
  auto v021 = AppVersion::Parse("0.2.1");
  auto v03_alpha = AppVersion::Parse("0.3.0-alpha.1");
  auto v03_alpha2 = AppVersion::Parse("0.3.0-alpha.2");
  auto v03_beta = AppVersion::Parse("0.3.0-beta");
  auto v03 = AppVersion::Parse("0.3");
  auto v030 = AppVersion::Parse("0.3.0");

  ASSERT_TRUE(v02 && v021 && v03_alpha && v03_alpha2 && v03_beta && v03 &&
              v030);
  EXPECT_LT(*v02, *v021);
  EXPECT_LT(*v021, *v03_alpha);
  EXPECT_LT(*v03_alpha, *v03_alpha2);
  EXPECT_LT(*v03_alpha2, *v03_beta);
  EXPECT_LT(*v03_beta, *v03);
  EXPECT_EQ(*v03, *v030);
}

}  // namespace
}  // namespace okeytre::win
