// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/localization.h"

#include <windows.h>

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "windows/localization-tables.generated.h"

namespace okeytre::win {

namespace {

std::vector<std::string> GetSystemPreferredLanguages() {
  ULONG num_languages = 0;
  ULONG buffer_size = 0;
  if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num_languages, nullptr,
                                  &buffer_size) &&
      buffer_size > 1) {
    std::wstring buf(buffer_size, L'\0');
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num_languages,
                                    buf.data(), &buffer_size)) {
      return absl::StrSplit(
          WideToUtf8(std::wstring_view(buf.data(), buffer_size)), '\0',
          absl::SkipEmpty());
    }
  }
  return {};
}

}  // namespace

std::string_view AppLanguageToRaw(AppLanguage language) noexcept {
  switch (language) {
    case AppLanguage::kSystem:
      return "system";
    case AppLanguage::kVietnamese:
      return "vi";
    case AppLanguage::kEnglish:
      return "en";
  }
  return "system";
}

AppLanguage AppLanguageFromRaw(std::string_view raw) noexcept {
  if (raw == "vi") return AppLanguage::kVietnamese;
  if (raw == "en") return AppLanguage::kEnglish;
  return AppLanguage::kSystem;
}

std::string_view ResolveLanguageCode(
    AppLanguage language,
    const std::vector<std::string>& preferred_languages) {
  switch (language) {
    case AppLanguage::kVietnamese:
      return "vi";
    case AppLanguage::kEnglish:
      return "en";
    case AppLanguage::kSystem:
      for (const std::string& identifier : preferred_languages) {
        if (absl::EqualsIgnoreCase(identifier, "en") ||
            absl::StartsWithIgnoreCase(identifier, "en-") ||
            absl::StartsWithIgnoreCase(identifier, "en_")) {
          return "en";
        }
        if (absl::EqualsIgnoreCase(identifier, "vi") ||
            absl::StartsWithIgnoreCase(identifier, "vi-") ||
            absl::StartsWithIgnoreCase(identifier, "vi_")) {
          return "vi";
        }
      }
      return "vi";
  }
  return "vi";
}

std::string_view ResolveSystemLanguageCode(AppLanguage language) {
  if (language == AppLanguage::kVietnamese) return "vi";
  if (language == AppLanguage::kEnglish) return "en";
  return ResolveLanguageCode(language, GetSystemPreferredLanguages());
}

std::wstring_view Localized(AppLanguage language, L10nKey key) {
  const size_t idx = static_cast<size_t>(key);
  if (idx >= static_cast<size_t>(L10nKey::kCount)) {
    return L"";
  }
  const std::string_view code = ResolveSystemLanguageCode(language);
  const auto& table = (code == "en") ? generated::kEnglishStrings
                                     : generated::kVietnameseStrings;
  return table[idx];
}

std::wstring LocalizedFormat(AppLanguage language, L10nKey key,
                             std::wstring_view arg) {
  std::wstring result(Localized(language, key));
  const size_t pos = result.find(L"%@");
  if (pos != std::wstring::npos) {
    result.replace(pos, 2, arg);
  }
  return result;
}

std::wstring_view LanguageDisplayName(AppLanguage option,
                                      AppLanguage active_language) {
  switch (option) {
    case AppLanguage::kSystem:
      return Localized(active_language, L10nKey::kLanguageSystem);
    case AppLanguage::kVietnamese:
      return L"Tiếng Việt";
    case AppLanguage::kEnglish:
      return L"English";
  }
  return Localized(active_language, L10nKey::kLanguageSystem);
}

std::wstring Utf8ToWide(std::string_view utf8) {
  if (utf8.empty()) {
    return {};
  }
  std::wstring out;
  out.resize_and_overwrite(utf8.size(), [utf8](wchar_t* buf, size_t n) {
    const int written = MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), buf,
        static_cast<int>(n));
    return written > 0 ? static_cast<size_t>(written) : 0;
  });
  return out;
}

std::string WideToUtf8(std::wstring_view wide) {
  if (wide.empty()) {
    return {};
  }
  std::string out;
  out.resize_and_overwrite(wide.size() * 3, [wide](char* buf, size_t n) {
    const int written = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), buf,
        static_cast<int>(n), nullptr, nullptr);
    return written > 0 ? static_cast<size_t>(written) : 0;
  });
  return out;
}

}  // namespace okeytre::win
