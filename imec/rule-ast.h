// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Abstract Syntax Tree (AST) and Flex/Bison ParseContext for `.ime`
// specifications.

#ifndef OKEYTRE_ENGINE_RULEAST_HPP_
#define OKEYTRE_ENGINE_RULEAST_HPP_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/substitute.h"
#include "unicode-utils.h"
#include "utf8-utils.h"

namespace okeytre {

// Diagnostic error produced when lexing, parsing, or compiling a `.ime`
// specification.
struct RuleCompileError {
  int line = 1;
  std::string message;

  std::string Description() const {
    return absl::Substitute("Line $0: $1", line, message);
  }
};

// Character-level predicate used in regular expressions (matched
// case-insensitively on word characters).
struct CharPredicate {
  enum class Kind {
    kAnyWordChar,
    kPositive,
    kNegative,
  };

  Kind kind = Kind::kAnyWordChar;
  absl::flat_hash_set<uint16_t> chars;

  static CharPredicate AnyWordChar() {
    return CharPredicate{.kind = Kind::kAnyWordChar};
  }

  static CharPredicate Positive(absl::flat_hash_set<uint16_t> s) {
    return CharPredicate{.kind = Kind::kPositive, .chars = std::move(s)};
  }

  static CharPredicate Negative(absl::flat_hash_set<uint16_t> s) {
    return CharPredicate{.kind = Kind::kNegative, .chars = std::move(s)};
  }

  bool operator==(const CharPredicate& other) const {
    return kind == other.kind && chars == other.chars;
  }

  bool Contains(uint16_t lower_char) const {
    switch (kind) {
      case Kind::kAnyWordChar:
        return true;
      case Kind::kPositive:
        return chars.contains(lower_char);
      case Kind::kNegative:
        return !chars.contains(lower_char);
    }
    return false;
  }

  void ReferencedCharacters(absl::flat_hash_set<uint16_t>& universe) const {
    if (kind == Kind::kPositive || kind == Kind::kNegative) {
      universe.insert(chars.begin(), chars.end());
    }
  }
};

// Algebraic 1D or 2D (key-parameterized) character projection map declared
// with `%map` in Section 1.
struct CharacterMap {
  std::string name;
  bool is_2d = false;
  std::vector<uint16_t> keys;

  // 1D forward lookup: lowercase source -> lowercase target
  absl::flat_hash_map<uint16_t, uint16_t> forward_1d;
  // Inverse lookup (`!MAP`): lowercase target -> canonical lowercase base
  absl::flat_hash_map<uint16_t, uint16_t> inverse_all;

  // 2D forward lookup: lowercase trigger key -> (lowercase source -> lowercase
  // target)
  absl::flat_hash_map<uint16_t, absl::flat_hash_map<uint16_t, uint16_t>>
      forward_by_key;
  // 2D inverse lookup (`!MAP($0)`): lowercase trigger key -> (lowercase target
  // -> canonical lowercase base)
  absl::flat_hash_map<uint16_t, absl::flat_hash_map<uint16_t, uint16_t>>
      inverse_by_key;

  // All lowercase source characters accepted by forward `{MAP}` lookups.
  absl::flat_hash_set<uint16_t> domain_set;
  // All lowercase target characters accepted by inverse `{!MAP}` lookups
  // (keys of `inverse_all`, excluding identity mappings).
  absl::flat_hash_set<uint16_t> inverse_domain_set;

  static bool Make1D(
      std::string_view name,
      const std::vector<std::pair<std::vector<uint16_t>, uint16_t>>& pairs,
      int line, CharacterMap& out_map, RuleCompileError& out_err);

  static bool Make2D(
      std::string_view name, const std::vector<uint16_t>& raw_keys,
      const std::vector<
          std::pair<std::vector<uint16_t>, std::vector<uint16_t>>>& rows,
      int line, CharacterMap& out_map, RuleCompileError& out_err);

  std::optional<uint16_t> Apply(uint16_t lower_char, bool inverted,
                                std::optional<uint16_t> lower_input_key) const;
};

// Parsed regular expression AST node prior to macro expansion and
// equivalence-class compilation.
struct RegexAST {
  enum class Kind {
    kEmpty,
    kPredicate,
    kMacroRef,
    kConcat,
    kAlternation,
    kQuantifier,
    kGroup,
    kCapture,
  };

