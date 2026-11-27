// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/app-version.h"

#include <algorithm>
#include <utility>

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

namespace okeytre::win {

namespace {

bool AllAsciiDigits(std::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) {
    return absl::ascii_isdigit(c);
  });
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
  std::string_view trimmed = absl::StripAsciiWhitespace(raw);
  if (!absl::ConsumePrefix(&trimmed, "v")) {
    absl::ConsumePrefix(&trimmed, "V");
  }
  if (trimmed.empty()) {
    return std::nullopt;
  }

  // Strip optional +build metadata.
  const std::pair<std::string_view, std::string_view> build_split =
      absl::StrSplit(trimmed, absl::MaxSplits('+', 1));
  const std::string_view without_build = build_split.first;
  if (without_build.empty()) {
    return std::nullopt;
  }

  const size_t dash_pos = without_build.find('-');
  const std::string_view core_part = dash_pos == std::string_view::npos
                                         ? without_build
                                         : without_build.substr(0, dash_pos);
  if (core_part.empty()) {
    return std::nullopt;
  }

  std::vector<int> parsed_numbers;
  for (std::string_view token : absl::StrSplit(core_part, '.')) {
    int value = 0;
    if (!AllAsciiDigits(token) || !absl::SimpleAtoi(token, &value) ||
        value < 0) {
      return std::nullopt;
    }
    parsed_numbers.push_back(value);
  }

  std::vector<PrereleaseIdentifier> parsed_prerelease;
  std::string prerelease_text;
  if (dash_pos != std::string_view::npos) {
    const std::string_view pre_part = without_build.substr(dash_pos + 1);
    if (pre_part.empty()) {
      return std::nullopt;
    }
    for (std::string_view token : absl::StrSplit(pre_part, '.')) {
      if (token.empty()) {
        return std::nullopt;
      }
      int num = 0;
      if (AllAsciiDigits(token) && absl::SimpleAtoi(token, &num)) {
        parsed_prerelease.emplace_back(num);
      } else {
        parsed_prerelease.emplace_back(std::string(token));
      }
    }
    prerelease_text = absl::StrCat("-", pre_part);
  }

  std::string desc =
      absl::StrCat(absl::StrJoin(parsed_numbers, "."), prerelease_text);

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
