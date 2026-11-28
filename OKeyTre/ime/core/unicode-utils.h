// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Runtime UTF-16 character classification and case transformation.

#ifndef UNICODE_UTILS_HPP_
#define UNICODE_UTILS_HPP_

#include <cstdint>

namespace okeytre {

// Returns true if the BMP UTF-16 code unit `c` is a whitespace, newline,
// separator, or punctuation character that bounds a word (matching Unicode
// White_Space, General_Category Z, and General_Category P for ASCII, Latin-1,
// General Punctuation, and CJK/Fullwidth punctuation; excluding symbols such as
// +, ^, ~, ` used by VIQR).
constexpr bool IsWordBoundary(uint16_t c) noexcept {
  if (c < 0x80) {
    switch (c) {
      case '\t':
      case '\n':
      case '\v':
      case '\f':
      case '\r':
      case ' ':
      case '!':
      case '"':
      case '#':
      case '%':
      case '&':
      case '\'':
      case '(':
      case ')':
      case '*':
      case ',':
      case '-':
      case '.':
      case '/':
      case ':':
      case ';':
      case '?':
      case '@':
      case '[':
      case '\\':
      case ']':
      case '_':
      case '{':
      case '}':
        return true;
      default:
        return false;
    }
  }
  // Unicode White_Space & General_Category Z (separators).
  if (c == 0x0085 || c == 0x00A0 || c == 0x1680 ||
      (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
      c == 0x202F || c == 0x205F || c == 0x3000) {
    return true;
  }
  // Latin-1 Supplement punctuation.
  if (c == 0x00A1 || c == 0x00A7 || c == 0x00AB || c == 0x00B6 ||
      c == 0x00B7 || c == 0x00BB || c == 0x00BF) {
    return true;
  }
  // General Punctuation (dashes, curly quotes, ellipsis, bullets, etc.).
  if ((c >= 0x2010 && c <= 0x2027) || (c >= 0x2030 && c <= 0x205E)) {
    return true;
  }
  // CJK & Fullwidth punctuation.
  if ((c >= 0x3001 && c <= 0x3003) || (c >= 0x3008 && c <= 0x3011) ||
      (c >= 0x3014 && c <= 0x301F) || (c >= 0xFF01 && c <= 0xFF03) ||
      (c >= 0xFF05 && c <= 0xFF0A) || (c >= 0xFF0C && c <= 0xFF0F) ||
      c == 0xFF1A || c == 0xFF1B || c == 0xFF1F || c == 0xFF20 ||
      (c >= 0xFF3B && c <= 0xFF3D) || c == 0xFF3F || c == 0xFF5B ||
      c == 0xFF5D || (c >= 0xFF5F && c <= 0xFF65)) {
    return true;
  }
  return false;
}

// Returns true if the BMP UTF-16 code unit `c` is an uppercase Latin/Vietnamese
// letter.
constexpr bool IsUpper(uint16_t c) noexcept {
  if (c >= 'A' && c <= 'Z') {
    return true;
  }
  if (c < 0xC0) {
    return false;
  }
  // Latin-1 Supplement uppercase (U+00C0..U+00DE except U+00D7).
  if (c <= 0x00DE) {
    return c != 0x00D7;
  }
  // Latin Extended-A (includes Ă, Đ, Ĩ, Ũ).
  if (c >= 0x0100 && c <= 0x017E) {
    if (c <= 0x012E) return (c & 1u) == 0;
    if (c == 0x0130) return true;
    if (c >= 0x0132 && c <= 0x0136) return (c & 1u) == 0;
    if (c >= 0x0139 && c <= 0x0147) return (c & 1u) == 1;
    if (c >= 0x014A && c <= 0x0176) return (c & 1u) == 0;
    if (c == 0x0178) return true;
    if (c >= 0x0179 && c <= 0x017D) return (c & 1u) == 1;
    return false;
  }
  // Vietnamese horn vowels in Latin Extended-B (Ơ, Ư).
  if (c == 0x01A0 || c == 0x01AF) {
    return true;
  }
  // Latin Extended Additional (includes all Vietnamese vowels U+1EA0..U+1EF9).
  if (c >= 0x1E00 && c <= 0x1EFE) {
    if (c <= 0x1E94) return (c & 1u) == 0;
    if (c == 0x1E9E) return true;
    if (c >= 0x1EA0) return (c & 1u) == 0;
  }
  return false;
}

// Converts a BMP UTF-16 code unit `c` to lowercase.
constexpr uint16_t ToLower(uint16_t c) noexcept {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<uint16_t>(c + 0x20);
  }
  if (c < 0xC0) {
    return c;
  }
  if (c <= 0x00DE) {
    return c != 0x00D7 ? static_cast<uint16_t>(c + 0x20) : c;
  }
  if (c >= 0x0100 && c <= 0x017E) {
    if (c <= 0x012E) return (c & 1u) == 0 ? static_cast<uint16_t>(c + 1) : c;
    if (c == 0x0130) return 'i';
    if (c >= 0x0132 && c <= 0x0136) {
      return (c & 1u) == 0 ? static_cast<uint16_t>(c + 1) : c;
    }
    if (c >= 0x0139 && c <= 0x0147) {
      return (c & 1u) == 1 ? static_cast<uint16_t>(c + 1) : c;
    }
    if (c >= 0x014A && c <= 0x0176) {
      return (c & 1u) == 0 ? static_cast<uint16_t>(c + 1) : c;
    }
    if (c == 0x0178) return 0x00FF;
    if (c >= 0x0179 && c <= 0x017D) {
      return (c & 1u) == 1 ? static_cast<uint16_t>(c + 1) : c;
    }
    return c;
  }
  if (c == 0x01A0 || c == 0x01AF) {
    return static_cast<uint16_t>(c + 1);
  }
  if (c >= 0x1E00 && c <= 0x1EFE) {
    if (c <= 0x1E94) return (c & 1u) == 0 ? static_cast<uint16_t>(c + 1) : c;
    if (c == 0x1E9E) return 0x00DF;
    if (c >= 0x1EA0) return (c & 1u) == 0 ? static_cast<uint16_t>(c + 1) : c;
  }
  return c;
}

// Converts a BMP UTF-16 code unit `c` to uppercase.
constexpr uint16_t ToUpper(uint16_t c) noexcept {
  if (c >= 'a' && c <= 'z') {
    return static_cast<uint16_t>(c - 0x20);
  }
  if (c < 0xE0) {
    return c;
  }
  if (c <= 0x00FE) {
    return c != 0x00F7 ? static_cast<uint16_t>(c - 0x20) : c;
  }
  if (c == 0x00FF) {
    return 0x0178;
  }
  if (c >= 0x0101 && c <= 0x017E) {
    if (c <= 0x012F) return (c & 1u) == 1 ? static_cast<uint16_t>(c - 1) : c;
    if (c == 0x0131) return 'I';
    if (c >= 0x0133 && c <= 0x0137) {
      return (c & 1u) == 1 ? static_cast<uint16_t>(c - 1) : c;
    }
    if (c >= 0x013A && c <= 0x0148) {
      return (c & 1u) == 0 ? static_cast<uint16_t>(c - 1) : c;
    }
    if (c >= 0x014B && c <= 0x0177) {
      return (c & 1u) == 1 ? static_cast<uint16_t>(c - 1) : c;
    }
    if (c >= 0x017A && c <= 0x017E) {
      return (c & 1u) == 0 ? static_cast<uint16_t>(c - 1) : c;
    }
    return c;
  }
  if (c == 0x01A1 || c == 0x01B0) {
    return static_cast<uint16_t>(c - 1);
  }
  if (c >= 0x1E01 && c <= 0x1EFF) {
    if (c <= 0x1E95) return (c & 1u) == 1 ? static_cast<uint16_t>(c - 1) : c;
    if (c >= 0x1EA1) return (c & 1u) == 1 ? static_cast<uint16_t>(c - 1) : c;
  }
  return c;
}

}  // namespace okeytre

#endif  // UNICODE_UTILS_HPP_
