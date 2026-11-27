// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_SETTINGS_STORE_H_
#define OKEYTRE_WINDOWS_SETTINGS_STORE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
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

// Abstract key-value persistence backend (implemented via HKCU\Software\OKeyTre
// on Windows and an in-memory map for unit tests).
class SettingsBackend {
 public:
  virtual ~SettingsBackend() = default;

  virtual std::optional<std::string> GetString(std::string_view key) const = 0;
  virtual void SetString(std::string_view key, std::string_view value) = 0;
  virtual void RemoveKey(std::string_view key) = 0;

  virtual std::optional<int64_t> GetInt64(std::string_view key) const = 0;
  virtual void SetInt64(std::string_view key, int64_t value) = 0;

  virtual std::optional<bool> GetBool(std::string_view key) const = 0;
  virtual void SetBool(std::string_view key, bool value) = 0;

  virtual bool HasKey(std::string_view key) const = 0;
};

class InMemorySettingsBackend final : public SettingsBackend {
 public:
  std::optional<std::string> GetString(std::string_view key) const override;
  void SetString(std::string_view key, std::string_view value) override;
  void RemoveKey(std::string_view key) override;

  std::optional<int64_t> GetInt64(std::string_view key) const override;
  void SetInt64(std::string_view key, int64_t value) override;

  std::optional<bool> GetBool(std::string_view key) const override;
  void SetBool(std::string_view key, bool value) override;

  bool HasKey(std::string_view key) const override;

 private:
  absl::flat_hash_map<std::string, std::string> strings_;
  absl::flat_hash_map<std::string, int64_t> ints_;
  absl::flat_hash_map<std::string, bool> bools_;
};

std::unique_ptr<SettingsBackend> CreateRegistrySettingsBackend();

// Typed settings facade matching all macOS UserDefaults keys and defaults.
class SettingsStore {
 public:
  static constexpr bool kDefaultModernTone = true;
  static constexpr bool kDefaultUoHorn = false;
  static constexpr bool kDefaultNgToNgh = false;
  static constexpr bool kDefaultNghToNg = false;
  static constexpr bool kDefaultRestoreInvalidWords = true;
  static constexpr bool kDefaultAutoCheckUpdates = true;
  static constexpr int32_t kDefaultMaxBufferSize = 16;

  explicit SettingsStore(std::unique_ptr<SettingsBackend> backend);

  InputStyle GetInputStyle() const;
  void SetInputStyle(InputStyle style);

  InputStyle GetLastActiveInputStyle() const;
  void SetLastActiveInputStyle(InputStyle style);

  bool GetModernTone() const;
  void SetModernTone(bool enabled);

  bool GetUoHorn() const;
  void SetUoHorn(bool enabled);

  bool GetNgToNgh() const;
  void SetNgToNgh(bool enabled);

  bool GetNghToNg() const;
  void SetNghToNg(bool enabled);

  bool GetRestoreInvalidWords() const;
  void SetRestoreInvalidWords(bool enabled);

  int32_t GetMaxBufferSize() const;
  void SetMaxBufferSize(int32_t size);

  AppLanguage GetAppLanguage() const;
  void SetAppLanguage(AppLanguage language);

  bool GetHasConfiguredLaunchAtLogin() const;
  void SetHasConfiguredLaunchAtLogin(bool configured);

  bool GetAutoCheckUpdates() const;
  void SetAutoCheckUpdates(bool enabled);

  int64_t GetLastUpdateCheckTimestamp() const;
  void SetLastUpdateCheckTimestamp(int64_t unix_seconds);

  std::optional<std::string> GetSkippedUpdateVersion() const;
  void SetSkippedUpdateVersion(std::string_view version);
  void ClearSkippedUpdateVersion();

  ShowcaseMode ResolveLaunchShowcaseMode(
      std::string_view milestone_version =
          kCurrentFeatureMilestoneVersionString) const;
  void MarkShowcaseSeen(std::string_view milestone_version =
                            kCurrentFeatureMilestoneVersionString);

 private:
  std::unique_ptr<SettingsBackend> backend_;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_SETTINGS_STORE_H_
