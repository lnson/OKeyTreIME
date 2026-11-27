// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/localization.h"

#include "absl/strings/match.h"
#include "windows/localization-tables.generated.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace okeytre::win {

namespace {

std::vector<std::string> GetSystemPreferredLanguages() {
  std::vector<std::string> result;
#ifdef _WIN32
  ULONG num_languages = 0;
  ULONG buffer_size = 0;
  if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num_languages, nullptr,
                                  &buffer_size) &&
      buffer_size > 1) {
    std::wstring buf(buffer_size, L'\0');
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num_languages,
                                    buf.data(), &buffer_size)) {
      const wchar_t* p = buf.c_str();
      while (*p != L'\0') {
        std::wstring_view lang(p);
        result.push_back(WideToUtf8(lang));
        p += lang.size() + 1;
      }
    }
  }
#endif
  return result;
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
  std::wstring out;
  out.reserve(utf8.size());
  size_t i = 0;
  while (i < utf8.size()) {
    const unsigned char b0 = static_cast<unsigned char>(utf8[i]);
    uint32_t cp = 0xFFFD;
    if (b0 < 0x80) {
      cp = b0;
      i += 1;
    } else if ((b0 & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
      cp = ((b0 & 0x1Fu) << 6) |
           (static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu);
      i += 2;
    } else if ((b0 & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
      cp = ((b0 & 0x0Fu) << 12) |
           ((static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu) << 6) |
           (static_cast<unsigned char>(utf8[i + 2]) & 0x3Fu);
      i += 3;
    } else if ((b0 & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
      cp = ((b0 & 0x07u) << 18) |
           ((static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu) << 12) |
           ((static_cast<unsigned char>(utf8[i + 2]) & 0x3Fu) << 6) |
           (static_cast<unsigned char>(utf8[i + 3]) & 0x3Fu);
      i += 4;
    } else {
      i += 1;
    }
    out.push_back(static_cast<wchar_t>(cp));
  }
  return out;
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
  std::string out;
  out.reserve(wide.size());
  for (wchar_t wc : wide) {
    const uint32_t cp = static_cast<uint32_t>(wc);
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
#endif
}

}  // namespace okeytre::win
