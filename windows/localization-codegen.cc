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

void EmitTable(std::ostream& out, std::string_view fn_name,
               const absl::flat_hash_map<std::string, std::string>& table) {
  std::vector<std::pair<std::string_view, std::string_view>> sorted;
  sorted.reserve(table.size());
  for (const auto& [k, v] : table) {
    sorted.emplace_back(k, v);
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  out << "inline const absl::flat_hash_map<std::string_view, std::wstring_view>&\n"
      << fn_name << "() {\n"
      << "  static const absl::NoDestructor<\n"
      << "      absl::flat_hash_map<std::string_view, std::wstring_view>>\n"
      << "      kMap({\n";
  for (const auto& [k, v] : sorted) {
    out << "          {\"" << EscapeCppString(k) << "\", L\""
        << EscapeCppString(v) << "\"},\n";
  }
  out << "      });\n"
      << "  return *kMap;\n"
      << "}\n\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6) {
    std::cerr << "Usage: " << argv[0]
              << " <en_base> <en_overrides> <vi_base> <vi_overrides> <out_h>\n";
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

  std::ofstream out(argv[5], std::ios::binary | std::ios::trunc);
  if (!out) {
    std::cerr << "localization_codegen: failed to write " << argv[5] << "\n";
    return 1;
  }

  out << "// Generated by //windows:localization_codegen — DO NOT EDIT.\n"
      << "#ifndef OKEYTRE_WINDOWS_LOCALIZATION_TABLES_GENERATED_H_\n"
      << "#define OKEYTRE_WINDOWS_LOCALIZATION_TABLES_GENERATED_H_\n\n"
      << "#include <string_view>\n\n"
      << "#include \"absl/base/no_destructor.h\"\n"
      << "#include \"absl/container/flat_hash_map.h\"\n\n"
      << "namespace okeytre::win::generated {\n\n";
  EmitTable(out, "EnglishStrings", en_table);
  EmitTable(out, "VietnameseStrings", vi_table);
  out << "}  // namespace okeytre::win::generated\n\n"
      << "#endif  // OKEYTRE_WINDOWS_LOCALIZATION_TABLES_GENERATED_H_\n";
  return out.good() ? 0 : 1;
}
