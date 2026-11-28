// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_SETTINGS_STORE_H_
#define OKEYTRE_WINDOWS_SETTINGS_STORE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "windows/app-version.h"
#include "windows/localization.h"

namespace okeytre::win {

enum class InputStyle : uint8_t {
  kOff = 0,
  kTelex = 1,
  kVNI = 2,
  kVIQR = 3,
};

std::string_view InputStyleToRaw(InputStyle style) noexcept;
std::optional<InputStyle> InputStyleFromRaw(std::string_view raw) noexcept;
std::wstring_view InputStyleDisplayName(InputStyle style, AppLanguage language);

enum class ShowcaseMode : uint8_t {
  kNone = 0,
  kFirstInstall = 1,
  kFeatureUpdate = 2,
};

// Typed settings store matching all macOS UserDefaults keys and defaults.
// Persists to HKCU\Software\OKeyTre when opened via OpenRegistry(), or keeps
// values in memory only when created via CreateInMemory() (for unit tests).
class SettingsStore {
 public:
  static constexpr bool kDefaultModernTone = true;
  static constexpr bool kDefaultUoHorn = false;
  static constexpr bool kDefaultNgToNgh = false;
  static constexpr bool kDefaultNghToNg = false;
  static constexpr bool kDefaultRestoreInvalidWords = true;
  static constexpr bool kDefaultAutoCheckUpdates = true;
  static constexpr int32_t kDefaultMaxBufferSize = 16;

  static SettingsStore OpenRegistry();
  static SettingsStore CreateInMemory();

  InputStyle GetInputStyle() const noexcept { return input_style_; }
  void SetInputStyle(InputStyle style);

  InputStyle GetLastActiveInputStyle() const noexcept;
  void SetLastActiveInputStyle(InputStyle style);

  bool GetModernTone() const noexcept { return modern_tone_; }
  void SetModernTone(bool enabled);

  bool GetUoHorn() const noexcept { return uo_horn_; }
  void SetUoHorn(bool enabled);

  bool GetNgToNgh() const noexcept { return ng_to_ngh_; }
  void SetNgToNgh(bool enabled);

  bool GetNghToNg() const noexcept { return ngh_to_ng_; }
  void SetNghToNg(bool enabled);

  bool GetRestoreInvalidWords() const noexcept {
    return restore_invalid_words_;
  }
  void SetRestoreInvalidWords(bool enabled);

  int32_t GetMaxBufferSize() const noexcept {
    return max_buffer_size_ > 0 ? max_buffer_size_ : kDefaultMaxBufferSize;
  }
  void SetMaxBufferSize(int32_t size);

  AppLanguage GetAppLanguage() const noexcept { return app_language_; }
  void SetAppLanguage(AppLanguage language);

  bool GetHasConfiguredLaunchAtLogin() const noexcept {
    return has_configured_launch_at_login_;
  }
  void SetHasConfiguredLaunchAtLogin(bool configured);

  bool GetAutoCheckUpdates() const noexcept { return auto_check_updates_; }
  void SetAutoCheckUpdates(bool enabled);

  int64_t GetLastUpdateCheckTimestamp() const noexcept {
    return last_update_check_timestamp_;
  }
  void SetLastUpdateCheckTimestamp(int64_t unix_seconds);

  std::optional<std::string> GetSkippedUpdateVersion() const {
    return skipped_update_version_;
  }
  void SetSkippedUpdateVersion(std::string_view version);
  void ClearSkippedUpdateVersion();

  ShowcaseMode ResolveLaunchShowcaseMode(
      std::string_view milestone_version =
          kCurrentFeatureMilestoneVersionString) const;
  void MarkShowcaseSeen(std::string_view milestone_version =
                            kCurrentFeatureMilestoneVersionString);

 private:
  explicit SettingsStore(bool persist_to_registry) noexcept
      : persist_to_registry_(persist_to_registry) {}

  void LoadFromRegistry();

  std::optional<std::string> skipped_update_version_;
  std::optional<std::string> last_seen_showcase_version_;
  int64_t last_update_check_timestamp_ = 0;
  int32_t max_buffer_size_ = kDefaultMaxBufferSize;
  InputStyle input_style_ = InputStyle::kTelex;
  InputStyle last_active_input_style_ = InputStyle::kTelex;
  AppLanguage app_language_ = AppLanguage::kSystem;
  bool persist_to_registry_ = false;
  bool modern_tone_ = kDefaultModernTone;
  bool uo_horn_ = kDefaultUoHorn;
  bool ng_to_ngh_ = kDefaultNgToNgh;
  bool ngh_to_ng_ = kDefaultNghToNg;
  bool restore_invalid_words_ = kDefaultRestoreInvalidWords;
  bool has_configured_launch_at_login_ = false;
  bool auto_check_updates_ = kDefaultAutoCheckUpdates;
  bool has_prior_install_ = false;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_SETTINGS_STORE_H_
