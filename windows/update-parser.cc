// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/update-parser.h"

#include <windows.h>
#include <winhttp.h>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/match.h"
#include "windows/localization.h"
#include "yyjson.h"

namespace okeytre::win {

namespace {

bool IsWindowsAssetName(std::string_view name) {
  const bool is_archive = absl::EndsWithIgnoreCase(name, ".zip") ||
                          absl::EndsWithIgnoreCase(name, ".exe") ||
                          absl::EndsWithIgnoreCase(name, ".msi");
  const bool mentions_win = absl::StrContainsIgnoreCase(name, "windows") ||
                            absl::StrContainsIgnoreCase(name, "win64") ||
                            absl::StrContainsIgnoreCase(name, "x64");
  return is_archive && mentions_win;
}

std::string_view GetJsonString(yyjson_val* obj, const char* key) {
  yyjson_val* val = yyjson_obj_get(obj, key);
  if (!yyjson_is_str(val)) return {};
  return {yyjson_get_str(val), yyjson_get_len(val)};
}

}  // namespace

bool IsValidatedGitHubUrl(std::string_view url) {
  const std::wstring wurl = Utf8ToWide(url);
  if (wurl.empty()) return false;

  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  uc.dwHostNameLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wurl.data(), static_cast<DWORD>(wurl.size()),
                       ICU_REJECT_USERPWD, &uc) ||
      uc.nScheme != INTERNET_SCHEME_HTTPS ||
      uc.nPort != INTERNET_DEFAULT_HTTPS_PORT || uc.lpszHostName == nullptr ||
      uc.dwHostNameLength == 0) {
    return false;
  }
  const std::string host =
      WideToUtf8(std::wstring_view(uc.lpszHostName, uc.dwHostNameLength));
  if (absl::StrContains(host, '\\')) return false;

  return absl::EqualsIgnoreCase(host, "github.com") ||
         absl::EndsWithIgnoreCase(host, ".github.com") ||
         absl::EndsWithIgnoreCase(host, ".githubusercontent.com");
}

std::optional<UpdateRelease> ParseLatestGitHubRelease(
    std::string_view json_body, const AppVersion& current_version) {
  yyjson_doc* doc = yyjson_read(json_body.data(), json_body.size(), 0);
  if (doc == nullptr) return std::nullopt;
  const auto free_doc = absl::MakeCleanup([doc]() { yyjson_doc_free(doc); });

  yyjson_val* root = yyjson_doc_get_root(doc);
  if (!yyjson_is_obj(root)) return std::nullopt;

  if (yyjson_is_true(yyjson_obj_get(root, "draft")) ||
      yyjson_is_true(yyjson_obj_get(root, "prerelease"))) {
    return std::nullopt;
  }

  const std::string_view tag_name = GetJsonString(root, "tag_name");
  if (tag_name.empty()) return std::nullopt;

  const std::optional<AppVersion> latest_version = AppVersion::Parse(tag_name);
  if (latest_version <= current_version) return std::nullopt;

  const std::string_view html_url = GetJsonString(root, "html_url");

  std::optional<std::string> win_download_url;
  yyjson_val* assets = yyjson_obj_get(root, "assets");
  if (yyjson_is_arr(assets)) {
    size_t idx = 0;
    size_t max = 0;
    yyjson_val* item = nullptr;
    yyjson_arr_foreach(assets, idx, max, item) {
      if (!yyjson_is_obj(item)) continue;
      const std::string_view name = GetJsonString(item, "name");
      const std::string_view url = GetJsonString(item, "browser_download_url");
      if (IsWindowsAssetName(name) && IsValidatedGitHubUrl(url)) {
        win_download_url = std::string(url);
        break;
      }
    }
  }

  return UpdateRelease{
      .tag_name = std::string(tag_name),
      .version = *latest_version,
      .release_page_url = std::string(
          IsValidatedGitHubUrl(html_url) ? html_url : kReleasesWebUrl),
      .windows_download_url = std::move(win_download_url),
  };
}

}  // namespace okeytre::win
