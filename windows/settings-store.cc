// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/settings-store.h"

#include <windows.h>

#include "absl/cleanup/cleanup.h"

namespace okeytre::win {

namespace {

constexpr wchar_t kRegistrySubKey[] = L"Software\\OKeyTre";

constexpr wchar_t kKeyInputStyle[] = L"inputMethodStyle";
constexpr wchar_t kKeyLastActiveStyle[] = L"lastActiveInputMethodStyle";
constexpr wchar_t kKeyModernTone[] = L"modernTone";
constexpr wchar_t kKeyUoHorn[] = L"uoHorn";
constexpr wchar_t kKeyNgToNgh[] = L"ngToNgh";
constexpr wchar_t kKeyNghToNg[] = L"nghToNg";
constexpr wchar_t kKeyRestoreInvalidWords[] = L"restoreInvalidWords";
constexpr wchar_t kKeyMaxBufferSize[] = L"maxBufferSize";
constexpr wchar_t kKeyAppLanguage[] = L"appLanguage";
constexpr wchar_t kKeyHasConfiguredLaunchAtLogin[] =
    L"hasConfiguredLaunchAtLogin";
constexpr wchar_t kKeyAutoCheckUpdates[] = L"automaticallyCheckForUpdates";
constexpr wchar_t kKeyLastUpdateCheck[] = L"lastUpdateCheckTimestamp";
constexpr wchar_t kKeySkippedVersion[] = L"skippedUpdateVersion";
constexpr wchar_t kKeyLastSeenShowcase[] = L"lastSeenShowcaseVersion";
constexpr wchar_t kKeyLegacyHasSeenShowcase[] = L"hasSeenFeatureShowcase";

std::optional<std::string> ReadRegString(HKEY hkey, const wchar_t* name) {
  DWORD type = 0;
  DWORD cb_data = 0;
  if (RegQueryValueExW(hkey, name, nullptr, &type, nullptr, &cb_data) !=
          ERROR_SUCCESS ||
      type != REG_SZ || cb_data == 0) {
    return std::nullopt;
  }
  std::wstring wval(cb_data / sizeof(wchar_t), L'\0');
  if (RegQueryValueExW(hkey, name, nullptr, &type,
                       reinterpret_cast<LPBYTE>(wval.data()),
                       &cb_data) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  while (!wval.empty() && wval.back() == L'\0') {
    wval.pop_back();
  }
  return WideToUtf8(wval);
}

std::optional<int64_t> ReadRegInt64(HKEY hkey, const wchar_t* name) {
  DWORD type = 0;
  uint64_t qword = 0;
  DWORD cb_data = sizeof(qword);
  if (RegQueryValueExW(hkey, name, nullptr, &type,
                       reinterpret_cast<LPBYTE>(&qword),
                       &cb_data) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  if (type == REG_QWORD) {
    return static_cast<int64_t>(qword);
  }
  if (type == REG_DWORD) {
    return static_cast<int64_t>(static_cast<int32_t>(qword & 0xFFFFFFFFu));
  }
  return std::nullopt;
}

std::optional<bool> ReadRegBool(HKEY hkey, const wchar_t* name) {
  const auto val = ReadRegInt64(hkey, name);
  if (!val.has_value()) {
    return std::nullopt;
  }
  return *val != 0;
}

bool HasRegValue(HKEY hkey, const wchar_t* name) {
  return RegQueryValueExW(hkey, name, nullptr, nullptr, nullptr, nullptr) ==
         ERROR_SUCCESS;
}

void WriteRegString(const wchar_t* name, std::string_view value) {
  HKEY hkey = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistrySubKey, 0, nullptr, 0,
                      KEY_WRITE, nullptr, &hkey, nullptr) != ERROR_SUCCESS) {
    return;
  }
  const auto close_key = absl::MakeCleanup([hkey]() { RegCloseKey(hkey); });
  const std::wstring wval = Utf8ToWide(value);
  const DWORD cb_data =
      static_cast<DWORD>((wval.size() + 1) * sizeof(wchar_t));
  RegSetValueExW(hkey, name, 0, REG_SZ,
                 reinterpret_cast<const BYTE*>(wval.c_str()), cb_data);
}

void WriteRegInt64(const wchar_t* name, int64_t value) {
  HKEY hkey = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistrySubKey, 0, nullptr, 0,
                      KEY_WRITE, nullptr, &hkey, nullptr) != ERROR_SUCCESS) {
    return;
  }
  const auto close_key = absl::MakeCleanup([hkey]() { RegCloseKey(hkey); });
  const uint64_t qword = static_cast<uint64_t>(value);
  RegSetValueExW(hkey, name, 0, REG_QWORD,
                 reinterpret_cast<const BYTE*>(&qword), sizeof(qword));
}

