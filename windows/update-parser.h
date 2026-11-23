// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#ifndef OKEYTRE_WINDOWS_UPDATE_PARSER_H_
#define OKEYTRE_WINDOWS_UPDATE_PARSER_H_

#include <optional>
#include <string>
#include <string_view>

#include "windows/app-version.h"

namespace okeytre::win {

inline constexpr std::string_view kReleasesWebUrl =
    "https://github.com/lnson/OKeyTreIME/releases/latest";

struct UpdateRelease {
  std::string tag_name;
  AppVersion version;
  std::string release_page_url;
  std::optional<std::string> windows_download_url;

  const std::string& display_version() const noexcept {
    return version.description();
  }

  bool operator==(const UpdateRelease& other) const = default;
};

// Validates that `url` is an HTTPS URL hosted on github.com or
// *.githubusercontent.com.
bool IsValidatedGitHubUrl(std::string_view url);

// Parses a GitHub Releases API JSON payload and returns an `UpdateRelease` if
// it represents a non-draft, non-prerelease version strictly newer than
// `current_version`.
std::optional<UpdateRelease> ParseLatestGitHubRelease(
    std::string_view json_body, const AppVersion& current_version);

}  // namespace okeytre::win

#endif  // OKEYTRE_WINDOWS_UPDATE_PARSER_H_
