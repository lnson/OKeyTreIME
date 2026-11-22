// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "rule-ast.h"

#include <algorithm>
#include <cctype>

#include "absl/strings/ascii.h"

namespace okeytre {

namespace {

bool IsValidIdentifier(std::string_view s) {
  if (s.empty()) return false;
  unsigned char first = static_cast<unsigned char>(s[0]);
  if (!std::isalpha(first) && first != '_') return false;
  for (unsigned char ch : s) {
    if (!std::isalnum(ch) && ch != '_') return false;
  }
  return true;
}

}  // namespace

// CharacterMap

bool CharacterMap::Make1D(
    std::string_view name,
    const std::vector<std::pair<std::vector<uint16_t>, uint16_t>>& pairs,
    int line, CharacterMap& out_map, RuleCompileError& out_err) {
  absl::flat_hash_map<uint16_t, uint16_t> forward;
  absl::flat_hash_map<uint16_t, uint16_t> inverse;

  for (const auto& [sources, raw_target] : pairs) {
    if (sources.empty()) continue;
    uint16_t canonical_base = sources[0];
    uint16_t target = ToLower(raw_target);
    uint16_t lower_canonical = ToLower(canonical_base);
    for (uint16_t src : sources) {
      uint16_t lower_src = ToLower(src);
      forward[lower_src] = target;
    }
    if (target != lower_canonical) {
      inverse[target] = lower_canonical;
    }
  }

  if (forward.empty()) {
    out_err = RuleCompileError{
        line,
        absl::Substitute("Map '$0' must contain at least one mapping.", name)};
    return false;
  }

  out_map.name = std::string(name);
  out_map.is_2d = false;
  out_map.keys.clear();
  out_map.forward_1d = std::move(forward);
  out_map.inverse_all = std::move(inverse);
  out_map.forward_by_key.clear();
  out_map.inverse_by_key.clear();
  out_map.domain_set.clear();
  for (const auto& [k, v] : out_map.forward_1d) {
    out_map.domain_set.insert(k);
  }
  out_map.inverse_domain_set.clear();
  for (const auto& [k, v] : out_map.inverse_all) {
    out_map.inverse_domain_set.insert(k);
  }
  return true;
}

bool CharacterMap::Make2D(
    std::string_view name, const std::vector<uint16_t>& raw_keys,
    const std::vector<std::pair<std::vector<uint16_t>, std::vector<uint16_t>>>&
        rows,
    int line, CharacterMap& out_map, RuleCompileError& out_err) {
  std::vector<uint16_t> lower_keys;
  lower_keys.reserve(raw_keys.size());
  for (uint16_t k : raw_keys) {
    lower_keys.push_back(ToLower(k));
  }
  if (lower_keys.empty()) {
    out_err = RuleCompileError{
        line, absl::Substitute("2D map '$0' requires at least one key.", name)};
    return false;
  }

  absl::flat_hash_map<uint16_t, absl::flat_hash_map<uint16_t, uint16_t>>
      forward_by_key;
  absl::flat_hash_map<uint16_t, absl::flat_hash_map<uint16_t, uint16_t>>
      inverse_by_key;
  for (uint16_t k : lower_keys) {
    forward_by_key[k] = {};
    inverse_by_key[k] = {};
  }
  absl::flat_hash_map<uint16_t, uint16_t> inverse_all;
  absl::flat_hash_set<uint16_t> domain_set;

  for (const auto& [inputs, outputs] : rows) {
    if (inputs.empty()) continue;
    if (outputs.size() != lower_keys.size()) {
      out_err = RuleCompileError{
          line, absl::Substitute("Map '$0' row has $1 outputs, expected $2.",
                                 name, outputs.size(), lower_keys.size())};
      return false;
    }
    uint16_t lower_canonical = ToLower(inputs[0]);
    std::vector<uint16_t> lower_inputs;
    lower_inputs.reserve(inputs.size());
    for (uint16_t inp : inputs) {
      uint16_t li = ToLower(inp);
      lower_inputs.push_back(li);
      domain_set.insert(li);
    }

    for (size_t col_idx = 0; col_idx < lower_keys.size(); ++col_idx) {
      uint16_t key = lower_keys[col_idx];
      uint16_t lower_out = ToLower(outputs[col_idx]);
      for (uint16_t inp : lower_inputs) {
        forward_by_key[key][inp] = lower_out;
      }
      if (lower_out != lower_canonical) {
        inverse_by_key[key][lower_out] = lower_canonical;
        inverse_all[lower_out] = lower_canonical;
      }
    }
  }

  if (domain_set.empty()) {
    out_err = RuleCompileError{
        line,
        absl::Substitute("Map '$0' must contain at least one mapping.", name)};
    return false;
  }

  out_map.name = std::string(name);
  out_map.is_2d = true;
  out_map.keys = std::move(lower_keys);
  out_map.forward_1d.clear();
  out_map.inverse_all = std::move(inverse_all);
  out_map.forward_by_key = std::move(forward_by_key);
  out_map.inverse_by_key = std::move(inverse_by_key);
  out_map.domain_set = std::move(domain_set);
  out_map.inverse_domain_set.clear();
  for (const auto& [k, v] : out_map.inverse_all) {
    out_map.inverse_domain_set.insert(k);
  }
  return true;
}

std::optional<uint16_t> CharacterMap::Apply(
    uint16_t lower_char, bool inverted,
    std::optional<uint16_t> lower_input_key) const {
  if (inverted) {
    if (is_2d && lower_input_key.has_value()) {
      auto kit = inverse_by_key.find(*lower_input_key);
      if (kit != inverse_by_key.end()) {
        auto cit = kit->second.find(lower_char);
        if (cit != kit->second.end()) {
          return cit->second;
        }
      }
    }
    auto it = inverse_all.find(lower_char);
    if (it != inverse_all.end()) {
      return it->second;
    }
    return std::nullopt;
  } else {
    if (is_2d && lower_input_key.has_value()) {
      auto kit = forward_by_key.find(*lower_input_key);
      if (kit != forward_by_key.end()) {
        auto cit = kit->second.find(lower_char);
        if (cit != kit->second.end()) {
          return cit->second;
        }
      }
      return std::nullopt;
    }
    auto it = forward_1d.find(lower_char);
    if (it != forward_1d.end()) {
      return it->second;
    }
    return std::nullopt;
  }
}

// ParseContext Token Helpers

bool ParseContext::DecodeQuotedString(const char* text, size_t len,
                                      int tok_line, int& out_str_idx) {
  if (len < 2 || text[0] != '"' || text[len - 1] != '"') {
    SetError(tok_line, "Expected double-quoted string.");
    return false;
  }
  std::string_view inner(text + 1, len - 2);
  std::string unescaped;
  unescaped.reserve(inner.size());
  size_t i = 0;
  while (i < inner.size()) {
    if (inner[i] == '\\' && i + 1 < inner.size()) {
      char next = inner[i + 1];
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

bool ParseContext::DecodeCharLiteral(const char* text, size_t len, int tok_line,
                                     uint16_t& out_char) {
  if (len < 3 || text[0] != '\'' || text[len - 1] != '\'') {
    SetError(tok_line, "Invalid character literal.");
    return false;
  }
  std::u16string inner = Utf8ToU16(std::string_view(text + 1, len - 2));
  if (inner.size() == 1) {
    out_char = static_cast<uint16_t>(inner[0]);
    return true;
  }
  if (inner.size() == 2 && inner[0] == u'\\') {
    out_char = UnescapeChar(static_cast<uint16_t>(inner[1]));
    return true;
  }
  SetError(tok_line, absl::Substitute("Invalid character literal '$0'.",
                                      std::string_view(text, len)));
  return false;
}

bool ParseContext::DecodeCharClass(const char* text, size_t len, int tok_line,
                                   int& out_pred_idx) {
  if (len < 2 || text[0] != '[' || text[len - 1] != ']') {
    SetError(tok_line, "Unclosed '[' character class.");
    return false;
  }
  std::u16string chars = Utf8ToU16(std::string_view(text + 1, len - 2));
  size_t pos = 0;
  bool negated = false;
  if (pos < chars.size() && chars[pos] == u'^') {
    negated = true;
    ++pos;
  }

  absl::flat_hash_set<uint16_t> set;
  while (pos < chars.size()) {
    uint16_t c = static_cast<uint16_t>(chars[pos]);
    uint16_t first_char = 0;
    if (c == u'\\') {
      ++pos;
      if (pos >= chars.size()) {
        SetError(tok_line, "Trailing '\\' inside '[...]'.");
        return false;
      }
      uint16_t esc = static_cast<uint16_t>(chars[pos++]);
      if (esc == u's') {
        if (!negated) {
          set.insert(0x0020);
          set.insert(0x0009);
          set.insert(0x000A);
          set.insert(0x000D);
        }
        continue;
      }
      first_char = UnescapeChar(esc);
    } else {
      first_char = c;
      ++pos;
    }

    // Character range lo-hi
    if (pos + 1 < chars.size() && chars[pos] == u'-') {
      ++pos;  // consume '-'
      uint16_t end_char = 0;
      if (chars[pos] == u'\\') {
        ++pos;
        if (pos >= chars.size()) {
          SetError(tok_line, "Trailing '\\' in character range.");
          return false;
        }
        end_char = UnescapeChar(static_cast<uint16_t>(chars[pos++]));
      } else {
        end_char = static_cast<uint16_t>(chars[pos++]);
      }
      uint16_t lo = std::min(first_char, end_char);
      uint16_t hi = std::max(first_char, end_char);
      for (uint32_t code = lo; code <= hi; ++code) {
        set.insert(ToLower(static_cast<uint16_t>(code)));
      }
    } else {
      set.insert(ToLower(first_char));
    }
  }

  out_pred_idx =
      InternPredicate(negated ? CharPredicate::Negative(std::move(set))
                              : CharPredicate::Positive(std::move(set)));
  return true;
}

bool ParseContext::DecodeMapCharList(const char* text, size_t len, int tok_line,
                                     int& out_list_idx) {
  if (len < 2 || text[0] != '[' || text[len - 1] != ']') {
    SetError(tok_line, "Expected '[...]' in map row.");
    return false;
  }
  std::u16string u16 = Utf8ToU16(std::string_view(text + 1, len - 2));
  if (u16.empty()) {
    SetError(tok_line, "Empty character list '[]' in map row.");
    return false;
  }
  std::vector<uint16_t> chars(u16.begin(), u16.end());
  out_list_idx = InternCharList(std::move(chars));
  return true;
}

// ParseContext Semantic Builders

bool ParseContext::ParseBraceRefToRegex(int str_idx, int tok_line,
                                        int& out_regex_idx) {
  std::string raw_ref(
      absl::StripAsciiWhitespace(strings[static_cast<size_t>(str_idx)]));
  std::string s = raw_ref;
  bool inverted = false;
  if (!s.empty() && s[0] == '!') {
    inverted = true;
    s = std::string(absl::StripAsciiWhitespace(std::string_view(s).substr(1)));
  }

  bool key_bound = false;
  size_t open_paren = s.find('(');
  if (open_paren != std::string::npos) {
    if (s.back() != ')') {
      SetError(tok_line,
               absl::Substitute("Malformed macro reference '{$0}'.", raw_ref));
      return false;
    }
    std::string_view arg = absl::StripAsciiWhitespace(
        std::string_view(s).substr(open_paren + 1, s.size() - open_paren - 2));
    if (arg != "$0" && arg != "$key") {
      SetError(tok_line,
               absl::Substitute("Expected '$$0' inside '{$0}'.", raw_ref));
      return false;
    }
    key_bound = true;
    s = std::string(
        absl::StripAsciiWhitespace(std::string_view(s).substr(0, open_paren)));
  }

  if (s.empty()) {
    SetError(tok_line, "Empty '{}' reference in pattern.");
    return false;
  }

  out_regex_idx =
      InternRegex(RegexAST::MakeMacroRef(s, inverted, key_bound, tok_line));
  return true;
}

bool ParseContext::ResolveInputBraceRef(int str_idx, int tok_line,
                                        int& out_pred_idx) {
  std::string name(
      absl::StripAsciiWhitespace(strings[static_cast<size_t>(str_idx)]));
  auto macro_it = spec.macros.find(name);
  if (macro_it != spec.macros.end()) {
    if (macro_it->second.kind != RegexAST::Kind::kPredicate) {
      SetError(tok_line, absl::Substitute("Input macro '{$0}' must resolve to "
                                          "a single character class.",
                                          name));
      return false;
    }
    out_pred_idx = InternPredicate(macro_it->second.predicate);
    return true;
  }

  auto map_it = spec.maps.find(name);
  if (map_it != spec.maps.end()) {
    if (map_it->second.is_2d) {
      absl::flat_hash_set<uint16_t> key_set(map_it->second.keys.begin(),
                                            map_it->second.keys.end());
      out_pred_idx =
          InternPredicate(CharPredicate::Positive(std::move(key_set)));
    } else {
      out_pred_idx =
          InternPredicate(CharPredicate::Positive(map_it->second.domain_set));
    }
    return true;
  }

  SetError(tok_line, absl::Substitute("Unknown macro or map '{$0}' in input "
                                      "specification.",
                                      name));
  return false;
}

bool ParseContext::ParseMapProjectionRHS(int str_idx, int tok_line,
                                         int& out_rhs_item_idx) {
  std::string expr(
      absl::StripAsciiWhitespace(strings[static_cast<size_t>(str_idx)]));
  std::string rest = expr;
  bool inverted = false;
  if (!rest.empty() && rest[0] == '!') {
    inverted = true;
    rest = std::string(
        absl::StripAsciiWhitespace(std::string_view(rest).substr(1)));
  }

  size_t open_paren = rest.find('(');
  if (open_paren == std::string::npos || rest.empty() || rest.back() != ')') {
    SetError(tok_line, absl::Substitute("Invalid map projection '{$0}'; "
                                        "expected '{MAP($$n)}'.",
                                        expr));
    return false;
  }

  std::string map_name(
      absl::StripAsciiWhitespace(std::string_view(rest).substr(0, open_paren)));
  auto map_it = spec.maps.find(map_name);
  if (map_it == spec.maps.end()) {
    SetError(tok_line,
             absl::Substitute("Unknown map '$0' in replacement '{$1}'.",
                              map_name, expr));
    return false;
  }

  std::string args_str =
      rest.substr(open_paren + 1, rest.size() - open_paren - 2);
  std::vector<std::string> args;
  size_t start = 0;
  while (start <= args_str.size()) {
    size_t comma = args_str.find(',', start);
    if (comma == std::string::npos) {
      args.emplace_back(
          absl::StripAsciiWhitespace(std::string_view(args_str).substr(start)));
      break;
    } else {
      args.emplace_back(absl::StripAsciiWhitespace(
          std::string_view(args_str).substr(start, comma - start)));
      start = comma + 1;
    }
  }

  if (args.empty() || args.size() > 2) {
    SetError(tok_line, absl::Substitute("Map projection '{$0}' expects 1 or 2 "
                                        "arguments.",
                                        expr));
    return false;
  }

  int cap_idx = -1;
  if (args[0].size() >= 2 && args[0][0] == '$') {
    try {
      cap_idx = std::stoi(args[0].substr(1));
    } catch (...) {
      cap_idx = -1;
    }
  }
  if (cap_idx < 1 || cap_idx > current_capture_count) {
    int max_cap = std::max(1, current_capture_count);
    SetError(tok_line,
             absl::Substitute("First argument of '{$0}' must be a valid "
                              "capture group '$$1'..'$1'.",
                              expr, absl::Substitute("$$$0", max_cap)));
    return false;
  }

  bool uses_input_key = false;
  if (args.size() == 2) {
    if (args[1] != "$0" && args[1] != "$key") {
      SetError(tok_line, absl::Substitute(
                             "Second argument of '{$0}' must be '$$0'.", expr));
      return false;
    }
    uses_input_key = true;
  } else if (!inverted && map_it->second.is_2d) {
    uses_input_key = true;
  }

  out_rhs_item_idx = InternRHSItem(
      RHSItem::MakeMapProjection(map_name, inverted, cap_idx, uses_input_key));
  return true;
}

bool ParseContext::AddFlagDecl(int name_str_idx, std::optional<int> val_str_idx,
                               int tok_line) {
  std::string name(
      absl::StripAsciiWhitespace(strings[static_cast<size_t>(name_str_idx)]));
  if (!IsValidIdentifier(name)) {
    SetError(tok_line, absl::Substitute("Invalid flag identifier '$0'.", name));
    return false;
  }
  bool default_value = false;
  if (val_str_idx.has_value()) {
    std::string val_str = absl::AsciiStrToLower(
        absl::StripAsciiWhitespace(strings[static_cast<size_t>(*val_str_idx)]));
    if (val_str == "true" || val_str == "1") {
      default_value = true;
    } else if (val_str == "false" || val_str == "0") {
      default_value = false;
    } else {
      SetError(tok_line,
               absl::Substitute("Invalid boolean default '$0' in '%flag $1'; "
                                "expected 'true' or 'false'.",
                                val_str, name));
      return false;
    }
  }
  for (const FlagDecl& f : spec.flags) {
    if (f.name == name) {
      SetError(tok_line, absl::Substitute(
                             "Duplicate '%flag' declaration for '$0'.", name));
      return false;
    }
  }
  spec.flags.push_back(FlagDecl{name, default_value, tok_line});
  return true;
}

bool ParseContext::AddMap1D(int name_str_idx, int entries_list_idx,
                            int tok_line) {
  std::string name(
      absl::StripAsciiWhitespace(strings[static_cast<size_t>(name_str_idx)]));
  if (!IsValidIdentifier(name)) {
    SetError(tok_line, absl::Substitute("Invalid map name '$0'.", name));
    return false;
  }
  const std::vector<MapRowEntry>& rows =
      map_entry_lists[static_cast<size_t>(entries_list_idx)];
  std::vector<std::pair<std::vector<uint16_t>, uint16_t>> pairs;
  pairs.reserve(rows.size());
  for (const MapRowEntry& r : rows) {
    if (r.outputs.size() != 1) {
      SetError(tok_line,
               absl::Substitute(
                   "Expected single target character in '%map $0'.", name));
      return false;
    }
    pairs.emplace_back(r.inputs, r.outputs[0]);
  }
  CharacterMap map;
  RuleCompileError err;
  if (!CharacterMap::Make1D(name, pairs, tok_line, map, err)) {
    SetError(err.line, err.message);
    return false;
  }
  spec.maps[name] = std::move(map);
  return true;
}

bool ParseContext::AddMap2D(int name_str_idx, int keys_list_idx,
                            int entries_list_idx, int tok_line) {
  std::string name(
      absl::StripAsciiWhitespace(strings[static_cast<size_t>(name_str_idx)]));
  if (!IsValidIdentifier(name)) {
    SetError(tok_line, absl::Substitute("Invalid map name '$0'.", name));
    return false;
  }
  const std::vector<uint16_t>& keys =
      char_lists[static_cast<size_t>(keys_list_idx)];
  const std::vector<MapRowEntry>& entry_rows =
      map_entry_lists[static_cast<size_t>(entries_list_idx)];
  std::vector<std::pair<std::vector<uint16_t>, std::vector<uint16_t>>> rows;
  rows.reserve(entry_rows.size());
  for (const MapRowEntry& r : entry_rows) {
    rows.emplace_back(r.inputs, r.outputs);
  }
  CharacterMap map;
  RuleCompileError err;
  if (!CharacterMap::Make2D(name, keys, rows, tok_line, map, err)) {
    SetError(err.line, err.message);
    return false;
  }
  spec.maps[name] = std::move(map);
  return true;
}

bool ParseContext::AddMacroDecl(int name_str_idx, int regex_idx, int tok_line) {
  std::string name(
      absl::StripAsciiWhitespace(strings[static_cast<size_t>(name_str_idx)]));
  if (!IsValidIdentifier(name)) {
    SetError(tok_line,
             absl::Substitute("Invalid macro identifier '$0'.", name));
    return false;
  }
  const RegexAST& ast = regex_nodes[static_cast<size_t>(regex_idx)];
  if (ast.kind == RegexAST::Kind::kEmpty) {
    SetError(tok_line, absl::Substitute(
                           "Macro '$0' cannot have an empty pattern.", name));
    return false;
  }
  spec.macros[name] = ast;
  return true;
}

bool ParseContext::BeginRule(std::optional<int> flag_str_idx, bool anchored,
                             int tok_line) {
  capturing_groups_enabled = true;
  current_capture_count = 0;
  current_rule_line = tok_line;
  current_rule_anchored = anchored;
  current_rule_flag.clear();

  if (flag_str_idx.has_value()) {
    std::string flag_name(absl::StripAsciiWhitespace(
        strings[static_cast<size_t>(*flag_str_idx)]));
    if (!IsValidIdentifier(flag_name)) {
      SetError(tok_line,
               absl::Substitute("Invalid flag identifier '?$0' in rule.",
                                flag_name));
      return false;
    }
    bool declared = false;
    for (const FlagDecl& f : spec.flags) {
      if (f.name == flag_name) {
        declared = true;
        break;
      }
    }
    if (!declared) {
      SetError(tok_line,
               absl::Substitute("Undeclared flag '?$0'; declare it in Section "
                                "1 with '%flag $0'.",
                                flag_name));
      return false;
    }
    current_rule_flag = std::move(flag_name);
  }
  return true;
}

bool ParseContext::FinishRule(int lhs_regex_idx, int input_pred_idx,
                              int rhs_list_idx, int tok_line) {
  const RegexAST& lhs = regex_nodes[static_cast<size_t>(lhs_regex_idx)];
  if (lhs.kind == RegexAST::Kind::kEmpty) {
    SetError(tok_line, "Left-hand pattern cannot be empty.");
    return false;
  }
  const std::vector<RHSItem>& rhs =
      rhs_lists[static_cast<size_t>(rhs_list_idx)];
  if (rhs.empty()) {
    SetError(tok_line, "Right-hand replacement cannot be empty.");
    return false;
  }

  RuleDecl decl;
  decl.line = current_rule_line;
  decl.flag_name = current_rule_flag;
  decl.is_anchored = current_rule_anchored;
  decl.lhs = lhs;
  decl.capture_count = current_capture_count;
  decl.input_predicate = predicates[static_cast<size_t>(input_pred_idx)];
  decl.rhs = rhs;
  spec.rules.push_back(std::move(decl));

  capturing_groups_enabled = false;
  current_capture_count = 0;
  return true;
}

bool ParseContext::AddTestVector(int flag_set_idx, int existing_str_idx,
                                 uint16_t input_key, int expected_str_idx,
                                 int tok_line) {
  const absl::flat_hash_set<std::string>& enabled_flags =
      flag_sets[static_cast<size_t>(flag_set_idx)];
  for (const std::string& candidate : enabled_flags) {
    bool declared = false;
    for (const FlagDecl& f : spec.flags) {
      if (f.name == candidate) {
        declared = true;
        break;
      }
    }
    if (!declared) {
      SetError(
          tok_line,
          absl::Substitute("Undeclared flag '?$0' in test vector.", candidate));
      return false;
    }
  }

  RuleTestVector vec;
  vec.line = tok_line;
  vec.enabled_flags = enabled_flags;
  vec.existing_text = strings[static_cast<size_t>(existing_str_idx)];
  vec.caret_position = -1;
  vec.input_key = input_key;
  vec.expected_output = strings[static_cast<size_t>(expected_str_idx)];
  spec.tests.push_back(std::move(vec));
  return true;
}

}  // namespace okeytre

// Flex / Bison entry points declared with C++ linkage
int okeytre_yyparse(okeytre::ParseContext* ctx);
void OkeytreLexerBeginScan(const char* utf8_source);
void OkeytreLexerEndScan();

namespace okeytre {

bool ParseGrammarSpec(std::string_view source, GrammarSpec& out_spec,
                      RuleCompileError& out_error) {
  ParseContext ctx;
  // Ensure a trailing newline so line-oriented rules at EOF always terminate
  // cleanly.
  std::string terminated_source(source);
  if (terminated_source.empty() || terminated_source.back() != '\n') {
    terminated_source.push_back('\n');
  }

  OkeytreLexerBeginScan(terminated_source.c_str());
  int parse_rc = okeytre_yyparse(&ctx);
  OkeytreLexerEndScan();

  if (ctx.has_error) {
    out_error = ctx.error;
    return false;
  }
  if (parse_rc != 0) {
    out_error =
        RuleCompileError{ctx.line, "Syntax error in .ime specification."};
    return false;
  }
  out_spec = std::move(ctx.spec);
  return true;
}

}  // namespace okeytre
