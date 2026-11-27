// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_LOCALIZATION_H_
#define OKEYTRE_WINDOWS_LOCALIZATION_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "windows/localization-keys.generated.h"

namespace okeytre::win {

enum class AppLanguage : uint8_t {
  kSystem = 0,
  kVietnamese = 1,
  kEnglish = 2,
};

std::string_view AppLanguageToRaw(AppLanguage language) noexcept;
AppLanguage AppLanguageFromRaw(std::string_view raw) noexcept;

// Resolves `kSystem` against an ordered list of BCP-47 language tags, matching
// the first English or Vietnamese entry and falling back to Vietnamese ("vi").
std::string_view ResolveLanguageCode(
    AppLanguage language,
    const std::vector<std::string>& preferred_languages);

// Queries the OS preferred UI languages (Windows) and resolves the active
// language code ("en" or "vi").
std::string_view ResolveSystemLanguageCode(AppLanguage language);

// Looks up a localized UTF-16 string by compile-time generated `L10nKey`.
// Returns a null-terminated view into static storage.
std::wstring_view Localized(AppLanguage language, L10nKey key);

// Looks up a localized UTF-16 format string and replaces "%@" with `arg`.
std::wstring LocalizedFormat(AppLanguage language, L10nKey key,
                             std::wstring_view arg);

// Display name for an `AppLanguage` option in the active UI language.
// Returns a null-terminated view into static storage.
std::wstring_view LanguageDisplayName(AppLanguage option,
                                      AppLanguage active_language);

// UTF-8 <-> UTF-16 helpers (safe on both Windows and POSIX).
std::wstring Utf8ToWide(std::string_view utf8);
std::string WideToUtf8(std::wstring_view wide);

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_LOCALIZATION_H_