  Kind kind = Kind::kEmpty;
  CharPredicate predicate;
  std::string macro_name;
  bool macro_inverted = false;
  bool macro_key_bound = false;
  int line = 1;
  std::vector<RegexAST> children;
  int quant_min = 0;
  int quant_max = -1;  // -1 means unbounded (nil)
  int capture_index = 0;

  static RegexAST MakeEmpty() {
    RegexAST n;
    n.kind = Kind::kEmpty;
    return n;
  }

  static RegexAST MakePredicate(CharPredicate p) {
    RegexAST n;
    n.kind = Kind::kPredicate;
    n.predicate = std::move(p);
    return n;
  }

  static RegexAST MakeMacroRef(std::string name, bool inverted, bool key_bound,
                               int line) {
    RegexAST n;
    n.kind = Kind::kMacroRef;
    n.macro_name = std::move(name);
    n.macro_inverted = inverted;
    n.macro_key_bound = key_bound;
    n.line = line;
    return n;
  }

  static RegexAST MakeConcat(std::vector<RegexAST> items) {
    if (items.empty()) return MakeEmpty();
    if (items.size() == 1) return std::move(items[0]);
    RegexAST n;
    n.kind = Kind::kConcat;
    n.children = std::move(items);
    return n;
  }

  static RegexAST MakeAlternation(std::vector<RegexAST> branches) {
    if (branches.empty()) return MakeEmpty();
    if (branches.size() == 1) return std::move(branches[0]);
    RegexAST n;
    n.kind = Kind::kAlternation;
    n.children = std::move(branches);
    return n;
  }

  static RegexAST MakeQuantifier(RegexAST sub, int min_count, int max_count) {
    RegexAST n;
    n.kind = Kind::kQuantifier;
    n.children.push_back(std::move(sub));
    n.quant_min = min_count;
    n.quant_max = max_count;
    return n;
  }

  static RegexAST MakeGroup(RegexAST sub) {
    RegexAST n;
    n.kind = Kind::kGroup;
    n.children.push_back(std::move(sub));
    return n;
  }

  static RegexAST MakeCapture(int index, RegexAST sub) {
    RegexAST n;
    n.kind = Kind::kCapture;
    n.capture_index = index;
    n.children.push_back(std::move(sub));
    return n;
  }
};

// Semantic replacement token on the right-hand side of `-->`.
struct RHSItem {
  enum class Kind {
    kLiteral,
    kInputKey,
    kCapture,
    kMapProjection,
  };

  Kind kind = Kind::kLiteral;
  uint16_t literal_char = 0;
  int capture_index = 0;
  std::string map_name;
  bool inverted = false;
  bool uses_input_key = false;

  static RHSItem MakeLiteral(uint16_t c) {
    RHSItem item;
    item.kind = Kind::kLiteral;
    item.literal_char = c;
    return item;
  }

  static RHSItem MakeInputKey() {
    RHSItem item;
    item.kind = Kind::kInputKey;
    return item;
  }

  static RHSItem MakeCapture(int idx) {
    RHSItem item;
    item.kind = Kind::kCapture;
    item.capture_index = idx;
    return item;
  }

