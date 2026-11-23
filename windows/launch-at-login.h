// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_LAUNCH_AT_LOGIN_H_
#define OKEYTRE_WINDOWS_LAUNCH_AT_LOGIN_H_

#include "windows/settings-store.h"

namespace okeytre::win {

// Manages the per-user Windows login item under
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run ("OKeyTre").
class LaunchAtLoginManager {
 public:
  explicit LaunchAtLoginManager(SettingsStore& settings);

  bool IsEnabled() const;
  bool SetEnabled(bool enabled);

  // Enables Launch at Login once on first install if the user has not
  // previously configured it.
  void EnableByDefaultIfNeeded();

 private:
  SettingsStore& settings_;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_LAUNCH_AT_LOGIN_H_
