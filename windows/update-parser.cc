// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "windows/update-parser.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <vector>

namespace okeytre::win {

namespace {

std::string ToLowerAscii(std::string_view s) {
  std::string out(s.size(), '\0');
  for (size_t i = 0; i < s.size(); ++i) {
    out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
  }
  return out;
}

// Minimal, strict RFC 8259 JSON parser tailored for GitHub release payloads
// (avoids pulling in an external JSON library dependency).
class JsonCursor {
 public:
  explicit JsonCursor(std::string_view src) : src_(src) {}

  void SkipWhitespace() {
    while (pos_ < src_.size() &&
           (src_[pos_] == ' ' || src_[pos_] == '\t' || src_[pos_] == '\r' ||
            src_[pos_] == '\n')) {
      ++pos_;
    }
  }

  bool AtEnd() const { return pos_ >= src_.size(); }

  char Peek() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }

  bool Consume(char expected) {
    SkipWhitespace();
    if (pos_ < src_.size() && src_[pos_] == expected) {
      ++pos_;
      return true;
    }
    return false;
  }

  std::optional<std::string> ParseString() {
    SkipWhitespace();
    if (pos_ >= src_.size() || src_[pos_] != '"') {
      return std::nullopt;
    }
    ++pos_;
    std::string out;
    while (pos_ < src_.size()) {
      const char c = src_[pos_++];
      if (c == '"') {
        return out;
      }
      if (c == '\\') {
        if (pos_ >= src_.size()) return std::nullopt;
        const char esc = src_[pos_++];
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
            if (pos_ + 4 > src_.size()) return std::nullopt;
            uint32_t code = 0;
            for (int i = 0; i < 4; ++i) {
              const char h = src_[pos_++];
              code <<= 4;
              if (h >= '0' && h <= '9') {
                code |= static_cast<uint32_t>(h - '0');
              } else if (h >= 'a' && h <= 'f') {
                code |= static_cast<uint32_t>(10 + (h - 'a'));
              } else if (h >= 'A' && h <= 'F') {
                code |= static_cast<uint32_t>(10 + (h - 'A'));
              } else {
                return std::nullopt;
              }
            }
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
    if (src_.substr(pos_).starts_with("true")) {
      pos_ += 4;
      return true;
    }
    if (src_.substr(pos_).starts_with("false")) {
      pos_ += 5;
      return false;
    }
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
      ++pos_;
      SkipWhitespace();
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
      ++pos_;
      SkipWhitespace();
      if (Consume(']')) return true;
      while (true) {
        if (!SkipValue()) return false;
        if (Consume(']')) return true;
        if (!Consume(',')) return false;
      }
    }
    if (src_.substr(pos_).starts_with("true")) {
      pos_ += 4;
      return true;
    }
    if (src_.substr(pos_).starts_with("false")) {
      pos_ += 5;
      return true;
    }
    if (src_.substr(pos_).starts_with("null")) {
      pos_ += 4;
      return true;
    }
    // Number
    if (c == '-' || (c >= '0' && c <= '9')) {
      ++pos_;
      while (pos_ < src_.size()) {
        const char n = src_[pos_];
        if ((n >= '0' && n <= '9') || n == '.' || n == 'e' || n == 'E' ||
            n == '+' || n == '-') {
          ++pos_;
        } else {
          break;
        }
      }
      return true;
    }
    return false;
  }

 private:
  std::string_view src_;
  size_t pos_ = 0;
};

struct RawAsset {
  std::string name;
  std::string browser_download_url;
};

bool IsWindowsAssetName(std::string_view name) {
  const std::string lower = ToLowerAscii(name);
  if (lower.ends_with(".exe") || lower.ends_with(".msi")) {
    return true;
  }
  if (lower.ends_with(".zip") &&
      (lower.find("win") != std::string::npos ||
       lower.find("x64") != std::string::npos)) {
    return true;
  }
  return false;
}

}  // namespace

bool IsValidatedGitHubUrl(std::string_view url) {
  constexpr std::string_view kHttps = "https://";
  if (url.size() <= kHttps.size()) {
    return false;
  }
  if (ToLowerAscii(url.substr(0, kHttps.size())) != kHttps) {
    return false;
  }
  const std::string_view rest = url.substr(kHttps.size());
  const size_t slash = rest.find('/');
  std::string_view authority =
      slash == std::string_view::npos ? rest : rest.substr(0, slash);
  // Reject userinfo (`user@host`) and ports for strictness.
  if (authority.find('@') != std::string_view::npos ||
      authority.find(':') != std::string_view::npos || authority.empty()) {
    return false;
  }
  const std::string host = ToLowerAscii(authority);
  return host == "github.com" || host.ends_with(".github.com") ||
         host.ends_with(".githubusercontent.com");
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
