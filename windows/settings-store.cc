// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/settings-store.h"

#include <windows.h>

#include <utility>

#include "absl/cleanup/cleanup.h"

namespace okeytre::win {

namespace {

constexpr std::string_view kKeyInputStyle = "inputMethodStyle";
constexpr std::string_view kKeyLastActiveStyle = "lastActiveInputMethodStyle";
constexpr std::string_view kKeyModernTone = "modernTone";
constexpr std::string_view kKeyUoHorn = "uoHorn";
constexpr std::string_view kKeyNgToNgh = "ngToNgh";
constexpr std::string_view kKeyNghToNg = "nghToNg";
constexpr std::string_view kKeyRestoreInvalidWords = "restoreInvalidWords";
constexpr std::string_view kKeyMaxBufferSize = "maxBufferSize";
constexpr std::string_view kKeyAppLanguage = "appLanguage";
constexpr std::string_view kKeyHasConfiguredLaunchAtLogin =
    "hasConfiguredLaunchAtLogin";
constexpr std::string_view kKeyAutoCheckUpdates =
    "automaticallyCheckForUpdates";
constexpr std::string_view kKeyLastUpdateCheck = "lastUpdateCheckTimestamp";
constexpr std::string_view kKeySkippedVersion = "skippedUpdateVersion";
constexpr std::string_view kKeyLastSeenShowcase = "lastSeenShowcaseVersion";
constexpr std::string_view kKeyLegacyHasSeenShowcase = "hasSeenFeatureShowcase";

constexpr wchar_t kRegistrySubKey[] = L"Software\\OKeyTre";

class RegistrySettingsBackend final : public SettingsBackend {
 public:
  std::optional<std::string> GetString(std::string_view key) const override {
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistrySubKey, 0, KEY_READ, &hkey) !=
        ERROR_SUCCESS) {
      return std::nullopt;
    }
    const auto close_key = absl::MakeCleanup([hkey]() { RegCloseKey(hkey); });
    const std::wstring wkey = Utf8ToWide(key);
    DWORD type = 0;
    DWORD cb_data = 0;
    const LONG rc =
        RegQueryValueExW(hkey, wkey.c_str(), nullptr, &type, nullptr, &cb_data);
    if (rc != ERROR_SUCCESS || type != REG_SZ || cb_data == 0) {
      return std::nullopt;
    }
    std::wstring wval(cb_data / sizeof(wchar_t), L'\0');
    const LONG rc2 =
        RegQueryValueExW(hkey, wkey.c_str(), nullptr, &type,
                         reinterpret_cast<LPBYTE>(wval.data()), &cb_data);
    if (rc2 != ERROR_SUCCESS) {
      return std::nullopt;
    }
    while (!wval.empty() && wval.back() == L'\0') {
      wval.pop_back();
    }
    return WideToUtf8(wval);
  }

  void SetString(std::string_view key, std::string_view value) override {
    HKEY hkey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistrySubKey, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &hkey, nullptr) != ERROR_SUCCESS) {
      return;
    }
    const auto close_key = absl::MakeCleanup([hkey]() { RegCloseKey(hkey); });
    const std::wstring wkey = Utf8ToWide(key);
    const std::wstring wval = Utf8ToWide(value);
    const DWORD cb_data =
        static_cast<DWORD>((wval.size() + 1) * sizeof(wchar_t));
    RegSetValueExW(hkey, wkey.c_str(), 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(wval.c_str()), cb_data);
  }

  void RemoveKey(std::string_view key) override {
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistrySubKey, 0, KEY_WRITE,
                      &hkey) != ERROR_SUCCESS) {
      return;
    }
    const auto close_key = absl::MakeCleanup([hkey]() { RegCloseKey(hkey); });
    const std::wstring wkey = Utf8ToWide(key);
    RegDeleteValueW(hkey, wkey.c_str());
  }

  std::optional<int64_t> GetInt64(std::string_view key) const override {
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistrySubKey, 0, KEY_READ, &hkey) !=
        ERROR_SUCCESS) {
      return std::nullopt;
    }
    const auto close_key = absl::MakeCleanup([hkey]() { RegCloseKey(hkey); });
    const std::wstring wkey = Utf8ToWide(key);
    DWORD type = 0;
    uint64_t qword = 0;
    DWORD cb_data = sizeof(qword);
    const LONG rc =
        RegQueryValueExW(hkey, wkey.c_str(), nullptr, &type,
                         reinterpret_cast<LPBYTE>(&qword), &cb_data);
    if (rc != ERROR_SUCCESS) {
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

  void SetInt64(std::string_view key, int64_t value) override {
    HKEY hkey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistrySubKey, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &hkey, nullptr) != ERROR_SUCCESS) {
      return;
    }
    const auto close_key = absl::MakeCleanup([hkey]() { RegCloseKey(hkey); });
    const std::wstring wkey = Utf8ToWide(key);
    const uint64_t qword = static_cast<uint64_t>(value);
    RegSetValueExW(hkey, wkey.c_str(), 0, REG_QWORD,
                   reinterpret_cast<const BYTE*>(&qword), sizeof(qword));
  }

  std::optional<bool> GetBool(std::string_view key) const override {
    const auto val = GetInt64(key);
    if (!val.has_value()) {
      return std::nullopt;
    }
    return *val != 0;
  }

  void SetBool(std::string_view key, bool value) override {
    HKEY hkey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistrySubKey, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &hkey, nullptr) != ERROR_SUCCESS) {
      return;
    }
    const auto close_key = absl::MakeCleanup([hkey]() { RegCloseKey(hkey); });
    const std::wstring wkey = Utf8ToWide(key);
    const DWORD dword = value ? 1u : 0u;
    RegSetValueExW(hkey, wkey.c_str(), 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&dword), sizeof(dword));
  }

  bool HasKey(std::string_view key) const override {
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistrySubKey, 0, KEY_READ, &hkey) !=
        ERROR_SUCCESS) {
      return false;
    }
    const auto close_key = absl::MakeCleanup([hkey]() { RegCloseKey(hkey); });
    const std::wstring wkey = Utf8ToWide(key);
    return RegQueryValueExW(hkey, wkey.c_str(), nullptr, nullptr, nullptr,
                            nullptr) == ERROR_SUCCESS;
  }
};

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

