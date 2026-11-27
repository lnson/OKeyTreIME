// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/update-parser.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/strip.h"

namespace okeytre::win {

namespace {

// Minimal, strict RFC 8259 JSON parser tailored for GitHub release payloads
// (avoids pulling in an external JSON library dependency).
class JsonCursor {
 public:
  explicit JsonCursor(std::string_view src) : rem_(src) {}

  void SkipWhitespace() {
    while (!rem_.empty() &&
           (rem_.front() == ' ' || rem_.front() == '\t' ||
            rem_.front() == '\r' || rem_.front() == '\n')) {
      rem_.remove_prefix(1);
    }
  }

  bool AtEnd() const { return rem_.empty(); }

  char Peek() const { return rem_.empty() ? '\0' : rem_.front(); }

  bool Consume(char expected) {
    SkipWhitespace();
    if (!rem_.empty() && rem_.front() == expected) {
      rem_.remove_prefix(1);
      return true;
    }
    return false;
  }

  std::optional<std::string> ParseString() {
    SkipWhitespace();
    if (!absl::ConsumePrefix(&rem_, "\"")) {
      return std::nullopt;
    }
    std::string out;
    while (!rem_.empty()) {
      const char c = rem_.front();
      rem_.remove_prefix(1);
      if (c == '"') {
        return out;
      }
      if (c == '\\') {
        if (rem_.empty()) return std::nullopt;
        const char esc = rem_.front();
        rem_.remove_prefix(1);
        switch (esc) {
          case '"':
          case '\\':
          case '/':
            out.push_back(esc);
            break;
          case 'b':
            out.push_back('\b');
            break;
          case 'f':
            out.push_back('\f');
            break;
          case 'n':
            out.push_back('\n');
            break;
          case 'r':
            out.push_back('\r');
            break;
          case 't':
            out.push_back('\t');
            break;
          case 'u': {
            if (rem_.size() < 4) return std::nullopt;
            const std::string_view hex = rem_.substr(0, 4);
            uint32_t code = 0;
            if (!std::all_of(hex.begin(), hex.end(),
                             [](unsigned char ch) {
                               return absl::ascii_isxdigit(ch);
                             }) ||
                !absl::SimpleHexAtoi(hex, &code)) {
              return std::nullopt;
            }
            rem_.remove_prefix(4);
            if (code < 0x80) {
              out.push_back(static_cast<char>(code));
            } else if (code < 0x800) {
              out.push_back(static_cast<char>(0xC0 | (code >> 6)));
              out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
              out.push_back(static_cast<char>(0xE0 | (code >> 12)));
              out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            break;
          }
          default:
            return std::nullopt;
        }
      } else {
        out.push_back(c);
      }
    }
    return std::nullopt;
  }

  std::optional<bool> ParseBool() {
    SkipWhitespace();
    if (absl::ConsumePrefix(&rem_, "true")) return true;
    if (absl::ConsumePrefix(&rem_, "false")) return false;
    return std::nullopt;
  }

  bool SkipValue() {
    SkipWhitespace();
    if (AtEnd()) return false;
    const char c = Peek();
    if (c == '"') {
      return ParseString().has_value();
    }
    if (c == '{') {
      rem_.remove_prefix(1);
      if (Consume('}')) return true;
      while (true) {
        if (!ParseString().has_value()) return false;
        if (!Consume(':')) return false;
        if (!SkipValue()) return false;
        if (Consume('}')) return true;
        if (!Consume(',')) return false;
      }
    }
    if (c == '[') {
      rem_.remove_prefix(1);
      if (Consume(']')) return true;
      while (true) {
        if (!SkipValue()) return false;
        if (Consume(']')) return true;
        if (!Consume(',')) return false;
      }
    }
    if (absl::ConsumePrefix(&rem_, "true") ||
        absl::ConsumePrefix(&rem_, "false") ||
        absl::ConsumePrefix(&rem_, "null")) {
      return true;
    }
    // Number
    if (c == '-' || absl::ascii_isdigit(static_cast<unsigned char>(c))) {
      rem_.remove_prefix(1);
      while (!rem_.empty()) {
        const char n = rem_.front();
        if (absl::ascii_isdigit(static_cast<unsigned char>(n)) || n == '.' ||
            n == 'e' || n == 'E' || n == '+' || n == '-') {
          rem_.remove_prefix(1);
        } else {
          break;
        }
      }
      return true;
    }
    return false;
  }

 private:
  std::string_view rem_;
};

struct RawAsset {
  std::string name;
  std::string browser_download_url;
};

bool IsWindowsAssetName(std::string_view name) {
  if (absl::EndsWithIgnoreCase(name, ".exe") ||
      absl::EndsWithIgnoreCase(name, ".msi")) {
    return true;
  }
  return absl::EndsWithIgnoreCase(name, ".zip") &&
         (absl::StrContainsIgnoreCase(name, "win") ||
          absl::StrContainsIgnoreCase(name, "x64"));
}

}  // namespace

bool IsValidatedGitHubUrl(std::string_view url) {
  constexpr std::string_view kHttps = "https://";
  if (url.size() <= kHttps.size() || !absl::StartsWithIgnoreCase(url, kHttps)) {
    return false;
  }
  const std::string_view rest = url.substr(kHttps.size());
  const size_t slash = rest.find('/');
  const std::string_view authority =
      slash == std::string_view::npos ? rest : rest.substr(0, slash);
  // Reject userinfo (`user@host`) and ports for strictness.
  if (authority.empty() || absl::StrContains(authority, '@') ||
      absl::StrContains(authority, ':')) {
    return false;
  }
  return absl::EqualsIgnoreCase(authority, "github.com") ||
         absl::EndsWithIgnoreCase(authority, ".github.com") ||
         absl::EndsWithIgnoreCase(authority, ".githubusercontent.com");
}

std::optional<UpdateRelease> ParseLatestGitHubRelease(
    std::string_view json_body, const AppVersion& current_version) {
  JsonCursor cursor(json_body);
  if (!cursor.Consume('{')) {
    return std::nullopt;
  }

  std::string tag_name;
  std::string html_url;
  bool draft = false;
  bool prerelease = false;
  std::vector<RawAsset> assets;

  cursor.SkipWhitespace();
  if (!cursor.Consume('}')) {
    while (true) {
      auto key = cursor.ParseString();
      if (!key.has_value() || !cursor.Consume(':')) {
        return std::nullopt;
      }

      if (*key == "tag_name") {
        auto val = cursor.ParseString();
        if (!val.has_value()) return std::nullopt;
        tag_name = std::move(*val);
      } else if (*key == "html_url") {
        auto val = cursor.ParseString();
        if (!val.has_value()) return std::nullopt;
        html_url = std::move(*val);
      } else if (*key == "draft") {
        auto val = cursor.ParseBool();
        if (val.has_value()) {
          draft = *val;
        } else if (!cursor.SkipValue()) {
          return std::nullopt;
        }
      } else if (*key == "prerelease") {
        auto val = cursor.ParseBool();
        if (val.has_value()) {
          prerelease = *val;
        } else if (!cursor.SkipValue()) {
          return std::nullopt;
        }
      } else if (*key == "assets") {
        if (cursor.Consume('[')) {
          cursor.SkipWhitespace();
          if (!cursor.Consume(']')) {
            while (true) {
              if (cursor.Consume('{')) {
                RawAsset asset;
                cursor.SkipWhitespace();
                if (!cursor.Consume('}')) {
                  while (true) {
                    auto akey = cursor.ParseString();
                    if (!akey.has_value() || !cursor.Consume(':')) {
                      return std::nullopt;
                    }
                    if (*akey == "name") {
                      if (auto v = cursor.ParseString()) {
                        asset.name = std::move(*v);
                      } else if (!cursor.SkipValue()) {
                        return std::nullopt;
                      }
                    } else if (*akey == "browser_download_url") {
                      if (auto v = cursor.ParseString()) {
                        asset.browser_download_url = std::move(*v);
                      } else if (!cursor.SkipValue()) {
                        return std::nullopt;
                      }
                    } else if (!cursor.SkipValue()) {
                      return std::nullopt;
                    }
                    if (cursor.Consume('}')) break;
                    if (!cursor.Consume(',')) return std::nullopt;
                  }
                }
                if (!asset.name.empty() &&
                    !asset.browser_download_url.empty()) {
                  assets.push_back(std::move(asset));
                }
              } else if (!cursor.SkipValue()) {
                return std::nullopt;
              }
              if (cursor.Consume(']')) break;
              if (!cursor.Consume(',')) return std::nullopt;
            }
          }
        } else if (!cursor.SkipValue()) {
          return std::nullopt;
        }
      } else {
        if (!cursor.SkipValue()) {
          return std::nullopt;
        }
      }

      if (cursor.Consume('}')) break;
      if (!cursor.Consume(',')) return std::nullopt;
    }
  }

  if (draft || prerelease || tag_name.empty()) {
    return std::nullopt;
  }

  const auto latest_version = AppVersion::Parse(tag_name);
  if (!latest_version.has_value() || !(*latest_version > current_version)) {
    return std::nullopt;
  }

  std::string release_page_url = IsValidatedGitHubUrl(html_url)
                                     ? html_url
                                     : std::string(kReleasesWebUrl);

  std::optional<std::string> win_download_url;
  for (const RawAsset& asset : assets) {
    if (IsWindowsAssetName(asset.name) &&
        IsValidatedGitHubUrl(asset.browser_download_url)) {
      win_download_url = asset.browser_download_url;
      break;
    }
  }

  return UpdateRelease{
      .tag_name = std::move(tag_name),
      .version = *latest_version,
      .release_page_url = std::move(release_page_url),
      .windows_download_url = std::move(win_download_url),
  };
}

}  // namespace okeytre::win
