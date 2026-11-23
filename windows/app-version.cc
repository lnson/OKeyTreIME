// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/app-version.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace okeytre::win {

namespace {

std::string_view TrimAsciiWhitespace(std::string_view s) {
  while (!s.empty() &&
         std::isspace(static_cast<unsigned char>(s.front())) != 0) {
    s.remove_prefix(1);
  }
  while (!s.empty() &&
         std::isspace(static_cast<unsigned char>(s.back())) != 0) {
    s.remove_suffix(1);
  }
  return s;
}

bool AllAsciiDigits(std::string_view s) {
  if (s.empty()) {
    return false;
  }
  for (char c : s) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

std::strong_ordering ComparePrereleaseId(
    const AppVersion::PrereleaseIdentifier& lhs,
    const AppVersion::PrereleaseIdentifier& rhs) {
  const bool lhs_num = std::holds_alternative<int>(lhs);
  const bool rhs_num = std::holds_alternative<int>(rhs);
  if (lhs_num && rhs_num) {
    return std::get<int>(lhs) <=> std::get<int>(rhs);
  }
  if (lhs_num && !rhs_num) {
    // Per SemVer 2.0.0, numeric identifiers have lower precedence.
    return std::strong_ordering::less;
  }
  if (!lhs_num && rhs_num) {
    return std::strong_ordering::greater;
  }
  const std::string& a = std::get<std::string>(lhs);
  const std::string& b = std::get<std::string>(rhs);
  if (a < b) return std::strong_ordering::less;
  if (a > b) return std::strong_ordering::greater;
  return std::strong_ordering::equal;
}

}  // namespace

AppVersion::AppVersion(std::vector<int> numbers,
                       std::vector<PrereleaseIdentifier> prerelease,
                       std::string description)
    : numbers_(std::move(numbers)),
      prerelease_(std::move(prerelease)),
      description_(std::move(description)) {}

std::optional<AppVersion> AppVersion::Parse(std::string_view raw) {
  std::string_view trimmed = TrimAsciiWhitespace(raw);
  if (!trimmed.empty() && (trimmed.front() == 'v' || trimmed.front() == 'V')) {
    trimmed.remove_prefix(1);
  }
  if (trimmed.empty()) {
    return std::nullopt;
  }

  // Strip optional +build metadata.
  const size_t plus_pos = trimmed.find('+');
  std::string_view without_build =
      plus_pos == std::string_view::npos ? trimmed : trimmed.substr(0, plus_pos);
  if (without_build.empty()) {
    return std::nullopt;
  }

  const size_t dash_pos = without_build.find('-');
  std::string_view core_part = dash_pos == std::string_view::npos
                                   ? without_build
                                   : without_build.substr(0, dash_pos);
  if (core_part.empty()) {
    return std::nullopt;
  }

  std::vector<int> parsed_numbers;
  size_t pos = 0;
  while (true) {
    const size_t dot = core_part.find('.', pos);
    std::string_view token = dot == std::string_view::npos
                                 ? core_part.substr(pos)
                                 : core_part.substr(pos, dot - pos);
    if (!AllAsciiDigits(token)) {
      return std::nullopt;
    }
    int value = 0;
    const auto [ptr, ec] =
        std::from_chars(token.data(), token.data() + token.size(), value);
    if (ec != std::errc{} || ptr != token.data() + token.size() || value < 0) {
      return std::nullopt;
    }
    parsed_numbers.push_back(value);
    if (dot == std::string_view::npos) {
      break;
    }
    pos = dot + 1;
  }

  std::vector<PrereleaseIdentifier> parsed_prerelease;
  std::string prerelease_text;
  if (dash_pos != std::string_view::npos) {
    std::string_view pre_part = without_build.substr(dash_pos + 1);
    if (pre_part.empty()) {
      return std::nullopt;
    }
    size_t pre_pos = 0;
    while (true) {
      const size_t dot = pre_part.find('.', pre_pos);
      std::string_view token = dot == std::string_view::npos
                                   ? pre_part.substr(pre_pos)
                                   : pre_part.substr(pre_pos, dot - pre_pos);
      if (token.empty()) {
        return std::nullopt;
      }
      if (AllAsciiDigits(token)) {
        int num = 0;
        const auto [ptr, ec] =
            std::from_chars(token.data(), token.data() + token.size(), num);
        if (ec == std::errc{} && ptr == token.data() + token.size()) {
          parsed_prerelease.emplace_back(num);
        } else {
          parsed_prerelease.emplace_back(std::string(token));
        }
      } else {
        parsed_prerelease.emplace_back(std::string(token));
      }
      if (dot == std::string_view::npos) {
        break;
      }
      pre_pos = dot + 1;
    }
    prerelease_text = "-" + std::string(pre_part);
  }

  std::string desc;
  for (size_t i = 0; i < parsed_numbers.size(); ++i) {
    if (i > 0) desc.push_back('.');
    desc += std::to_string(parsed_numbers[i]);
  }
  desc += prerelease_text;

  return AppVersion(std::move(parsed_numbers), std::move(parsed_prerelease),
                    std::move(desc));
}

std::vector<int> AppVersion::NormalizedCore() const {
  std::vector<int> trimmed = numbers_;
  while (trimmed.size() > 1 && trimmed.back() == 0) {
    trimmed.pop_back();
  }
  return trimmed;
}

bool AppVersion::operator==(const AppVersion& other) const {
  return NormalizedCore() == other.NormalizedCore() &&
         prerelease_ == other.prerelease_;
}

std::strong_ordering AppVersion::operator<=>(const AppVersion& other) const {
  const size_t count = std::max(numbers_.size(), other.numbers_.size());
  for (size_t i = 0; i < count; ++i) {
    const int left = i < numbers_.size() ? numbers_[i] : 0;
    const int right = i < other.numbers_.size() ? other.numbers_[i] : 0;
    if (left != right) {
      return left <=> right;
    }
  }

  if (prerelease_.empty() && other.prerelease_.empty()) {
    return std::strong_ordering::equal;
  }
  if (prerelease_.empty()) {
    // Normal release has higher precedence than a prerelease.
    return std::strong_ordering::greater;
  }
  if (other.prerelease_.empty()) {
    return std::strong_ordering::less;
  }

  const size_t pre_count =
      std::min(prerelease_.size(), other.prerelease_.size());
  for (size_t i = 0; i < pre_count; ++i) {
    const auto cmp = ComparePrereleaseId(prerelease_[i], other.prerelease_[i]);
    if (cmp != std::strong_ordering::equal) {
      return cmp;
    }
  }
  return prerelease_.size() <=> other.prerelease_.size();
}

}  // namespace okeytre::win
