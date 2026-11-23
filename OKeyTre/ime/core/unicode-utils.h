// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Runtime UTF-16 character classification and case transformation (via
// uni-algo).

#ifndef UNICODE_UTILS_HPP_
#define UNICODE_UTILS_HPP_

#include <cstdint>

#include "uni_algo/case.h"
#include "uni_algo/prop.h"

namespace okeytre {

// Returns true if the BMP UTF-16 code unit `c` is a whitespace, newline,
// separator, or punctuation character that bounds a word.
inline bool IsWordBoundary(uint16_t c) noexcept {
  const una::codepoint::prop p{static_cast<char32_t>(c)};
  return p.White_Space() || p.General_Category_Z() || p.General_Category_P();
}

// Returns true if the BMP UTF-16 code unit `c` is an uppercase letter.
inline bool IsUpper(uint16_t c) noexcept {
  return una::codepoint::is_uppercase(static_cast<char32_t>(c));
}

// Converts a BMP UTF-16 code unit `c` to lowercase.
inline uint16_t ToLower(uint16_t c) noexcept {
  return static_cast<uint16_t>(
      una::codepoint::to_simple_lowercase(static_cast<char32_t>(c)));
}

// Converts a BMP UTF-16 code unit `c` to uppercase.
inline uint16_t ToUpper(uint16_t c) noexcept {
  return static_cast<uint16_t>(
      una::codepoint::to_simple_uppercase(static_cast<char32_t>(c)));
}

}  // namespace okeytre

#endif  // UNICODE_UTILS_HPP_