  static RHSItem MakeMapProjection(std::string map_name, bool inverted,
                                   int capture_idx, bool uses_key) {
    RHSItem item;
    item.kind = Kind::kMapProjection;
    item.map_name = std::move(map_name);
    item.inverted = inverted;
    item.capture_index = capture_idx;
    item.uses_input_key = uses_key;
    return item;
  }
};

// Boolean feature flag declared with `%flag <name> = <true|false>` in
// Section 1.
struct FlagDecl {
  std::string name;
  bool default_value = false;
  int line = 1;
};

// Single parsed rewrite rule from Section 2
// (`[?flag] [anchor]<lhs> + <input> --> <rhs>`).
struct RuleDecl {
  int line = 1;
  std::string flag_name;  // empty if unguarded
  bool is_anchored = false;
  RegexAST lhs;
  int capture_count = 0;
  CharPredicate input_predicate;
  std::vector<RHSItem> rhs;
};

// Optional inline verification test vector from Section 3
// (`[?flag] "input" + 'k' --> "expected"`).
struct RuleTestVector {
  int line = 1;
  absl::flat_hash_set<std::string> enabled_flags;
  std::string existing_text;
  int caret_position = -1;  // -1 means end of existing_text
  uint16_t input_key = 0;
  std::string expected_output;
};

// Complete parsed `.ime` grammar specification
// (`definitions %% rules %% tests`).
struct GrammarSpec {
  std::string name;
  bool has_name = false;
  std::vector<FlagDecl> flags;
  absl::flat_hash_map<std::string, RegexAST> macros;
  absl::flat_hash_map<std::string, CharacterMap> maps;
  std::vector<RuleDecl> rules;
  std::vector<RuleTestVector> tests;
};

// Flex / Bison Shared Token IDs, Semantic Value & ParseContext

#ifndef TOK_SECTION_SEP
enum RuleTokenKind : int {
  TOK_SECTION_SEP = 258,
  TOK_DIR_NAME = 259,
  TOK_DIR_FLAG = 260,
  TOK_DIR_MAP = 261,
  TOK_IDENT = 262,
  TOK_STRING = 263,
  TOK_CHAR_LIT = 264,
  TOK_UCHAR = 265,
  TOK_CHAR_LIST = 266,
  TOK_CHAR_CLASS = 267,
  TOK_BRACE_REF = 268,
  TOK_LPAREN = 269,
  TOK_LPAREN_NC = 270,
  TOK_RPAREN = 271,
  TOK_LBRACE = 272,
  TOK_RBRACE = 273,
  TOK_PIPE = 274,
  TOK_STAR = 275,
  TOK_PLUS = 276,
  TOK_QUEST = 277,
  TOK_DOT = 278,
  TOK_CARET = 279,
  TOK_FLAG_GUARD = 280,
  TOK_RULE_PLUS = 281,
  TOK_ARROW = 282,
  TOK_EQ = 283,
  TOK_COLON = 284,
  TOK_COMMA = 285,
  TOK_MAP_SEP = 286,
  TOK_EOL = 287,
  TOK_RHS_INPUT_KEY = 288,
  TOK_RHS_CAPTURE = 289,
  TOK_RHS_PROJECTION = 290,
  TOK_LEX_ERROR = 291,
};
#endif

// Trivially-copyable POD semantic value (`YYSTYPE`) shared by Flex and Bison.
// Complex objects are stored in `ParseContext` arenas and referenced by integer
// index.
struct RuleSemanticValue {
  int int_val;
  uint16_t char_val;
  bool bool_val;
  int node_idx;
  int list_idx;
  int line;
};

struct MapRowEntry {
  std::vector<uint16_t> inputs;
  std::vector<uint16_t> outputs;
};

// Arena and state carrier passed to `okeytre_yylex` and `okeytre_yyparse`.
struct ParseContext {
  GrammarSpec spec;
  bool has_error = false;
  RuleCompileError error;

  int line = 1;
  int last_token_line = 1;
  int section_count = 0;
  int comment_start_line = 1;
  int comment_caller_state = 0;
  int map_start_line = 1;

  // Rule / Macro LHS state
  bool capturing_groups_enabled = false;
  int current_capture_count = 0;
  int current_rule_line = 1;
  std::string current_rule_flag;
  bool current_rule_anchored = false;

  // Arenas for POD YYSTYPE handles
  std::vector<std::string> strings;
  std::vector<CharPredicate> predicates;
  std::vector<RegexAST> regex_nodes;
  std::vector<std::vector<RegexAST>> regex_lists;
  std::vector<std::vector<uint16_t>> char_lists;
  std::vector<MapRowEntry> map_entries;
  std::vector<std::vector<MapRowEntry>> map_entry_lists;
  std::vector<RHSItem> rhs_items;
  std::vector<std::vector<RHSItem>> rhs_lists;
  std::vector<absl::flat_hash_set<std::string>> flag_sets;

