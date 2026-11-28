// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "l10n-compiler.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/cord.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"

namespace okeytre {

namespace {

void SkipWhitespaceAndComments(std::string_view src, size_t& pos) {
  while (pos < src.size()) {
    if (absl::ascii_isspace(static_cast<unsigned char>(src[pos]))) {
      ++pos;
      continue;
    }
    if (pos + 1 < src.size() && src[pos] == '/' && src[pos + 1] == '*') {
      pos += 2;
      while (pos + 1 < src.size() &&
             !(src[pos] == '*' && src[pos + 1] == '/')) {
        ++pos;
      }
      pos = (pos + 1 < src.size()) ? (pos + 2) : src.size();
      continue;
    }
    if (pos + 1 < src.size() && src[pos] == '/' && src[pos + 1] == '/') {
      pos += 2;
      while (pos < src.size() && src[pos] != '\n') {
        ++pos;
      }
      continue;
    }
    break;
  }
}

bool ParseQuotedString(std::string_view src, size_t& pos, std::string& out) {
  if (pos >= src.size() || src[pos] != '"') return false;
  ++pos;
  out.clear();
  while (pos < src.size()) {
    const char c = src[pos++];
    if (c == '"') return true;
    if (c == '\\') {
      if (pos >= src.size()) return false;
      const char esc = src[pos++];
      switch (esc) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
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
        default:
          out.push_back(esc);
          break;
      }
    } else {
      out.push_back(c);
    }
  }
  return false;
}

std::string EscapeCppString(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

std::string KeyToEnumName(std::string_view key) {
  std::string out = "k";
  out.reserve(key.size() + 1);
  bool cap = true;
  for (unsigned char c : key) {
    if (absl::ascii_isalnum(c)) {
      out.push_back(cap ? absl::ascii_toupper(c) : static_cast<char>(c));
      cap = false;
    } else {
      cap = true;
    }
  }
  return out;
}

void EmitStringArray(
    absl::Cord& out, std::string_view name,
    const std::vector<std::string>& sorted_keys,
    const absl::flat_hash_map<std::string, std::string>& primary,
    const absl::flat_hash_map<std::string, std::string>& fallback) {
  out.Append(absl::Substitute(
      "inline constexpr std::array<std::wstring_view,\n"
      "                            static_cast<size_t>(L10nKey::kCount)>\n"
      "    $0 = {\n",
      name));
  for (const std::string& key : sorted_keys) {
    std::string_view val = key;
    if (const auto it = primary.find(key); it != primary.end()) {
      val = it->second;
    } else if (const auto fb = fallback.find(key); fb != fallback.end()) {
      val = fb->second;
    }
    out.Append(absl::Substitute("        L\"$0\",\n", EscapeCppString(val)));
  }
  out.Append("};\n\n");
}

}  // namespace

absl::Status ParseStringsSource(
    std::string_view source, std::string_view source_name,
    absl::flat_hash_map<std::string, std::string>& table) {
  // Strip optional UTF-8 BOM.
  source = absl::StripPrefix(source, "\xEF\xBB\xBF");

  size_t pos = 0;
  while (true) {
    SkipWhitespaceAndComments(source, pos);
    if (pos >= source.size()) return absl::OkStatus();

    std::string key;
    if (!ParseQuotedString(source, pos, key)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "l10nc error: expected quoted key in $0 at offset $1", source_name,
          pos));
    }
    SkipWhitespaceAndComments(source, pos);
    if (pos >= source.size() || source[pos] != '=') {
      return absl::InvalidArgumentError(absl::Substitute(
          "l10nc error: expected '=' in $0 at offset $1", source_name, pos));
    }
    ++pos;
    SkipWhitespaceAndComments(source, pos);
    std::string value;
    if (!ParseQuotedString(source, pos, value)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "l10nc error: expected quoted value in $0 at offset $1", source_name,
          pos));
    }
    SkipWhitespaceAndComments(source, pos);
    if (pos >= source.size() || source[pos] != ';') {
      return absl::InvalidArgumentError(absl::Substitute(
          "l10nc error: expected ';' in $0 at offset $1", source_name, pos));
    }
    ++pos;
    table.insert_or_assign(std::move(key), std::move(value));
  }
}

