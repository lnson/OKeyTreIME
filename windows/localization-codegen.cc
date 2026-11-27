// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only
//
// Build-time code generator that merges Apple `.strings` localization files
// (base + Windows overrides) into a sorted constexpr C++ lookup header.

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace {

bool ReadFile(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::cerr << "localization_codegen: failed to open " << path << "\n";
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  // Strip optional UTF-8 BOM.
  if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
      static_cast<unsigned char>(out[1]) == 0xBB &&
      static_cast<unsigned char>(out[2]) == 0xBF) {
    out.erase(0, 3);
  }
  return true;
}

void SkipWhitespaceAndComments(std::string_view src, size_t& pos) {
  while (pos < src.size()) {
    if (std::isspace(static_cast<unsigned char>(src[pos])) != 0) {
      ++pos;
      continue;
    }
    if (pos + 1 < src.size() && src[pos] == '/' && src[pos + 1] == '*') {
      pos += 2;
      while (pos + 1 < src.size() &&
             !(src[pos] == '*' && src[pos + 1] == '/')) {
        ++pos;
      }
      if (pos + 1 < src.size()) {
        pos += 2;
      } else {
        pos = src.size();
      }
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
  if (pos >= src.size() || src[pos] != '"') {
    return false;
  }
  ++pos;
  out.clear();
  while (pos < src.size()) {
    const char c = src[pos++];
    if (c == '"') {
      return true;
    }
    if (c == '\\') {
      if (pos >= src.size()) {
        return false;
      }
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

bool ParseStringsFile(const std::string& path,
                      absl::flat_hash_map<std::string, std::string>& table) {
  std::string content;
  if (!ReadFile(path, content)) {
    return false;
  }
  std::string_view src(content);
  size_t pos = 0;
  while (true) {
    SkipWhitespaceAndComments(src, pos);
    if (pos >= src.size()) {
      return true;
    }
    std::string key;
    if (!ParseQuotedString(src, pos, key)) {
      std::cerr << "localization_codegen: expected quoted key in " << path
                << " at offset " << pos << "\n";
      return false;
    }
    SkipWhitespaceAndComments(src, pos);
    if (pos >= src.size() || src[pos] != '=') {
      std::cerr << "localization_codegen: expected '=' in " << path
                << " at offset " << pos << "\n";
      return false;
    }
    ++pos;
    SkipWhitespaceAndComments(src, pos);
    std::string value;
    if (!ParseQuotedString(src, pos, value)) {
      std::cerr << "localization_codegen: expected quoted value in " << path
                << " at offset " << pos << "\n";
      return false;
    }
    SkipWhitespaceAndComments(src, pos);
    if (pos >= src.size() || src[pos] != ';') {
      std::cerr << "localization_codegen: expected ';' in " << path
                << " at offset " << pos << "\n";
      return false;
    }
    ++pos;
    table.insert_or_assign(std::move(key), std::move(value));
  }
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
    if (std::isalnum(c) != 0) {
      out.push_back(cap ? static_cast<char>(std::toupper(c))
                        : static_cast<char>(c));
      cap = false;
    } else {
      cap = true;
    }
  }
  return out;
}

void EmitStringArray(
    std::ostream& out, std::string_view name,
    const std::vector<std::string>& sorted_keys,
    const absl::flat_hash_map<std::string, std::string>& primary,
    const absl::flat_hash_map<std::string, std::string>& fallback) {
  out << "inline constexpr std::array<std::wstring_view,\n"
      << "                            static_cast<size_t>(L10nKey::kCount)>\n"
      << "    " << name << " = {\n";
  for (const std::string& key : sorted_keys) {
    std::string_view val = key;
    if (const auto it = primary.find(key); it != primary.end()) {
      val = it->second;
    } else if (const auto fb = fallback.find(key); fb != fallback.end()) {
      val = fb->second;
    }
    out << "        L\"" << EscapeCppString(val) << "\",\n";
  }
  out << "};\n\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 7) {
    std::cerr << "Usage: " << argv[0]
              << " <en_base> <en_overrides> <vi_base> <vi_overrides>"
              << " <out_keys_h> <out_tables_h>\n";
    return 1;
  }

  absl::flat_hash_map<std::string, std::string> en_table;
  absl::flat_hash_map<std::string, std::string> vi_table;
  if (!ParseStringsFile(argv[1], en_table) ||
      !ParseStringsFile(argv[2], en_table) ||
      !ParseStringsFile(argv[3], vi_table) ||
      !ParseStringsFile(argv[4], vi_table)) {
    return 1;
  }

  absl::flat_hash_set<std::string> key_set;
  key_set.reserve(en_table.size() + vi_table.size());
  for (const auto& [k, _] : en_table) key_set.insert(k);
  for (const auto& [k, _] : vi_table) key_set.insert(k);

  std::vector<std::string> sorted_keys(key_set.begin(), key_set.end());
  std::sort(sorted_keys.begin(), sorted_keys.end());

  absl::flat_hash_set<std::string> seen_enums;
  std::vector<std::string> enum_names;
  enum_names.reserve(sorted_keys.size());
  for (const std::string& key : sorted_keys) {
    std::string enum_name = KeyToEnumName(key);
    if (!seen_enums.insert(enum_name).second) {
      std::cerr << "localization_codegen: duplicate enum identifier "
                << enum_name << " for key \"" << key << "\"\n";
      return 1;
    }
    enum_names.push_back(std::move(enum_name));
  }

  std::ofstream keys_out(argv[5], std::ios::binary | std::ios::trunc);
  if (!keys_out) {
    std::cerr << "localization_codegen: failed to write " << argv[5] << "\n";
    return 1;
  }
  keys_out << "// Generated by //windows:localization_codegen — DO NOT EDIT.\n"
           << "#ifndef OKEYTRE_WINDOWS_LOCALIZATION_KEYS_GENERATED_H_\n"
           << "#define OKEYTRE_WINDOWS_LOCALIZATION_KEYS_GENERATED_H_\n\n"
           << "#include <cstdint>\n\n"
           << "namespace okeytre::win {\n\n"
           << "enum class L10nKey : uint16_t {\n";
  for (const std::string& name : enum_names) {
    keys_out << "  " << name << ",\n";
  }
  keys_out << "  kCount,\n"
           << "};\n\n"
           << "}  // namespace okeytre::win\n\n"
           << "#endif  // OKEYTRE_WINDOWS_LOCALIZATION_KEYS_GENERATED_H_\n";

  std::ofstream tables_out(argv[6], std::ios::binary | std::ios::trunc);
  if (!tables_out) {
    std::cerr << "localization_codegen: failed to write " << argv[6] << "\n";
    return 1;
  }
  tables_out << "// Generated by //windows:localization_codegen — DO NOT EDIT.\n"
             << "#ifndef OKEYTRE_WINDOWS_LOCALIZATION_TABLES_GENERATED_H_\n"
             << "#define OKEYTRE_WINDOWS_LOCALIZATION_TABLES_GENERATED_H_\n\n"
             << "#include <array>\n"
             << "#include <cstddef>\n"
             << "#include <string_view>\n\n"
             << "#include \"windows/localization-keys.generated.h\"\n\n"
             << "namespace okeytre::win::generated {\n\n";
  EmitStringArray(tables_out, "kEnglishStrings", sorted_keys, en_table,
                  en_table);
  EmitStringArray(tables_out, "kVietnameseStrings", sorted_keys, vi_table,
                  en_table);
  tables_out << "}  // namespace okeytre::win::generated\n\n"
             << "#endif  // OKEYTRE_WINDOWS_LOCALIZATION_TABLES_GENERATED_H_\n";

  return (keys_out.good() && tables_out.good()) ? 0 : 1;
}
