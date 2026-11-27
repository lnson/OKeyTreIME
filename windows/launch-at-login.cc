// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/launch-at-login.h"

#include <windows.h>

#include <string>

#include "absl/cleanup/cleanup.h"

namespace okeytre::win {

namespace {

constexpr wchar_t kRunSubKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"OKeyTre";

std::wstring GetCurrentExecutableQuotedPath() {
  std::wstring buf(MAX_PATH, L'\0');
  while (true) {
    const DWORD len =
        GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (len == 0) {
      return {};
    }
    if (len < buf.size() - 1) {
      buf.resize(len);
      break;
    }
    buf.resize(buf.size() * 2);
  }
  return L"\"" + buf + L"\"";
}

}  // namespace

LaunchAtLoginManager::LaunchAtLoginManager(SettingsStore& settings)
    : settings_(settings) {}

bool LaunchAtLoginManager::IsEnabled() const {
  HKEY hkey = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, KEY_READ, &hkey) !=
      ERROR_SUCCESS) {
    return false;
  }
  const auto close_key = absl::MakeCleanup([hkey]() { RegCloseKey(hkey); });
  DWORD type = 0;
  DWORD cb_data = 0;
  const LONG rc = RegQueryValueExW(hkey, kRunValueName, nullptr, &type, nullptr,
                                   &cb_data);
  return rc == ERROR_SUCCESS && type == REG_SZ && cb_data > sizeof(wchar_t);
}

bool LaunchAtLoginManager::SetEnabled(bool enabled) {
  settings_.SetHasConfiguredLaunchAtLogin(true);

  HKEY hkey = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, nullptr, 0, KEY_WRITE,
                      nullptr, &hkey, nullptr) != ERROR_SUCCESS) {
    return false;
  }
  const auto close_key = absl::MakeCleanup([hkey]() { RegCloseKey(hkey); });

  LONG rc = ERROR_SUCCESS;
  if (enabled) {
    const std::wstring cmd = GetCurrentExecutableQuotedPath();
    if (cmd.empty()) {
      return false;
    }
    const DWORD cb = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
    rc = RegSetValueExW(hkey, kRunValueName, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(cmd.c_str()), cb);
  } else {
    rc = RegDeleteValueW(hkey, kRunValueName);
    if (rc == ERROR_FILE_NOT_FOUND) {
      rc = ERROR_SUCCESS;
    }
  }

  return rc == ERROR_SUCCESS;
}

void LaunchAtLoginManager::EnableByDefaultIfNeeded() {
  if (settings_.GetHasConfiguredLaunchAtLogin()) {
    return;
  }
  if (IsEnabled()) {
    settings_.SetHasConfiguredLaunchAtLogin(true);
    return;
  }
  SetEnabled(true);
}

}  // namespace okeytre::win
