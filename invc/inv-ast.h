// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Abstract Syntax Tree (AST) and Flex/Bison ParseContext for `.inv`
// specifications.

#ifndef OKEYTRE_INVC_INV_AST_H_
#define OKEYTRE_INVC_INV_AST_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "inv-compiler.h"

namespace okeytre {

// Character-class predicate over lowercase BMP UTF-16 code units.
struct CharClassPred {
  enum class Kind : uint8_t {
    kAny,
    kPositive,
    kNegative,
  };

  absl::flat_hash_set<uint16_t> chars;
  Kind kind = Kind::kAny;

  bool MatchesLower(uint16_t lower) const {
    switch (kind) {
      case Kind::kAny:
        return true;
      case Kind::kPositive:
        return chars.contains(lower);
      case Kind::kNegative:
        return !chars.contains(lower);
    }
    return false;
  }
};

// Parsed regular expression AST node prior to macro expansion and
// equivalence-class compilation.
struct InvExprNode {
  enum class Kind : uint8_t {
    kEmpty,
    kCharClass,
    kMacroRef,
    kConcat,
    kAlt,
    kStar,
    kPlus,
    kQuest,
  };

  std::vector<InvExprNode> children;
  // Populated during equivalence-class compilation when `kind == kCharClass`.
  std::vector<uint8_t> ec_mask;
  std::string macro_name;
  CharClassPred pred;
  int32_t line = 1;
  Kind kind = Kind::kEmpty;

  static InvExprNode MakeEmpty(int32_t line = 1) {
    InvExprNode n;
    n.line = line;
    n.kind = Kind::kEmpty;
    return n;
  }

  static InvExprNode MakeCharClass(CharClassPred pred, int32_t line) {
    InvExprNode n;
    n.pred = std::move(pred);
    n.line = line;
    n.kind = Kind::kCharClass;
    return n;
  }

  static InvExprNode MakeMacroRef(std::string name, int32_t line) {
    InvExprNode n;
    n.macro_name = std::move(name);
    n.line = line;
    n.kind = Kind::kMacroRef;
    return n;
  }

  static InvExprNode MakeConcat(std::vector<InvExprNode> items, int32_t line) {
    if (items.empty()) {
      return MakeEmpty(line);
    }
    if (items.size() == 1) {
      return std::move(items[0]);
    }
    InvExprNode n;
    n.children = std::move(items);
    n.line = line;
    n.kind = Kind::kConcat;
    return n;
  }

  static InvExprNode MakeAlt(std::vector<InvExprNode> branches, int32_t line) {
    if (branches.empty()) {
      return MakeEmpty(line);
    }
    if (branches.size() == 1) {
      return std::move(branches[0]);
    }
    InvExprNode n;
    n.children = std::move(branches);
    n.line = line;
    n.kind = Kind::kAlt;
    return n;
  }

  static InvExprNode MakeQuantified(Kind quant_kind, InvExprNode sub,
                                    int32_t line) {
    InvExprNode n;
    n.children.push_back(std::move(sub));
    n.line = line;
    n.kind = quant_kind;
    return n;
  }
};

// Single disallowed pattern declaration from Section 2 (`[^]<regex>[$]`).
struct InvPatternRule {
  InvExprNode expr;
  int32_t line = 1;
  bool anchor_start = false;
  bool anchor_end = false;
};

// Complete parsed `.inv` specification (`definitions %% patterns %% tests`).
struct ParsedInvSpec {
  std::string name;
  absl::flat_hash_map<std::string, InvExprNode> macros;
  std::vector<InvPatternRule> rules;
  std::vector<InvTestAssertion> assertions;
};

// Trivially-copyable POD semantic value (`YYSTYPE`) shared by Flex and Bison.
// Complex AST nodes and strings live in `InvParseContext` arenas and are
// referenced by integer handle.
struct InvSemanticValue {
  int32_t node_idx;
  int32_t list_idx;
  int32_t line;
  uint16_t char_val;
  bool bool_val;
};

// Arena and parse state passed to `invc_yylex` and `invc_yyparse`.
struct InvParseContext {
  ParsedInvSpec spec;
  std::string error_message;

  std::vector<std::string> strings;
  std::vector<CharClassPred> predicates;
  std::vector<InvExprNode> expr_nodes;
  std::vector<std::vector<InvExprNode>> expr_lists;

  int32_t line = 1;
  int32_t last_token_line = 1;
  int32_t error_line = 1;
  int32_t section_count = 0;
  int32_t comment_start_line = 1;
  int32_t comment_caller_state = 0;
  bool has_error = false;

  void SetError(int32_t err_line, std::string_view msg) {
    if (!has_error) {
      has_error = true;
      error_line = err_line;
      error_message = std::string(msg);
    }
  }

  int32_t InternString(std::string s) {
    strings.push_back(std::move(s));
    return static_cast<int32_t>(strings.size() - 1);
  }

  int32_t InternPredicate(CharClassPred p) {
    predicates.push_back(std::move(p));
    return static_cast<int32_t>(predicates.size() - 1);
  }

  int32_t InternExpr(InvExprNode r) {
    expr_nodes.push_back(std::move(r));
    return static_cast<int32_t>(expr_nodes.size() - 1);
  }

  int32_t NewExprList(InvExprNode first) {
    expr_lists.push_back({std::move(first)});
    return static_cast<int32_t>(expr_lists.size() - 1);
  }

  static uint16_t UnescapeChar(uint16_t c) noexcept {
    switch (c) {
      case 0x006E /* n */:
        return 0x000A;
      case 0x0072 /* r */:
        return 0x000D;
      case 0x0074 /* t */:
        return 0x0009;
      default:
        return c;
    }
  }

  // Token decoding helpers called by Flex actions.
  bool DecodeQuotedString(const char* text, size_t len, int32_t tok_line,
                          int32_t& out_str_idx);
  bool DecodeCharClass(const char* text, size_t len, int32_t tok_line,
                       int32_t& out_pred_idx);

  // Semantic validation & AST building helpers called by Bison actions.
  bool AddMacroDecl(int32_t name_str_idx, int32_t expr_idx, int32_t tok_line);
  bool AddPatternRule(bool anchor_start, int32_t expr_idx, bool anchor_end,
                      int32_t tok_line);
  bool AddAssertion(bool expect_match, int32_t word_str_idx, int32_t tok_line);
};

// Parses a `.inv` specification string via the Flex lexer and Bison parser.
absl::StatusOr<ParsedInvSpec> ParseInvSpec(std::string_view source);

}  // namespace okeytre

#ifndef YYSTYPE
#define YYSTYPE okeytre::InvSemanticValue
#define YYSTYPE_IS_DECLARED 1
#define YYSTYPE_IS_TRIVIAL 1
#endif

#endif  // OKEYTRE_INVC_INV_AST_H_
