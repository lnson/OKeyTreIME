// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/localization.h"

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "windows/localization-tables.generated.h"

#ifdef _WIN32
#include <windows.h>
#else
#include "uni_algo/conv.h"
#endif

namespace okeytre::win {

namespace {

std::vector<std::string> GetSystemPreferredLanguages() {
#ifdef _WIN32
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
#endif
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
#ifdef _WIN32
  const int needed = MultiByteToWideChar(
      CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (needed <= 0) {
    return {};
  }
  std::wstring out(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                      out.data(), needed);
  return out;
#else
  if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
    return una::utf8to16<char, wchar_t>(utf8);
  } else {
    return una::utf8to32<char, wchar_t>(utf8);
  }
#endif
}

std::string WideToUtf8(std::wstring_view wide) {
  if (wide.empty()) {
    return {};
  }
#ifdef _WIN32
  const int needed = WideCharToMultiByte(
      CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
      nullptr, nullptr);
  if (needed <= 0) {
    return {};
  }
  std::string out(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      out.data(), needed, nullptr, nullptr);
  return out;
#else
  if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
    return una::utf16to8<wchar_t, char>(wide);
  } else {
    return una::utf32to8<wchar_t, char>(wide);
  }
#endif
}

}  // namespace okeytre::win
