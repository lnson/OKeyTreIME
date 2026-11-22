// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Build-Time `.ime` Compiler Unicode Helpers (`imec`)

#ifndef OKEYTRE_IMEC_UTF8_UTILS_H_
#define OKEYTRE_IMEC_UTF8_UTILS_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "simdutf.h"
#include "unicode-utils.h"

namespace okeytre {

// Transfers the case of `original` onto `target`.
constexpr inline uint16_t MatchCase(uint16_t original,
                                    uint16_t target) noexcept {
  return IsUpper(original) ? ToUpper(target) : ToLower(target);
}

// Converts a UTF-8 string into a UTF-16 string (`std::u16string`) via simdutf.
inline std::u16string Utf8ToU16(std::string_view utf8) {
  std::u16string out(simdutf::utf16_length_from_utf8(utf8.data(), utf8.size()),
                     u'\0');
  const size_t written =
      simdutf::convert_utf8_to_utf16(utf8.data(), utf8.size(), out.data());
  out.resize(written);
  return out;
}

}  // namespace okeytre

#endif  // OKEYTRE_IMEC_UTF8_UTILS_H_
