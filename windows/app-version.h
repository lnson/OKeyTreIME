// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_APP_VERSION_H_
#define OKEYTRE_WINDOWS_APP_VERSION_H_

#include <compare>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace okeytre::win {

inline constexpr std::string_view kCurrentAppVersionString = "0.3.1";
inline constexpr std::string_view kCurrentFeatureMilestoneVersionString = "0.3";

// Semantic version parser and comparator with SemVer 2.0.0 prerelease ordering
// and trailing-zero normalization (e.g. "0.3" == "0.3.0").
class AppVersion {
 public:
  using PrereleaseIdentifier = std::variant<int, std::string>;

  static std::optional<AppVersion> Parse(std::string_view raw);

  const std::vector<int>& numbers() const noexcept { return numbers_; }
  const std::vector<PrereleaseIdentifier>& prerelease() const noexcept {
    return prerelease_;
  }
  const std::string& description() const noexcept { return description_; }

  bool operator==(const AppVersion& other) const;
  std::strong_ordering operator<=>(const AppVersion& other) const;

 private:
  AppVersion(std::vector<int> numbers,
             std::vector<PrereleaseIdentifier> prerelease,
             std::string description);

  std::vector<int> NormalizedCore() const;

  std::vector<int> numbers_;
  std::vector<PrereleaseIdentifier> prerelease_;
  std::string description_;
};

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_APP_VERSION_H_