std::optional<std::string> InMemorySettingsBackend::GetString(
    std::string_view key) const {
  const auto it = strings_.find(key);
  if (it == strings_.end()) return std::nullopt;
  return it->second;
}

void InMemorySettingsBackend::SetString(std::string_view key,
                                        std::string_view value) {
  strings_.insert_or_assign(std::string(key), std::string(value));
}

void InMemorySettingsBackend::RemoveKey(std::string_view key) {
  strings_.erase(key);
  ints_.erase(key);
  bools_.erase(key);
}

std::optional<int64_t> InMemorySettingsBackend::GetInt64(
    std::string_view key) const {
  const auto it = ints_.find(key);
  if (it == ints_.end()) return std::nullopt;
  return it->second;
}

void InMemorySettingsBackend::SetInt64(std::string_view key, int64_t value) {
  ints_.insert_or_assign(std::string(key), value);
}

std::optional<bool> InMemorySettingsBackend::GetBool(
    std::string_view key) const {
  const auto it = bools_.find(key);
  if (it == bools_.end()) return std::nullopt;
  return it->second;
}

void InMemorySettingsBackend::SetBool(std::string_view key, bool value) {
  bools_.insert_or_assign(std::string(key), value);
}

bool InMemorySettingsBackend::HasKey(std::string_view key) const {
  return strings_.contains(key) || ints_.contains(key) || bools_.contains(key);
}

std::unique_ptr<SettingsBackend> CreateRegistrySettingsBackend() {
  return std::make_unique<RegistrySettingsBackend>();
}

SettingsStore::SettingsStore(std::unique_ptr<SettingsBackend> backend)
    : backend_(std::move(backend)) {}

InputStyle SettingsStore::GetInputStyle() const {
  const auto raw = backend_->GetString(kKeyInputStyle);
  if (!raw.has_value()) {
    return InputStyle::kTelex;
  }
  return InputStyleFromRaw(*raw).value_or(InputStyle::kTelex);
}

void SettingsStore::SetInputStyle(InputStyle style) {
  backend_->SetString(kKeyInputStyle, InputStyleToRaw(style));
}

InputStyle SettingsStore::GetLastActiveInputStyle() const {
  const InputStyle selected = GetInputStyle();
  if (selected != InputStyle::kOff) {
    return selected;
  }
  const auto raw = backend_->GetString(kKeyLastActiveStyle);
  if (raw.has_value()) {
    const auto parsed = InputStyleFromRaw(*raw);
    if (parsed.has_value() && *parsed != InputStyle::kOff) {
      return *parsed;
    }
  }
  return InputStyle::kTelex;
}

void SettingsStore::SetLastActiveInputStyle(InputStyle style) {
  if (style != InputStyle::kOff) {
    backend_->SetString(kKeyLastActiveStyle, InputStyleToRaw(style));
  }
}

bool SettingsStore::GetModernTone() const {
  return backend_->GetBool(kKeyModernTone).value_or(kDefaultModernTone);
}

void SettingsStore::SetModernTone(bool enabled) {
  backend_->SetBool(kKeyModernTone, enabled);
}

bool SettingsStore::GetUoHorn() const {
  return backend_->GetBool(kKeyUoHorn).value_or(kDefaultUoHorn);
}