absl::StatusOr<CompiledL10nCatalog> CompileL10nCatalog(
    std::string_view en_base, std::string_view en_overrides,
    std::string_view vi_base, std::string_view vi_overrides) {
  CompiledL10nCatalog catalog;
  ABSL_RETURN_IF_ERROR(
      ParseStringsSource(en_base, "en_base", catalog.en_table));
  ABSL_RETURN_IF_ERROR(
      ParseStringsSource(en_overrides, "en_overrides", catalog.en_table));
  ABSL_RETURN_IF_ERROR(
      ParseStringsSource(vi_base, "vi_base", catalog.vi_table));
  ABSL_RETURN_IF_ERROR(
      ParseStringsSource(vi_overrides, "vi_overrides", catalog.vi_table));

  absl::flat_hash_set<std::string> key_set;
  key_set.reserve(catalog.en_table.size() + catalog.vi_table.size());
  for (const auto& [k, _] : catalog.en_table) key_set.insert(k);
  for (const auto& [k, _] : catalog.vi_table) key_set.insert(k);

  catalog.sorted_keys.assign(key_set.begin(), key_set.end());
  std::sort(catalog.sorted_keys.begin(), catalog.sorted_keys.end());

  absl::flat_hash_set<std::string> seen_enums;
  catalog.enum_names.reserve(catalog.sorted_keys.size());
  for (const std::string& key : catalog.sorted_keys) {
    std::string enum_name = KeyToEnumName(key);
    if (!seen_enums.insert(enum_name).second) {
      return absl::InvalidArgumentError(absl::Substitute(
          "l10nc error: duplicate enum identifier $0 for key \"$1\"", enum_name,
          key));
    }
    catalog.enum_names.push_back(std::move(enum_name));
  }

  return catalog;
}

absl::Cord GenerateCppL10nKeysHeader(const CompiledL10nCatalog& catalog) {
  absl::Cord out;
  out.Append(
      "// Generated by //l10nc:l10nc — DO NOT EDIT.\n"
      "#ifndef OKEYTRE_WINDOWS_LOCALIZATION_KEYS_GENERATED_H_\n"
      "#define OKEYTRE_WINDOWS_LOCALIZATION_KEYS_GENERATED_H_\n\n"
      "#include <cstdint>\n\n"
      "namespace okeytre::win {\n\n"
      "enum class L10nKey : uint16_t {\n");
  for (const std::string& name : catalog.enum_names) {
    out.Append(absl::Substitute("  $0,\n", name));
  }
  out.Append(
      "  kCount,\n"
      "};\n\n"
      "}  // namespace okeytre::win\n\n"
      "#endif  // OKEYTRE_WINDOWS_LOCALIZATION_KEYS_GENERATED_H_\n");
  return out;
}

absl::Cord GenerateCppL10nTablesHeader(const CompiledL10nCatalog& catalog) {
  absl::Cord out;
  out.Append(
      "// Generated by //l10nc:l10nc — DO NOT EDIT.\n"
      "#ifndef OKEYTRE_WINDOWS_LOCALIZATION_TABLES_GENERATED_H_\n"
      "#define OKEYTRE_WINDOWS_LOCALIZATION_TABLES_GENERATED_H_\n\n"
      "#include <array>\n"
      "#include <cstddef>\n"
      "#include <string_view>\n\n"
      "#include \"windows/localization-keys.generated.h\"\n\n"
      "namespace okeytre::win::generated {\n\n");
  EmitStringArray(out, "kEnglishStrings", catalog.sorted_keys, catalog.en_table,
                  catalog.en_table);
  EmitStringArray(out, "kVietnameseStrings", catalog.sorted_keys,
                  catalog.vi_table, catalog.en_table);
  out.Append(
      "}  // namespace okeytre::win::generated\n\n"
      "#endif  // OKEYTRE_WINDOWS_LOCALIZATION_TABLES_GENERATED_H_\n");
  return out;
}

}  // namespace okeytre