  void SetError(int err_line, std::string_view msg) {
    if (!has_error) {
      has_error = true;
      error.line = err_line;
      error.message = std::string(msg);
    }
  }

  int InternString(std::string s) {
    strings.push_back(std::move(s));
    return static_cast<int>(strings.size() - 1);
  }

  int InternPredicate(CharPredicate p) {
    predicates.push_back(std::move(p));
    return static_cast<int>(predicates.size() - 1);
  }

  int InternRegex(RegexAST r) {
    regex_nodes.push_back(std::move(r));
    return static_cast<int>(regex_nodes.size() - 1);
  }

  int NewRegexList(RegexAST first) {
    regex_lists.push_back({std::move(first)});
    return static_cast<int>(regex_lists.size() - 1);
  }

  int NewEmptyRegexList() {
    regex_lists.emplace_back();
    return static_cast<int>(regex_lists.size() - 1);
  }

  int InternCharList(std::vector<uint16_t> list) {
    char_lists.push_back(std::move(list));
    return static_cast<int>(char_lists.size() - 1);
  }

  int InternMapEntry(std::vector<uint16_t> lhs, std::vector<uint16_t> rhs) {
    map_entries.push_back(MapRowEntry{std::move(lhs), std::move(rhs)});
    return static_cast<int>(map_entries.size() - 1);
  }

  int NewMapEntryList(int entry_idx) {
    map_entry_lists.push_back({map_entries[static_cast<size_t>(entry_idx)]});
    return static_cast<int>(map_entry_lists.size() - 1);
  }

  int InternRHSItem(RHSItem item) {
    rhs_items.push_back(std::move(item));
    return static_cast<int>(rhs_items.size() - 1);
  }

  int NewRHSList(int item_idx) {
    rhs_lists.push_back({rhs_items[static_cast<size_t>(item_idx)]});
    return static_cast<int>(rhs_lists.size() - 1);
  }

  int NewFlagSet() {
    flag_sets.emplace_back();
    return static_cast<int>(flag_sets.size() - 1);
  }

  // Token decoding helpers called by Flex actions
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

  bool DecodeQuotedString(const char* text, size_t len, int tok_line,
                          int& out_str_idx);
  bool DecodeCharLiteral(const char* text, size_t len, int tok_line,
                         uint16_t& out_char);
  bool DecodeCharClass(const char* text, size_t len, int tok_line,
                       int& out_pred_idx);
  bool DecodeMapCharList(const char* text, size_t len, int tok_line,
                         int& out_list_idx);

  // Semantic validation & AST building helpers called by Bison actions
  bool ParseBraceRefToRegex(int str_idx, int tok_line, int& out_regex_idx);
  bool ResolveInputBraceRef(int str_idx, int tok_line, int& out_pred_idx);
  bool ParseMapProjectionRHS(int str_idx, int tok_line, int& out_rhs_item_idx);
  bool AddFlagDecl(int name_str_idx, std::optional<int> val_str_idx,
                   int tok_line);
  bool AddMap1D(int name_str_idx, int entries_list_idx, int tok_line);
  bool AddMap2D(int name_str_idx, int keys_list_idx, int entries_list_idx,
                int tok_line);
  bool AddMacroDecl(int name_str_idx, int regex_idx, int tok_line);
  bool BeginRule(std::optional<int> flag_str_idx, bool anchored, int tok_line);
  bool FinishRule(int lhs_regex_idx, int input_pred_idx, int rhs_list_idx,
                  int tok_line);
  bool AddTestVector(int flag_set_idx, int existing_str_idx, uint16_t input_key,
                     int expected_str_idx, int tok_line);
};

// Parses a complete `.ime` specification string using the Flex lexer and Bison
// parser.
bool ParseGrammarSpec(std::string_view source, GrammarSpec& out_spec,
                      RuleCompileError& out_error);

}  // namespace okeytre

#ifndef YYSTYPE
#define YYSTYPE okeytre::RuleSemanticValue
#define YYSTYPE_IS_DECLARED 1
#define YYSTYPE_IS_TRIVIAL 1
#endif

#endif  // OKEYTRE_ENGINE_RULEAST_HPP_