void SettingsStore::SetUoHorn(bool enabled) {
  backend_->SetBool(kKeyUoHorn, enabled);
}

bool SettingsStore::GetNgToNgh() const {
  return backend_->GetBool(kKeyNgToNgh).value_or(kDefaultNgToNgh);
}

void SettingsStore::SetNgToNgh(bool enabled) {
  backend_->SetBool(kKeyNgToNgh, enabled);
}

bool SettingsStore::GetNghToNg() const {
  return backend_->GetBool(kKeyNghToNg).value_or(kDefaultNghToNg);
}

void SettingsStore::SetNghToNg(bool enabled) {
  backend_->SetBool(kKeyNghToNg, enabled);
}

bool SettingsStore::GetRestoreInvalidWords() const {
  return backend_->GetBool(kKeyRestoreInvalidWords)
      .value_or(kDefaultRestoreInvalidWords);
}

void SettingsStore::SetRestoreInvalidWords(bool enabled) {
  backend_->SetBool(kKeyRestoreInvalidWords, enabled);
}

int32_t SettingsStore::GetMaxBufferSize() const {
  const auto val = backend_->GetInt64(kKeyMaxBufferSize);
  if (!val.has_value() || *val <= 0) {
    return kDefaultMaxBufferSize;
  }
  return static_cast<int32_t>(*val);
}

void SettingsStore::SetMaxBufferSize(int32_t size) {
  backend_->SetInt64(kKeyMaxBufferSize, size);
}

AppLanguage SettingsStore::GetAppLanguage() const {
  const auto raw = backend_->GetString(kKeyAppLanguage);
  if (!raw.has_value()) {
    return AppLanguage::kSystem;
  }
  return AppLanguageFromRaw(*raw);
}

void SettingsStore::SetAppLanguage(AppLanguage language) {
  backend_->SetString(kKeyAppLanguage, AppLanguageToRaw(language));
}

bool SettingsStore::GetHasConfiguredLaunchAtLogin() const {
  return backend_->GetBool(kKeyHasConfiguredLaunchAtLogin).value_or(false);
}

void SettingsStore::SetHasConfiguredLaunchAtLogin(bool configured) {
  backend_->SetBool(kKeyHasConfiguredLaunchAtLogin, configured);
}

bool SettingsStore::GetAutoCheckUpdates() const {
  return backend_->GetBool(kKeyAutoCheckUpdates)
      .value_or(kDefaultAutoCheckUpdates);
}

void SettingsStore::SetAutoCheckUpdates(bool enabled) {
  backend_->SetBool(kKeyAutoCheckUpdates, enabled);
}

int64_t SettingsStore::GetLastUpdateCheckTimestamp() const {
  return backend_->GetInt64(kKeyLastUpdateCheck).value_or(0);
}

void SettingsStore::SetLastUpdateCheckTimestamp(int64_t unix_seconds) {
  backend_->SetInt64(kKeyLastUpdateCheck, unix_seconds);
}

std::optional<std::string> SettingsStore::GetSkippedUpdateVersion() const {
  return backend_->GetString(kKeySkippedVersion);
}

void SettingsStore::SetSkippedUpdateVersion(std::string_view version) {
  backend_->SetString(kKeySkippedVersion, version);
}

void SettingsStore::ClearSkippedUpdateVersion() {
  backend_->RemoveKey(kKeySkippedVersion);
}

ShowcaseMode SettingsStore::ResolveLaunchShowcaseMode(
    std::string_view milestone_version_str) const {
  const auto milestone = AppVersion::Parse(milestone_version_str);
  if (!milestone.has_value()) {
    return ShowcaseMode::kNone;
  }

  if (const auto seen_raw = backend_->GetString(kKeyLastSeenShowcase);
      seen_raw.has_value()) {
    if (const auto seen = AppVersion::Parse(*seen_raw); seen.has_value()) {
      if (*seen < *milestone) {
        return ShowcaseMode::kFeatureUpdate;
      }
      return ShowcaseMode::kNone;
    }
  }

  const bool has_prior_install =
      backend_->GetBool(kKeyLegacyHasSeenShowcase).value_or(false) ||
      backend_->HasKey(kKeyHasConfiguredLaunchAtLogin) ||
      backend_->HasKey(kKeyInputStyle);

  return has_prior_install ? ShowcaseMode::kFeatureUpdate
                           : ShowcaseMode::kFirstInstall;
}

void SettingsStore::MarkShowcaseSeen(std::string_view milestone_version_str) {
  backend_->SetString(kKeyLastSeenShowcase, milestone_version_str);
  backend_->SetBool(kKeyLegacyHasSeenShowcase, true);
}

}  // namespace okeytre::win
