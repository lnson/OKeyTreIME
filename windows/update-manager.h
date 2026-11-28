// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_UPDATE_MANAGER_H_
#define OKEYTRE_WINDOWS_UPDATE_MANAGER_H_

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "windows/app-version.h"
#include "windows/settings-store.h"
#include "windows/update-parser.h"

namespace okeytre::win {

// Background GitHub Releases update checker using native WinHTTP.
// Posts `notify_msg_` to `notify_hwnd_` when an asynchronous check completes so
// all UI observers run safely on the main UI thread.
class UpdateManager {
 public:
  static constexpr int64_t kCheckIntervalSeconds = 24 * 60 * 60;
  static constexpr int64_t kRetryIntervalSeconds = 60 * 60;

  using StateChangedFn = std::function<void()>;

  UpdateManager(SettingsStore& settings, HWND notify_hwnd, UINT notify_msg);
  ~UpdateManager();

  UpdateManager(const UpdateManager&) = delete;
  UpdateManager& operator=(const UpdateManager&) = delete;

  void SetOnStateChanged(StateChangedFn callback) {
    on_state_changed_ = std::move(callback);
  }

  const AppVersion& current_version() const noexcept {
    return current_version_;
  }

  bool is_auto_check_enabled() const noexcept { return is_auto_check_enabled_; }
  void SetAutoCheckEnabled(bool enabled);

  bool is_checking() const noexcept { return is_checking_; }
  bool did_confirm_up_to_date() const noexcept {
    return did_confirm_up_to_date_;
  }
  const std::optional<UpdateRelease>& available_release() const noexcept {
    return available_release_;
  }
  std::optional<UpdateRelease> visible_available_release() const;

  // Starts/triggers an automatic check if enabled and due (24h since last
  // successful check).
  void CheckIfDueInBackground();

  // Triggers an explicit user-initiated update check (clears skipped version if
  // an update is found, or briefly shows "Up to date").
  void CheckForUpdatesNow();

  // Called by the main window procedure when `notify_msg_` arrives.
  void HandleAsyncCheckFinished();

  // Clears the transient "Up to date" confirmation banner (called on a 4s
  // timer from the main window).
  void ClearUpToDateBanner();

  void SkipRelease(const UpdateRelease& release);
  void OpenReleasePage(const std::optional<UpdateRelease>& release = std::nullopt);
  void OpenReleasesWebPage();

 private:
  struct PendingResult {
    std::optional<UpdateRelease> release;
    bool succeeded = false;
    bool user_initiated = false;
  };

  void StartCheckThread(bool user_initiated);
  static int64_t NowUnixSeconds();

  SettingsStore& settings_;
  HWND notify_hwnd_ = nullptr;
  AppVersion current_version_;
  std::optional<std::string> skipped_version_;
  std::optional<UpdateRelease> available_release_;
  StateChangedFn on_state_changed_;
  absl::Mutex result_mutex_;
  std::optional<PendingResult> pending_result_ ABSL_GUARDED_BY(result_mutex_);

  UINT notify_msg_ = 0;
  std::atomic<bool> shutting_down_{false};
  bool is_auto_check_enabled_ = true;
  bool is_checking_ = false;
  bool did_confirm_up_to_date_ = false;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_UPDATE_MANAGER_H_