void WriteRegBool(const wchar_t* name, bool value) {
  HKEY hkey = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistrySubKey, 0, nullptr, 0,
                      KEY_WRITE, nullptr, &hkey, nullptr) != ERROR_SUCCESS) {
    return;
  }
  const auto close_key = absl::MakeCleanup([hkey]() { RegCloseKey(hkey); });
  const DWORD dword = value ? 1u : 0u;
  RegSetValueExW(hkey, name, 0, REG_DWORD,
                 reinterpret_cast<const BYTE*>(&dword), sizeof(dword));
}

void DeleteRegValue(const wchar_t* name) {
  HKEY hkey = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistrySubKey, 0, KEY_WRITE, &hkey) !=
      ERROR_SUCCESS) {
    return;
  }
  const auto close_key = absl::MakeCleanup([hkey]() { RegCloseKey(hkey); });
  RegDeleteValueW(hkey, name);
}

}  // namespace

std::string_view InputStyleToRaw(InputStyle style) noexcept {
  switch (style) {
    case InputStyle::kOff:
      return "off";
    case InputStyle::kTelex:
      return "telex";
    case InputStyle::kVNI:
      return "vni";
    case InputStyle::kVIQR:
      return "viqr";
  }
  return "telex";
}

std::optional<InputStyle> InputStyleFromRaw(std::string_view raw) noexcept {
  if (raw == "off") return InputStyle::kOff;
  if (raw == "telex") return InputStyle::kTelex;
  if (raw == "vni") return InputStyle::kVNI;
  if (raw == "viqr") return InputStyle::kVIQR;
  return std::nullopt;
}

std::wstring_view InputStyleDisplayName(InputStyle style,
                                        AppLanguage language) {
  switch (style) {
    case InputStyle::kTelex:
      return L"Telex";
    case InputStyle::kVNI:
      return L"VNI";
    case InputStyle::kVIQR:
      return L"VIQR";
    case InputStyle::kOff:
      return Localized(language, L10nKey::kMenuInputStyleOff);
  }
  return L"Telex";
}

SettingsStore SettingsStore::OpenRegistry() {
  SettingsStore store(/*persist_to_registry=*/true);
  store.LoadFromRegistry();
  return store;
}

SettingsStore SettingsStore::CreateInMemory() {
  return SettingsStore(/*persist_to_registry=*/false);
}

void SettingsStore::LoadFromRegistry() {
  HKEY hkey = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistrySubKey, 0, KEY_READ, &hkey) !=
      ERROR_SUCCESS) {
    return;
  }
  const auto close_key = absl::MakeCleanup([hkey]() { RegCloseKey(hkey); });

  if (const auto raw = ReadRegString(hkey, kKeyInputStyle); raw.has_value()) {
    input_style_ = InputStyleFromRaw(*raw).value_or(InputStyle::kTelex);
  }
  if (const auto raw = ReadRegString(hkey, kKeyLastActiveStyle);
      raw.has_value()) {
    const auto parsed = InputStyleFromRaw(*raw);
    if (parsed.has_value() && *parsed != InputStyle::kOff) {
      last_active_input_style_ = *parsed;
    }
  }
  modern_tone_ = ReadRegBool(hkey, kKeyModernTone).value_or(kDefaultModernTone);
  uo_horn_ = ReadRegBool(hkey, kKeyUoHorn).value_or(kDefaultUoHorn);
  ng_to_ngh_ = ReadRegBool(hkey, kKeyNgToNgh).value_or(kDefaultNgToNgh);
  ngh_to_ng_ = ReadRegBool(hkey, kKeyNghToNg).value_or(kDefaultNghToNg);
  restore_invalid_words_ = ReadRegBool(hkey, kKeyRestoreInvalidWords)
                               .value_or(kDefaultRestoreInvalidWords);
  if (const auto val = ReadRegInt64(hkey, kKeyMaxBufferSize);
      val.has_value() && *val > 0) {
    max_buffer_size_ = static_cast<int32_t>(*val);
  }
  if (const auto raw = ReadRegString(hkey, kKeyAppLanguage); raw.has_value()) {
    app_language_ = AppLanguageFromRaw(*raw);
  }
  has_configured_launch_at_login_ =
      ReadRegBool(hkey, kKeyHasConfiguredLaunchAtLogin).value_or(false);
  auto_check_updates_ =
      ReadRegBool(hkey, kKeyAutoCheckUpdates).value_or(kDefaultAutoCheckUpdates);
  last_update_check_timestamp_ =
      ReadRegInt64(hkey, kKeyLastUpdateCheck).value_or(0);
  skipped_update_version_ = ReadRegString(hkey, kKeySkippedVersion);
  last_seen_showcase_version_ = ReadRegString(hkey, kKeyLastSeenShowcase);
  has_prior_install_ =
      ReadRegBool(hkey, kKeyLegacyHasSeenShowcase).value_or(false) ||
      HasRegValue(hkey, kKeyHasConfiguredLaunchAtLogin) ||
      HasRegValue(hkey, kKeyInputStyle);
}

