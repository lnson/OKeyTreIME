// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "inv-ast.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/substitute.h"
#include "simdutf.h"
#include "unicode-utils.h"

namespace okeytre {

std::u16string InvUtf8ToU16(std::string_view utf8) {
  std::u16string out(simdutf::utf16_length_from_utf8(utf8.data(), utf8.size()),
                     u'\0');
  const size_t written =
      simdutf::convert_utf8_to_utf16(utf8.data(), utf8.size(), out.data());
  out.resize(written);
  return out;
}

namespace {

bool IsValidIdentifier(std::string_view s) noexcept {
  if (s.empty()) {
    return false;
  }
  const unsigned char first = static_cast<unsigned char>(s[0]);
  if (!std::isalpha(first) && first != '_') {
    return false;
  }
  for (unsigned char ch : s) {
    if (!std::isalnum(ch) && ch != '_') {
      return false;
    }
  }
  return true;
}

}  // namespace

bool InvParseContext::DecodeQuotedString(const char* text, size_t len,
                                         int32_t tok_line,
                                         int32_t& out_str_idx) {
  if (text == nullptr || len < 2 || text[0] != '"' || text[len - 1] != '"') {
    SetError(tok_line, "expected double-quoted string literal");
    return false;
  }
  const std::string_view inner(text + 1, len - 2);
  std::string unescaped;
  unescaped.reserve(inner.size());
  size_t i = 0;
  while (i < inner.size()) {
    if (inner[i] == '\\') {
      if (i + 1 >= inner.size()) {
        SetError(tok_line, "dangling '\\' in string literal");
        return false;
      }
      const char next = inner[i + 1];
      switch (next) {
        case 'n':
          unescaped.push_back('\n');
          break;
        case 'r':
          unescaped.push_back('\r');
          break;
        case 't':
          unescaped.push_back('\t');
          break;
        default:
          unescaped.push_back(next);
          break;
      }
      i += 2;
    } else {
      unescaped.push_back(inner[i]);
      i += 1;
    }
  }
  out_str_idx = InternString(std::move(unescaped));
  return true;
}

bool InvParseContext::DecodeCharClass(const char* text, size_t len,
                                      int32_t tok_line, int32_t& out_pred_idx) {
  if (text == nullptr || len < 2 || text[0] != '[' || text[len - 1] != ']') {
    SetError(tok_line, "unterminated '[' character class");
    return false;
  }
  const std::u16string chars =
      InvUtf8ToU16(std::string_view(text + 1, len - 2));
  size_t pos = 0;
  bool negated = false;
  if (pos < chars.size() && chars[pos] == u'^') {
    negated = true;
    ++pos;
  }
  if (pos >= chars.size()) {
    SetError(tok_line, "empty '[' character class");
    return false;
  }

  absl::flat_hash_set<uint16_t> set;
  while (pos < chars.size()) {
    uint16_t first_char = 0;
    if (chars[pos] == u'\\') {
      ++pos;
      if (pos >= chars.size()) {
        SetError(tok_line, "dangling '\\' inside '[...]'");
        return false;
      }
      const uint16_t esc = static_cast<uint16_t>(chars[pos++]);
      if (esc == u's') {
        set.insert(0x0020);
        set.insert(0x0009);
        set.insert(0x000A);
        set.insert(0x000D);
        continue;
      }
      first_char = UnescapeChar(esc);
    } else {
      first_char = static_cast<uint16_t>(chars[pos++]);
    }

    if (pos + 1 < chars.size() && chars[pos] == u'-') {
      ++pos;  // skip '-'
      uint16_t end_char = 0;
      if (chars[pos] == u'\\') {
        ++pos;
        if (pos >= chars.size()) {
          SetError(tok_line, "dangling '\\' inside character range");
          return false;
        }
        end_char = UnescapeChar(static_cast<uint16_t>(chars[pos++]));
      } else {
        end_char = static_cast<uint16_t>(chars[pos++]);
      }
      if (first_char > end_char) {
        SetError(tok_line, "inverted character range in '[...]'");
        return false;
      }
      for (uint32_t code = first_char; code <= end_char; ++code) {
        set.insert(ToLower(static_cast<uint16_t>(code)));
      }
    } else {
      set.insert(ToLower(first_char));
    }
  }

  CharClassPred pred;
  pred.kind =
      negated ? CharClassPred::Kind::kNegative : CharClassPred::Kind::kPositive;
  pred.chars = std::move(set);
  out_pred_idx = InternPredicate(std::move(pred));
  return true;
}

bool InvParseContext::AddMacroDecl(int32_t name_str_idx, int32_t expr_idx,
                                   int32_t tok_line) {
  const std::string name(
      absl::StripAsciiWhitespace(strings[static_cast<size_t>(name_str_idx)]));
  if (!IsValidIdentifier(name)) {
    SetError(tok_line, absl::Substitute("invalid macro identifier '$0'", name));
    return false;
  }
  const InvExprNode& ast = expr_nodes[static_cast<size_t>(expr_idx)];
  if (ast.kind == InvExprNode::Kind::kEmpty) {
    SetError(tok_line,
             absl::Substitute("macro '$0' cannot have an empty pattern", name));
    return false;
  }
  auto [it, inserted] = spec.macros.try_emplace(name, ast);
  if (!inserted) {
    SetError(tok_line, absl::Substitute("duplicate macro '$0'", name));
    return false;
  }
  return true;
}

bool InvParseContext::AddPatternRule(bool anchor_start, int32_t expr_idx,
                                     bool anchor_end, int32_t tok_line) {
  const InvExprNode& ast = expr_nodes[static_cast<size_t>(expr_idx)];
  if (ast.kind == InvExprNode::Kind::kEmpty && !anchor_start && !anchor_end) {
    SetError(tok_line, "pattern in section 2 cannot be empty");
    return false;
  }
  spec.rules.push_back(InvPatternRule{
      .expr = ast,
      .line = tok_line,
      .anchor_start = anchor_start,
      .anchor_end = anchor_end,
  });
  return true;
}

bool InvParseContext::AddAssertion(bool expect_match, int32_t word_str_idx,
                                   int32_t tok_line) {
  spec.assertions.push_back(InvTestAssertion{
      .word_utf8 = strings[static_cast<size_t>(word_str_idx)],
      .line = tok_line,
      .expect_match = expect_match,
  });
  return true;
}

}  // namespace okeytre

// Flex / Bison entry points declared with C++ linkage.
int invc_yyparse(okeytre::InvParseContext* ctx);
void InvcLexerBeginScan(const char* utf8_source);
void InvcLexerEndScan();

namespace okeytre {

absl::StatusOr<ParsedInvSpec> ParseInvSpec(std::string_view source) {
  InvParseContext ctx;
  std::string terminated_source(source);
  if (terminated_source.empty() || terminated_source.back() != '\n') {
    terminated_source.push_back('\n');
  }

  InvcLexerBeginScan(terminated_source.c_str());
  const int parse_rc = invc_yyparse(&ctx);
  InvcLexerEndScan();

  if (ctx.has_error) {
    return absl::InvalidArgumentError(
        absl::Substitute("line $0: $1", ctx.error_line, ctx.error_message));
  }
  if (parse_rc != 0) {
    return absl::InvalidArgumentError(absl::Substitute(
        "line $0: syntax error in .inv specification", ctx.last_token_line));
  }
  if (ctx.spec.rules.empty()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "line $0: specification must contain at least one pattern in section 2",
        ctx.line));
  }
  return std::move(ctx.spec);
}

}  // namespace okeytre
