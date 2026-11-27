// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/localization.h"

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "simdutf.h"
#include "windows/localization-tables.generated.h"

#ifdef _WIN32
#include <windows.h>
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

std::wstring Localized(AppLanguage language, std::string_view key) {
  const std::string_view code = ResolveSystemLanguageCode(language);
  const auto& table = (code == "en") ? generated::EnglishStrings()
                                     : generated::VietnameseStrings();
  if (const auto it = table.find(key); it != table.end()) {
    return std::wstring(it->second);
  }
  const auto& fallback = generated::EnglishStrings();
  if (const auto it = fallback.find(key); it != fallback.end()) {
    return std::wstring(it->second);
  }
  return Utf8ToWide(key);
}

std::wstring LocalizedFormat(AppLanguage language, std::string_view key,
                             std::wstring_view arg) {
  std::wstring result = Localized(language, key);
  const size_t pos = result.find(L"%@");
  if (pos != std::wstring::npos) {
    result.replace(pos, 2, arg);
  }
  return result;
}

std::wstring LanguageDisplayName(AppLanguage option,
                                 AppLanguage active_language) {
  switch (option) {
    case AppLanguage::kSystem:
      return Localized(active_language, "language.system");
    case AppLanguage::kVietnamese:
      return L"Tiếng Việt";
    case AppLanguage::kEnglish:
      return L"English";
  }
  return Localized(active_language, "language.system");
}

std::wstring Utf8ToWide(std::string_view utf8) {
  if (utf8.empty()) {
    return {};
  }
  if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
    std::wstring out(simdutf::utf16_length_from_utf8(utf8.data(), utf8.size()),
                     L'\0');
    const size_t written = simdutf::convert_utf8_to_utf16(
        utf8.data(), utf8.size(), reinterpret_cast<char16_t*>(out.data()));
    out.resize(written);
    return out;
  } else {
    std::wstring out(simdutf::utf32_length_from_utf8(utf8.data(), utf8.size()),
                     L'\0');
    const size_t written = simdutf::convert_utf8_to_utf32(
        utf8.data(), utf8.size(), reinterpret_cast<char32_t*>(out.data()));
    out.resize(written);
    return out;
  }
}

std::string WideToUtf8(std::wstring_view wide) {
  if (wide.empty()) {
    return {};
  }
  if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
    const auto* src = reinterpret_cast<const char16_t*>(wide.data());
    std::string out(simdutf::utf8_length_from_utf16(src, wide.size()), '\0');
    const size_t written =
        simdutf::convert_utf16_to_utf8(src, wide.size(), out.data());
    out.resize(written);
    return out;
  } else {
    const auto* src = reinterpret_cast<const char32_t*>(wide.data());
    std::string out(simdutf::utf8_length_from_utf32(src, wide.size()), '\0');
    const size_t written =
        simdutf::convert_utf32_to_utf8(src, wide.size(), out.data());
    out.resize(written);
    return out;
  }
}

}  // namespace okeytre::win
