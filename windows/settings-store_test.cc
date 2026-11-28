// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/settings-store.h"

#include "gtest/gtest.h"

namespace okeytre::win {
namespace {

TEST(SettingsStoreTest, DefaultsMatchMacOS) {
  SettingsStore store = SettingsStore::CreateInMemory();

  EXPECT_EQ(store.GetInputStyle(), InputStyle::kTelex);
  EXPECT_EQ(store.GetLastActiveInputStyle(), InputStyle::kTelex);
  EXPECT_TRUE(store.GetModernTone());
  EXPECT_FALSE(store.GetUoHorn());
  EXPECT_FALSE(store.GetNgToNgh());
  EXPECT_FALSE(store.GetNghToNg());
  EXPECT_TRUE(store.GetRestoreInvalidWords());
  EXPECT_EQ(store.GetMaxBufferSize(), 16);
  EXPECT_EQ(store.GetAppLanguage(), AppLanguage::kSystem);
  EXPECT_TRUE(store.GetAutoCheckUpdates());
  EXPECT_FALSE(store.GetSkippedUpdateVersion().has_value());
}

TEST(SettingsStoreTest, ShowcasePresentationLifecycle) {
  SettingsStore store = SettingsStore::CreateInMemory();

  // Fresh install shows first-install showcase.
  EXPECT_EQ(store.ResolveLaunchShowcaseMode("0.3"),
            ShowcaseMode::kFirstInstall);

  // Marking seen suppresses subsequent launches for the same milestone.
  store.MarkShowcaseSeen("0.3");
  EXPECT_EQ(store.ResolveLaunchShowcaseMode("0.3"), ShowcaseMode::kNone);

  // Future milestone re-opens showcase in feature-update mode.
  EXPECT_EQ(store.ResolveLaunchShowcaseMode("0.4"),
            ShowcaseMode::kFeatureUpdate);
}

TEST(SettingsStoreTest, PriorInstallWithoutShowcaseKeyShowsFeatureUpdate) {
  SettingsStore store = SettingsStore::CreateInMemory();
  store.SetInputStyle(InputStyle::kVNI);

  EXPECT_EQ(store.ResolveLaunchShowcaseMode("0.3"),
            ShowcaseMode::kFeatureUpdate);
}

}  // namespace
}  // namespace okeytre::win