void SettingsStore::SetInputStyle(InputStyle style) {
  input_style_ = style;
  has_prior_install_ = true;
  if (persist_to_registry_) {
    WriteRegString(kKeyInputStyle, InputStyleToRaw(style));
  }
}

InputStyle SettingsStore::GetLastActiveInputStyle() const noexcept {
  return input_style_ != InputStyle::kOff ? input_style_
                                          : last_active_input_style_;
}

void SettingsStore::SetLastActiveInputStyle(InputStyle style) {
  if (style == InputStyle::kOff) {
    return;
  }
  last_active_input_style_ = style;
  if (persist_to_registry_) {
    WriteRegString(kKeyLastActiveStyle, InputStyleToRaw(style));
  }
}

void SettingsStore::SetModernTone(bool enabled) {
  modern_tone_ = enabled;
  if (persist_to_registry_) {
    WriteRegBool(kKeyModernTone, enabled);
  }
}

void SettingsStore::SetUoHorn(bool enabled) {
  uo_horn_ = enabled;
  if (persist_to_registry_) {
    WriteRegBool(kKeyUoHorn, enabled);
  }
}

void SettingsStore::SetNgToNgh(bool enabled) {
  ng_to_ngh_ = enabled;
  if (persist_to_registry_) {
    WriteRegBool(kKeyNgToNgh, enabled);
  }
}

void SettingsStore::SetNghToNg(bool enabled) {
  ngh_to_ng_ = enabled;
  if (persist_to_registry_) {
    WriteRegBool(kKeyNghToNg, enabled);
  }
}

void SettingsStore::SetRestoreInvalidWords(bool enabled) {
  restore_invalid_words_ = enabled;
  if (persist_to_registry_) {
    WriteRegBool(kKeyRestoreInvalidWords, enabled);
  }
}

void SettingsStore::SetMaxBufferSize(int32_t size) {
  max_buffer_size_ = size;
  if (persist_to_registry_) {
    WriteRegInt64(kKeyMaxBufferSize, size);
  }
}

void SettingsStore::SetAppLanguage(AppLanguage language) {
  app_language_ = language;
  if (persist_to_registry_) {
    WriteRegString(kKeyAppLanguage, AppLanguageToRaw(language));
  }
}

void SettingsStore::SetHasConfiguredLaunchAtLogin(bool configured) {
  has_configured_launch_at_login_ = configured;
  has_prior_install_ = true;
  if (persist_to_registry_) {
    WriteRegBool(kKeyHasConfiguredLaunchAtLogin, configured);
  }
}

void SettingsStore::SetAutoCheckUpdates(bool enabled) {
  auto_check_updates_ = enabled;
  if (persist_to_registry_) {
    WriteRegBool(kKeyAutoCheckUpdates, enabled);
  }
}

void SettingsStore::SetLastUpdateCheckTimestamp(int64_t unix_seconds) {
  last_update_check_timestamp_ = unix_seconds;
  if (persist_to_registry_) {
    WriteRegInt64(kKeyLastUpdateCheck, unix_seconds);
  }
}

void SettingsStore::SetSkippedUpdateVersion(std::string_view version) {
  skipped_update_version_.emplace(version);
  if (persist_to_registry_) {
    WriteRegString(kKeySkippedVersion, version);
  }
}

void SettingsStore::ClearSkippedUpdateVersion() {
  skipped_update_version_.reset();
  if (persist_to_registry_) {
    DeleteRegValue(kKeySkippedVersion);
  }
}

ShowcaseMode SettingsStore::ResolveLaunchShowcaseMode(
    std::string_view milestone_version_str) const {
  const auto milestone = AppVersion::Parse(milestone_version_str);
  if (!milestone.has_value()) {
    return ShowcaseMode::kNone;
  }

  if (last_seen_showcase_version_.has_value()) {
    if (const auto seen = AppVersion::Parse(*last_seen_showcase_version_);
        seen.has_value()) {
      if (*seen < *milestone) {
        return ShowcaseMode::kFeatureUpdate;
      }
      return ShowcaseMode::kNone;
    }
  }

  return has_prior_install_ ? ShowcaseMode::kFeatureUpdate
                            : ShowcaseMode::kFirstInstall;
}

void SettingsStore::MarkShowcaseSeen(std::string_view milestone_version_str) {
  last_seen_showcase_version_.emplace(milestone_version_str);
  has_prior_install_ = true;
  if (persist_to_registry_) {
    WriteRegString(kKeyLastSeenShowcase, milestone_version_str);
    WriteRegBool(kKeyLegacyHasSeenShowcase, true);
  }
}

}  // namespace okeytre::win
