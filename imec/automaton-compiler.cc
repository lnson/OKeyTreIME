// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "automaton-compiler.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/substitute.h"

namespace okeytre {

namespace {

struct ExpandedRuleSpec {
  std::string flag_name;
  RegexAST lhs;
  CharPredicate trigger_predicate;
  std::vector<RHSItem> rhs;
  int priority = 0;
  int line = 1;
  int capture_count = 0;
  bool is_anchored = false;
};

struct CompiledRegex {
  enum class Kind {
    // Matches the empty string (epsilon).
    kEmpty,
    // Matches a single character belonging to a set of equivalence classes.
    kAtom,
    // Sequential concatenation of child sub-expressions.
    kConcat,
    // Disjunctive alternation (`|`) across child branches.
    kAlt,
    // Bounded or unbounded repetition (`*`, `+`, `?`, `{m,n}`) of a child.
    kRepeatNode,
    // Numbered capturing group (`(...)`) around a child sub-expression.
    kCapture,
  };

  Kind kind = Kind::kEmpty;
  ECBitSet bits;
  int atom_slot = -1;  // -1 if none
  std::vector<CompiledRegex> children;
  int repeat_min = 0;
  int repeat_max = -1;  // -1 if unbounded
  int capture_index = 0;

  static CompiledRegex MakeEmpty() {
    CompiledRegex r;
    r.kind = Kind::kEmpty;
    return r;
  }
  static CompiledRegex MakeAtom(ECBitSet b, int slot) {
    CompiledRegex r;
    r.kind = Kind::kAtom;
    r.bits = std::move(b);
    r.atom_slot = slot;
    return r;
  }
  static CompiledRegex MakeConcat(std::vector<CompiledRegex> items) {
    CompiledRegex r;
    r.kind = Kind::kConcat;
    r.children = std::move(items);
    return r;
  }
  static CompiledRegex MakeAlt(std::vector<CompiledRegex> branches) {
    CompiledRegex r;
    r.kind = Kind::kAlt;
    r.children = std::move(branches);
    return r;
  }
  static CompiledRegex MakeRepeat(CompiledRegex sub, int min_c, int max_c) {
    CompiledRegex r;
    r.kind = Kind::kRepeatNode;
    r.children.push_back(std::move(sub));
    r.repeat_min = min_c;
    r.repeat_max = max_c;
    return r;
  }
  static CompiledRegex MakeCapture(int idx, CompiledRegex sub) {
    CompiledRegex r;
    r.kind = Kind::kCapture;
    r.capture_index = idx;
    r.children.push_back(std::move(sub));
    return r;
  }
};

// Macro Expansion & Universe Collection

bool ContainsKeyBoundMacro(const RegexAST& ast) {
  switch (ast.kind) {
    case RegexAST::Kind::kEmpty:
    case RegexAST::Kind::kPredicate:
      return false;
    case RegexAST::Kind::kMacroRef:
      return ast.macro_key_bound;
    case RegexAST::Kind::kConcat:
    case RegexAST::Kind::kAlternation:
      for (const RegexAST& c : ast.children) {
        if (ContainsKeyBoundMacro(c)) return true;
      }
      return false;
    case RegexAST::Kind::kQuantifier:
    case RegexAST::Kind::kGroup:
    case RegexAST::Kind::kCapture:
      return !ast.children.empty() && ContainsKeyBoundMacro(ast.children[0]);
  }
  return false;
}

absl::flat_hash_set<uint16_t> ResolveTriggerKeys(
    const CharPredicate& pred, const absl::flat_hash_set<uint16_t>& universe) {
  switch (pred.kind) {
    case CharPredicate::Kind::kPositive:
      return pred.chars;
    case CharPredicate::Kind::kNegative: {
      absl::flat_hash_set<uint16_t> res;
      for (uint16_t c : universe) {
        if (!pred.chars.contains(c)) {
          res.insert(c);
        }
      }
      return res;
    }
    case CharPredicate::Kind::kAnyWordChar:
      return universe;
  }
  return {};
}

bool ExpandMacros(const RegexAST& ast, const GrammarSpec& spec,
                  std::optional<uint16_t> bound_input_key,
                  absl::flat_hash_set<std::string>& visiting, RegexAST& out_ast,
                  RuleCompileError& out_err) {
  switch (ast.kind) {
    case RegexAST::Kind::kEmpty:
    case RegexAST::Kind::kPredicate:
      out_ast = ast;
      return true;

    case RegexAST::Kind::kMacroRef: {
      auto map_it = spec.maps.find(ast.macro_name);
      if (map_it != spec.maps.end()) {
        const CharacterMap& map = map_it->second;
        if (ast.macro_key_bound) {
          if (!ast.macro_inverted) {
            out_err = RuleCompileError{
                ast.line, absl::Substitute("Only inverted '{!$0($$0)}' is "
                                           "supported on LHS.",
                                           ast.macro_name)};
            return false;
          }
          absl::flat_hash_set<uint16_t> inverse_domain_for_key;
          if (bound_input_key.has_value()) {
            auto kit = map.inverse_by_key.find(*bound_input_key);
            if (kit != map.inverse_by_key.end()) {
              for (const auto& [k, v] : kit->second) {
                inverse_domain_for_key.insert(k);
              }
            }
          }
          out_ast = RegexAST::MakePredicate(
              CharPredicate::Positive(std::move(inverse_domain_for_key)));
          return true;
        }
        const absl::flat_hash_set<uint16_t>& set =
            ast.macro_inverted ? map.inverse_domain_set : map.domain_set;
        out_ast = RegexAST::MakePredicate(CharPredicate::Positive(set));
        return true;
      }

      if (ast.macro_inverted || ast.macro_key_bound) {
        out_err = RuleCompileError{
            ast.line,
            absl::Substitute("'{$0$1}' is not a declared '%map'.",
                             ast.macro_inverted ? "!" : "", ast.macro_name)};
        return false;
      }

      auto macro_it = spec.macros.find(ast.macro_name);
      if (macro_it == spec.macros.end()) {
        out_err = RuleCompileError{
            ast.line,
            absl::Substitute("Undefined macro or map '{$0}'.", ast.macro_name)};
        return false;
      }

      auto [visiting_it, inserted] = visiting.emplace(ast.macro_name);
      if (!inserted) {
        out_err = RuleCompileError{ast.line,
                                   absl::Substitute("Cyclic macro reference "
                                                    "involving '{$0}'.",
                                                    ast.macro_name)};
        return false;
      }
      RegexAST expanded;
      if (!ExpandMacros(macro_it->second, spec, bound_input_key, visiting,
                        expanded, out_err)) {
        return false;
      }
      visiting.erase(visiting_it);
      out_ast = RegexAST::MakeGroup(std::move(expanded));
      return true;
    }

    case RegexAST::Kind::kConcat: {
      std::vector<RegexAST> items;
      items.reserve(ast.children.size());
      for (const RegexAST& c : ast.children) {
        RegexAST exp;
        if (!ExpandMacros(c, spec, bound_input_key, visiting, exp, out_err)) {
          return false;
        }
        items.push_back(std::move(exp));
      }
      out_ast = RegexAST::MakeConcat(std::move(items));
      return true;
    }

    case RegexAST::Kind::kAlternation: {
      std::vector<RegexAST> branches;
      branches.reserve(ast.children.size());
      for (const RegexAST& c : ast.children) {
        RegexAST exp;
        if (!ExpandMacros(c, spec, bound_input_key, visiting, exp, out_err)) {
          return false;
        }
        branches.push_back(std::move(exp));
      }
      out_ast = RegexAST::MakeAlternation(std::move(branches));
      return true;
    }

    case RegexAST::Kind::kQuantifier: {
      RegexAST exp;
      if (!ExpandMacros(ast.children[0], spec, bound_input_key, visiting, exp,
                        out_err)) {
        return false;
      }
      out_ast = RegexAST::MakeQuantifier(std::move(exp), ast.quant_min,
                                         ast.quant_max);
      return true;
    }

    case RegexAST::Kind::kGroup: {
      RegexAST exp;
      if (!ExpandMacros(ast.children[0], spec, bound_input_key, visiting, exp,
                        out_err)) {
        return false;
      }
      out_ast = RegexAST::MakeGroup(std::move(exp));
      return true;
    }

    case RegexAST::Kind::kCapture: {
      RegexAST exp;
      if (!ExpandMacros(ast.children[0], spec, bound_input_key, visiting, exp,
                        out_err)) {
        return false;
      }
      out_ast = RegexAST::MakeCapture(ast.capture_index, std::move(exp));
      return true;
    }
  }
  return true;
}

void CollectUniverse(const RegexAST& ast,
                     absl::flat_hash_set<uint16_t>& universe) {
  switch (ast.kind) {
    case RegexAST::Kind::kEmpty:
    case RegexAST::Kind::kMacroRef:
      break;
    case RegexAST::Kind::kPredicate:
      ast.predicate.ReferencedCharacters(universe);
      break;
    case RegexAST::Kind::kConcat:
    case RegexAST::Kind::kAlternation:
    case RegexAST::Kind::kQuantifier:
    case RegexAST::Kind::kGroup:
    case RegexAST::Kind::kCapture:
      for (const RegexAST& c : ast.children) {
        CollectUniverse(c, universe);
      }
      break;
  }
}

void CollectPredicates(const RegexAST& ast, std::vector<CharPredicate>& list) {
  switch (ast.kind) {
    case RegexAST::Kind::kEmpty:
    case RegexAST::Kind::kMacroRef:
      break;
    case RegexAST::Kind::kPredicate:
      list.push_back(ast.predicate);
      break;
    case RegexAST::Kind::kConcat:
    case RegexAST::Kind::kAlternation:
    case RegexAST::Kind::kQuantifier:
    case RegexAST::Kind::kGroup:
    case RegexAST::Kind::kCapture:
      for (const RegexAST& c : ast.children) {
        CollectPredicates(c, list);
      }
      break;
  }
}

void BuildEquivalenceClasses(
    const std::vector<ExpandedRuleSpec>& expanded_rules,
    const absl::flat_hash_set<uint16_t>& universe,
    std::vector<uint16_t>& out_ec_table, int& out_num_classes,
    absl::flat_hash_map<uint16_t, int>& out_char_to_ec) {
  std::vector<CharPredicate> predicates;
  for (const ExpandedRuleSpec& r : expanded_rules) {
    CollectPredicates(r.lhs, predicates);
  }

  constexpr uint16_t kSentinel = 0xFFFD;
  auto signature = [&predicates](uint16_t lower_char) -> std::vector<uint64_t> {
    size_t word_count = std::max<size_t>(1, (predicates.size() + 63) / 64);
    std::vector<uint64_t> sig(word_count, 0ULL);
    for (size_t i = 0; i < predicates.size(); ++i) {
      if (predicates[i].Contains(lower_char)) {
        sig[i / 64] |= (1ULL << static_cast<uint64_t>(i % 64));
      }
    }
    return sig;
  };

  absl::flat_hash_map<std::vector<uint64_t>, int> sig_to_ec;
  std::vector<uint64_t> default_sig = signature(kSentinel);
  sig_to_ec[default_sig] = 0;
  int next_ec = 1;

  // Sort universe characters so equivalence class IDs are deterministic.
  std::vector<uint16_t> sorted_universe(universe.begin(), universe.end());
  std::sort(sorted_universe.begin(), sorted_universe.end());

  out_char_to_ec.clear();
  out_char_to_ec[kSentinel] = 0;
  for (uint16_t ch : sorted_universe) {
    std::vector<uint64_t> sig = signature(ch);
    auto [it, inserted] = sig_to_ec.emplace(std::move(sig), next_ec);
    if (inserted) {
      ++next_ec;
    }
    out_char_to_ec[ch] = it->second;
  }

  out_ec_table.assign(65536, 0);
  for (const auto& [lower_ch, ec_id] : out_char_to_ec) {
    if (lower_ch == kSentinel) continue;
    uint16_t ec = static_cast<uint16_t>(ec_id);
    out_ec_table[lower_ch] = ec;
    uint16_t upper_ch = ToUpper(lower_ch);
    out_ec_table[upper_ch] = ec;
  }
  out_num_classes = next_ec;
}

ECBitSet PredicateToBitSet(const CharPredicate& pred,
                           const absl::flat_hash_map<uint16_t, int>& char_to_ec,
                           const absl::flat_hash_set<uint16_t>& universe) {
  ECBitSet bits;
  if (pred.Contains(0xFFFD)) {
    bits.Insert(0);
  }
  for (uint16_t ch : universe) {
    if (pred.Contains(ch)) {
      auto it = char_to_ec.find(ch);
      if (it != char_to_ec.end()) {
        bits.Insert(it->second);
      }
    }
  }
  return bits;
}

// Regex Lowering & Atom Case Slot Assignment

CompiledRegex CompileNestedRegex(
    const RegexAST& ast, const absl::flat_hash_map<uint16_t, int>& char_to_ec,
    const absl::flat_hash_set<uint16_t>& universe) {
  switch (ast.kind) {
    case RegexAST::Kind::kEmpty:
    case RegexAST::Kind::kMacroRef:
      return CompiledRegex::MakeEmpty();
    case RegexAST::Kind::kPredicate:
      return CompiledRegex::MakeAtom(
          PredicateToBitSet(ast.predicate, char_to_ec, universe), -1);
    case RegexAST::Kind::kConcat: {
      std::vector<CompiledRegex> items;
      items.reserve(ast.children.size());
      for (const RegexAST& c : ast.children) {
        items.push_back(CompileNestedRegex(c, char_to_ec, universe));
      }
      return CompiledRegex::MakeConcat(std::move(items));
    }
    case RegexAST::Kind::kAlternation: {
      std::vector<CompiledRegex> branches;
      branches.reserve(ast.children.size());
      for (const RegexAST& c : ast.children) {
        branches.push_back(CompileNestedRegex(c, char_to_ec, universe));
      }
      return CompiledRegex::MakeAlt(std::move(branches));
    }
    case RegexAST::Kind::kQuantifier:
      return CompiledRegex::MakeRepeat(
          CompileNestedRegex(ast.children[0], char_to_ec, universe),
          ast.quant_min, ast.quant_max);
    case RegexAST::Kind::kGroup:
      return CompileNestedRegex(ast.children[0], char_to_ec, universe);
    case RegexAST::Kind::kCapture:
      return CompiledRegex::MakeCapture(
          ast.capture_index,
          CompileNestedRegex(ast.children[0], char_to_ec, universe));
  }
  return CompiledRegex::MakeEmpty();
}

CompiledRegex CompileTopLevelRegex(
    const RegexAST& ast, const absl::flat_hash_map<uint16_t, int>& char_to_ec,
    const absl::flat_hash_set<uint16_t>& universe, int& atom_slot_count) {
  switch (ast.kind) {
    case RegexAST::Kind::kPredicate: {
      int slot = atom_slot_count++;
      return CompiledRegex::MakeAtom(
          PredicateToBitSet(ast.predicate, char_to_ec, universe), slot);
    }
    case RegexAST::Kind::kGroup: {
      const RegexAST& inner = ast.children[0];
      if (inner.kind == RegexAST::Kind::kPredicate) {
        int slot = atom_slot_count++;
        return CompiledRegex::MakeAtom(
            PredicateToBitSet(inner.predicate, char_to_ec, universe), slot);
      }
      return CompileNestedRegex(inner, char_to_ec, universe);
    }
    case RegexAST::Kind::kConcat: {
      std::vector<CompiledRegex> lowered;
      lowered.reserve(ast.children.size());
      for (const RegexAST& item : ast.children) {
        if (item.kind == RegexAST::Kind::kPredicate ||
            item.kind == RegexAST::Kind::kGroup) {
          lowered.push_back(CompileTopLevelRegex(item, char_to_ec, universe,
                                                 atom_slot_count));
        } else {
          lowered.push_back(CompileNestedRegex(item, char_to_ec, universe));
        }
      }
      return CompiledRegex::MakeConcat(std::move(lowered));
    }
    default:
      return CompileNestedRegex(ast, char_to_ec, universe);
  }
}

// Compile-Time Fixed Offset Submatch Analyzer

struct TopSegment {
  enum class Kind {
    kFixedAtom,
    kFixedCapture,
    kVariableCapture,
    kFixedOther,
    kVariableOther,
  };
  Kind kind = Kind::kFixedOther;
  int slot = -1;
  int group_idx = 0;
  int length = 0;
};

bool ContainsCapture(const CompiledRegex& node) {
  switch (node.kind) {
    case CompiledRegex::Kind::kEmpty:
    case CompiledRegex::Kind::kAtom:
      return false;
    case CompiledRegex::Kind::kCapture:
      return true;
    case CompiledRegex::Kind::kConcat:
    case CompiledRegex::Kind::kAlt:
      for (const CompiledRegex& c : node.children) {
        if (ContainsCapture(c)) return true;
      }
      return false;
    case CompiledRegex::Kind::kRepeatNode:
      return !node.children.empty() && ContainsCapture(node.children[0]);
  }
  return false;
}

std::optional<int> FixedLengthWithoutCaptures(const CompiledRegex& node) {
  switch (node.kind) {
    case CompiledRegex::Kind::kEmpty:
      return 0;
    case CompiledRegex::Kind::kAtom:
      return node.atom_slot < 0 ? std::optional<int>(1) : std::nullopt;
    case CompiledRegex::Kind::kConcat: {
      int total = 0;
      for (const CompiledRegex& item : node.children) {
        std::optional<int> len = FixedLengthWithoutCaptures(item);
        if (!len.has_value()) return std::nullopt;
        total += *len;
      }
      return total;
    }
    case CompiledRegex::Kind::kAlt: {
      if (node.children.empty()) return std::nullopt;
      std::optional<int> expected =
          FixedLengthWithoutCaptures(node.children[0]);
      if (!expected.has_value()) return std::nullopt;
      for (size_t i = 1; i < node.children.size(); ++i) {
        if (FixedLengthWithoutCaptures(node.children[i]) != expected) {
          return std::nullopt;
        }
      }
      return expected;
    }
    case CompiledRegex::Kind::kRepeatNode: {
      if (node.repeat_max < 0 || node.repeat_min != node.repeat_max) {
        return std::nullopt;
      }
      std::optional<int> sub_len = FixedLengthWithoutCaptures(node.children[0]);
      if (!sub_len.has_value()) return std::nullopt;
      return node.repeat_min * (*sub_len);
    }
    case CompiledRegex::Kind::kCapture:
      return std::nullopt;
  }
  return std::nullopt;
}

bool BuildFixedSlicePlan(const CompiledRegex& regex, int capture_count,
                         int atom_slot_count, FixedSlicePlan& out_plan) {
  std::vector<CompiledRegex> top_items;
  if (regex.kind == CompiledRegex::Kind::kConcat) {
    top_items = regex.children;
  } else {
    top_items.push_back(regex);
  }

  std::vector<TopSegment> segments;
  std::optional<size_t> variable_index;

  for (const CompiledRegex& item : top_items) {
    if (item.kind == CompiledRegex::Kind::kAtom) {
      segments.push_back(
          TopSegment{TopSegment::Kind::kFixedAtom, item.atom_slot, 0, 1});
    } else if (item.kind == CompiledRegex::Kind::kCapture) {
      const CompiledRegex& inner = item.children[0];
      if (ContainsCapture(inner)) return false;
      std::optional<int> len = FixedLengthWithoutCaptures(inner);
      if (len.has_value()) {
        segments.push_back(TopSegment{TopSegment::Kind::kFixedCapture, -1,
                                      item.capture_index, *len});
      } else {
        if (variable_index.has_value()) return false;
        variable_index = segments.size();
        segments.push_back(TopSegment{TopSegment::Kind::kVariableCapture, -1,
                                      item.capture_index, 0});
      }
    } else {
      if (ContainsCapture(item)) return false;
      std::optional<int> len = FixedLengthWithoutCaptures(item);
      if (len.has_value()) {
        segments.push_back(
            TopSegment{TopSegment::Kind::kFixedOther, -1, 0, *len});
      } else {
        if (variable_index.has_value()) return false;
        variable_index = segments.size();
        segments.push_back(
            TopSegment{TopSegment::Kind::kVariableOther, -1, 0, 0});
      }
    }
  }

  size_t var_idx = variable_index.value_or(segments.size());

  int suffix_total = 0;
  if (var_idx + 1 < segments.size()) {
    for (size_t idx = var_idx + 1; idx < segments.size(); ++idx) {
      const TopSegment& seg = segments[idx];
      if (seg.kind == TopSegment::Kind::kVariableCapture ||
          seg.kind == TopSegment::Kind::kVariableOther) {
        return false;
      }
      suffix_total += seg.length;
    }
  }

  std::vector<std::optional<std::pair<SliceOffset, SliceOffset>>> capture_spans(
      static_cast<size_t>(capture_count));
  std::vector<std::optional<SliceOffset>> atom_offsets(
      static_cast<size_t>(atom_slot_count));

  int prefix_cursor = 0;
  for (size_t idx = 0; idx < var_idx; ++idx) {
    const TopSegment& seg = segments[idx];
    switch (seg.kind) {
      case TopSegment::Kind::kFixedAtom:
        if (seg.slot >= 0 && seg.slot < atom_slot_count) {
          atom_offsets[static_cast<size_t>(seg.slot)] =
              SliceOffset{false, prefix_cursor};
        }
        prefix_cursor += seg.length;
        break;
      case TopSegment::Kind::kFixedCapture:
        capture_spans[static_cast<size_t>(seg.group_idx - 1)] =
            std::make_pair(SliceOffset{false, prefix_cursor},
                           SliceOffset{false, prefix_cursor + seg.length});
        prefix_cursor += seg.length;
        break;
      case TopSegment::Kind::kFixedOther:
        prefix_cursor += seg.length;
        break;
      case TopSegment::Kind::kVariableCapture:
      case TopSegment::Kind::kVariableOther:
        return false;
    }
  }

  if (var_idx < segments.size()) {
    if (segments[var_idx].kind == TopSegment::Kind::kVariableCapture) {
      capture_spans[static_cast<size_t>(segments[var_idx].group_idx - 1)] =
          std::make_pair(SliceOffset{false, prefix_cursor},
                         SliceOffset{true, -suffix_total});
    }
  }

  int rem_from_end = suffix_total;
  if (var_idx + 1 < segments.size()) {
    for (size_t idx = var_idx + 1; idx < segments.size(); ++idx) {
      const TopSegment& seg = segments[idx];
      switch (seg.kind) {
        case TopSegment::Kind::kFixedAtom:
          if (seg.slot >= 0 && seg.slot < atom_slot_count) {
            atom_offsets[static_cast<size_t>(seg.slot)] =
                SliceOffset{true, -rem_from_end};
          }
          rem_from_end -= seg.length;
          break;
        case TopSegment::Kind::kFixedCapture:
          capture_spans[static_cast<size_t>(seg.group_idx - 1)] =
              std::make_pair(SliceOffset{true, -rem_from_end},
                             SliceOffset{true, -(rem_from_end - seg.length)});
          rem_from_end -= seg.length;
          break;
        case TopSegment::Kind::kFixedOther:
          rem_from_end -= seg.length;
          break;
        case TopSegment::Kind::kVariableCapture:
        case TopSegment::Kind::kVariableOther:
          return false;
      }
    }
  }

  out_plan.capture_spans.clear();
  out_plan.atom_offsets.clear();
  for (const std::optional<std::pair<SliceOffset, SliceOffset>>& c :
       capture_spans) {
    if (!c.has_value()) return false;
    out_plan.capture_spans.push_back(*c);
  }
  for (const std::optional<SliceOffset>& a : atom_offsets) {
    if (!a.has_value()) return false;
    out_plan.atom_offsets.push_back(*a);
  }
  return true;
}

// Stage 2 Builder: Forward PikeVM (`RuleMatcher`)

RuleMatcher BuildPikeVMMatcher(const CompiledRegex& regex, int capture_count,
                               int atom_slot_count) {
  std::vector<PikeVMInst> insts;
  int atom_tag_base = capture_count * 2;

  auto emit = [&](this auto&& self, const CompiledRegex& node) -> void {
    switch (node.kind) {
      case CompiledRegex::Kind::kEmpty:
        break;
      case CompiledRegex::Kind::kAtom:
        if (node.atom_slot >= 0) {
          insts.push_back(
              PikeVMInst::MakeSaveTag(atom_tag_base + node.atom_slot));
        }
        insts.push_back(PikeVMInst::MakeCharClass(node.bits));
        break;
      case CompiledRegex::Kind::kConcat:
        for (const CompiledRegex& item : node.children) {
          self(item);
        }
        break;
      case CompiledRegex::Kind::kAlt: {
        if (node.children.empty()) return;
        if (node.children.size() == 1) {
          self(node.children[0]);
          return;
        }
        std::vector<size_t> end_jumps;
        for (size_t i = 0; i + 1 < node.children.size(); ++i) {
          size_t split_idx = insts.size();
          insts.push_back(PikeVMInst::MakeSplit(0, 0));
          int first_start = static_cast<int>(insts.size());
          self(node.children[i]);
          size_t jump_idx = insts.size();
          insts.push_back(PikeVMInst::MakeJump(0));
          end_jumps.push_back(jump_idx);
          int next_start = static_cast<int>(insts.size());
          insts[split_idx] = PikeVMInst::MakeSplit(first_start, next_start);
        }
        self(node.children.back());
        int after_all = static_cast<int>(insts.size());
        for (size_t j : end_jumps) {
          insts[j] = PikeVMInst::MakeJump(after_all);
        }
        break;
      }
      case CompiledRegex::Kind::kRepeatNode: {
        for (int i = 0; i < node.repeat_min; ++i) {
          self(node.children[0]);
        }
        if (node.repeat_max >= 0) {
          for (int i = node.repeat_min; i < node.repeat_max; ++i) {
            size_t split_idx = insts.size();
            insts.push_back(PikeVMInst::MakeSplit(0, 0));
            int body_start = static_cast<int>(insts.size());
            self(node.children[0]);
            int after = static_cast<int>(insts.size());
            insts[split_idx] = PikeVMInst::MakeSplit(body_start, after);
          }
        } else {
          size_t split_idx = insts.size();
          insts.push_back(PikeVMInst::MakeSplit(0, 0));
          int body_start = static_cast<int>(insts.size());
          self(node.children[0]);
          insts.push_back(PikeVMInst::MakeJump(static_cast<int>(split_idx)));
          int after = static_cast<int>(insts.size());
          insts[split_idx] = PikeVMInst::MakeSplit(body_start, after);
        }
        break;
      }
      case CompiledRegex::Kind::kCapture: {
        int start_tag = (node.capture_index - 1) * 2;
        int end_tag = start_tag + 1;
        insts.push_back(PikeVMInst::MakeSaveTag(start_tag));
        self(node.children[0]);
        insts.push_back(PikeVMInst::MakeSaveTag(end_tag));
        break;
      }
    }
  };

  emit(regex);
  insts.push_back(PikeVMInst::MakeMatch());
  return RuleMatcher{std::move(insts), capture_count * 2 + atom_slot_count};
}

// Stage 1 Builder: Reversed Multi-Rule NFA -> Flat Powerset DFA
// (`KeyAutomaton`)

struct RevNFAState {
  std::vector<int> epsilon;
  std::vector<std::pair<ECBitSet, int>> transitions;
  KeyAutomaton::AcceptEntry unanchored_accept;
  KeyAutomaton::AcceptEntry anchored_accept;
};

CompiledRegex ReverseRegex(const CompiledRegex& node) {
  switch (node.kind) {
    case CompiledRegex::Kind::kEmpty:
      return CompiledRegex::MakeEmpty();
    case CompiledRegex::Kind::kAtom:
      return CompiledRegex::MakeAtom(node.bits, -1);
    case CompiledRegex::Kind::kConcat: {
      std::vector<CompiledRegex> rev;
      rev.reserve(node.children.size());
      for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
        rev.push_back(ReverseRegex(*it));
      }
      return CompiledRegex::MakeConcat(std::move(rev));
    }
    case CompiledRegex::Kind::kAlt: {
      std::vector<CompiledRegex> branches;
      branches.reserve(node.children.size());
      for (const CompiledRegex& b : node.children) {
        branches.push_back(ReverseRegex(b));
      }
      return CompiledRegex::MakeAlt(std::move(branches));
    }
    case CompiledRegex::Kind::kRepeatNode:
      return CompiledRegex::MakeRepeat(ReverseRegex(node.children[0]),
                                       node.repeat_min, node.repeat_max);
    case CompiledRegex::Kind::kCapture:
      return ReverseRegex(node.children[0]);
  }
  return CompiledRegex::MakeEmpty();
}

struct AcceptKey {
  std::vector<KeyAutomaton::AcceptEntry> unanchored;
  std::vector<KeyAutomaton::AcceptEntry> anchored;
  bool operator==(const AcceptKey& o) const {
    return unanchored == o.unanchored && anchored == o.anchored;
  }
  template <typename H>
  friend H AbslHashValue(H h, const AcceptKey& k) {
    return H::combine(std::move(h), k.unanchored, k.anchored);
  }
};

KeyAutomaton MinimizeDFA(const KeyAutomaton& dfa) {
  int state_count = dfa.StateCount();
  int num_classes = dfa.num_classes;
  if (state_count <= 2 || num_classes <= 0) return dfa;

  absl::flat_hash_map<AcceptKey, int> accept_to_part;
  std::vector<int> part_id(static_cast<size_t>(state_count), 0);
  int part_count = 0;

  for (int s = 0; s < state_count; ++s) {
    AcceptKey key{dfa.unanchored_accept[static_cast<size_t>(s)],
                  dfa.anchored_accept[static_cast<size_t>(s)]};
    auto [it, inserted] = accept_to_part.emplace(std::move(key), part_count);
    if (inserted) {
      ++part_count;
    }
    part_id[static_cast<size_t>(s)] = it->second;
  }

  bool changed = true;
  while (changed) {
    changed = false;
    absl::flat_hash_map<std::vector<int>, int> sig_to_part;
    std::vector<int> new_part_id(static_cast<size_t>(state_count), 0);
    int next_count = 0;

    for (int s = 0; s < state_count; ++s) {
      std::vector<int> sig;
      sig.reserve(static_cast<size_t>(num_classes + 1));
      sig.push_back(part_id[static_cast<size_t>(s)]);
      int row_base = s * num_classes;
      for (int ec = 0; ec < num_classes; ++ec) {
        int target = static_cast<int>(
            dfa.transitions[static_cast<size_t>(row_base + ec)]);
        sig.push_back(part_id[static_cast<size_t>(target)]);
      }
      auto [it, inserted] = sig_to_part.emplace(std::move(sig), next_count);
      if (inserted) {
        ++next_count;
      }
      new_part_id[static_cast<size_t>(s)] = it->second;
    }

    if (next_count != part_count) {
      changed = true;
      part_count = next_count;
      part_id = std::move(new_part_id);
    }
  }

  std::vector<int> remap(static_cast<size_t>(part_count), -1);
  int dead_part = part_id[0];
  remap[static_cast<size_t>(dead_part)] = 0;
  int next_final_id = 1;

  int start_part = part_id[1];
  if (start_part != dead_part) {
    remap[static_cast<size_t>(start_part)] = 1;
    next_final_id = 2;
  } else {
    KeyAutomaton empty;
    empty.num_classes = num_classes;
    empty.min_priority = dfa.min_priority;
    empty.transitions.assign(static_cast<size_t>(2 * num_classes), 0);
    empty.unanchored_accept.resize(2);
    empty.anchored_accept.resize(2);
    return empty;
  }

  for (int s = 0; s < state_count; ++s) {
    int p = part_id[static_cast<size_t>(s)];
    if (remap[static_cast<size_t>(p)] == -1) {
      remap[static_cast<size_t>(p)] = next_final_id++;
    }
  }

  int final_count = next_final_id;
  KeyAutomaton min_dfa;
  min_dfa.num_classes = num_classes;
  min_dfa.min_priority = dfa.min_priority;
  min_dfa.transitions.assign(static_cast<size_t>(final_count * num_classes), 0);
  min_dfa.unanchored_accept.resize(static_cast<size_t>(final_count));
  min_dfa.anchored_accept.resize(static_cast<size_t>(final_count));
  std::vector<bool> populated(static_cast<size_t>(final_count), false);

  for (int s = 0; s < state_count; ++s) {
    int new_s = remap[static_cast<size_t>(part_id[static_cast<size_t>(s)])];
    if (populated[static_cast<size_t>(new_s)]) continue;
    populated[static_cast<size_t>(new_s)] = true;
    min_dfa.unanchored_accept[static_cast<size_t>(new_s)] =
        dfa.unanchored_accept[static_cast<size_t>(s)];
    min_dfa.anchored_accept[static_cast<size_t>(new_s)] =
        dfa.anchored_accept[static_cast<size_t>(s)];
    int old_base = s * num_classes;
    int new_base = new_s * num_classes;
    for (int ec = 0; ec < num_classes; ++ec) {
      int old_next =
          static_cast<int>(dfa.transitions[static_cast<size_t>(old_base + ec)]);
      min_dfa.transitions[static_cast<size_t>(new_base + ec)] =
          static_cast<int32_t>(remap[static_cast<size_t>(
              part_id[static_cast<size_t>(old_next)])]);
    }
  }

  return min_dfa;
}

KeyAutomaton BuildReverseDFA(
    const std::vector<int>& rule_ids,
    const std::vector<ExpandedRuleSpec>& expanded_rules,
    const std::vector<CompiledRule>& compiled_rules, int num_classes,
    const absl::flat_hash_map<uint16_t, int>& char_to_ec,
    const absl::flat_hash_set<uint16_t>& universe) {
  std::vector<RevNFAState> nfa;
  auto new_state = [&nfa]() -> int {
    nfa.emplace_back();
    return static_cast<int>(nfa.size() - 1);
  };

  auto build_fragment = [&](this auto&& self,
                            const CompiledRegex& node) -> std::pair<int, int> {
    switch (node.kind) {
      case CompiledRegex::Kind::kEmpty: {
        int s = new_state();
        return {s, s};
      }
      case CompiledRegex::Kind::kAtom: {
        int s = new_state();
        int e = new_state();
        nfa[static_cast<size_t>(s)].transitions.emplace_back(node.bits, e);
        return {s, e};
      }
      case CompiledRegex::Kind::kConcat: {
        if (node.children.empty()) {
          int s = new_state();
          return {s, s};
        }
        std::pair<int, int> current = self(node.children[0]);
        for (size_t i = 1; i < node.children.size(); ++i) {
          auto [next_start, next_end] = self(node.children[i]);
          nfa[static_cast<size_t>(current.second)].epsilon.push_back(
              next_start);
          current = {current.first, next_end};
        }
        return current;
      }
      case CompiledRegex::Kind::kAlt: {
        int s = new_state();
        int e = new_state();
        for (const CompiledRegex& b : node.children) {
          auto [frag_start, frag_end] = self(b);
          nfa[static_cast<size_t>(s)].epsilon.push_back(frag_start);
          nfa[static_cast<size_t>(frag_end)].epsilon.push_back(e);
        }
        return {s, e};
      }
      case CompiledRegex::Kind::kRepeatNode: {
        std::optional<int> head_start;
        std::optional<int> prev_end;
        auto append_frag = [&](std::pair<int, int> f) {
          if (prev_end.has_value()) {
            nfa[static_cast<size_t>(*prev_end)].epsilon.push_back(f.first);
          } else {
            head_start = f.first;
          }
          prev_end = f.second;
        };

        for (int i = 0; i < node.repeat_min; ++i) {
          append_frag(self(node.children[0]));
        }
        if (node.repeat_max >= 0) {
          for (int i = node.repeat_min; i < node.repeat_max; ++i) {
            int s = new_state();
            int e = new_state();
            auto [frag_start, frag_end] = self(node.children[0]);
            nfa[static_cast<size_t>(s)].epsilon.push_back(frag_start);
            nfa[static_cast<size_t>(s)].epsilon.push_back(e);
            nfa[static_cast<size_t>(frag_end)].epsilon.push_back(e);
            append_frag({s, e});
          }
        } else {
          int s = new_state();
          int e = new_state();
          auto [frag_start, frag_end] = self(node.children[0]);
          nfa[static_cast<size_t>(s)].epsilon.push_back(frag_start);
          nfa[static_cast<size_t>(s)].epsilon.push_back(e);
          nfa[static_cast<size_t>(frag_end)].epsilon.push_back(frag_start);
          nfa[static_cast<size_t>(frag_end)].epsilon.push_back(e);
          append_frag({s, e});
        }

        if (head_start.has_value() && prev_end.has_value()) {
          return {*head_start, *prev_end};
        }
        int s = new_state();
        return {s, s};
      }
      case CompiledRegex::Kind::kCapture:
        return self(node.children[0]);
    }
    int s = new_state();
    return {s, s};
  };

  int root_nfa = new_state();
  for (int rule_id : rule_ids) {
    const ExpandedRuleSpec& exp = expanded_rules[static_cast<size_t>(rule_id)];
    const CompiledRule& comp = compiled_rules[static_cast<size_t>(rule_id)];
    int dummy_slots = 0;
    CompiledRegex forward_compiled =
        CompileTopLevelRegex(exp.lhs, char_to_ec, universe, dummy_slots);
    CompiledRegex rev = ReverseRegex(forward_compiled);
    auto [frag_start, frag_end] = build_fragment(rev);
    nfa[static_cast<size_t>(root_nfa)].epsilon.push_back(frag_start);

    KeyAutomaton::AcceptEntry entry{static_cast<int32_t>(comp.priority),
                                    static_cast<int32_t>(rule_id),
                                    comp.flag_name};
    RevNFAState& end_state = nfa[static_cast<size_t>(frag_end)];
    if (comp.is_anchored) {
      if (!end_state.anchored_accept.IsValid() ||
          entry.priority < end_state.anchored_accept.priority) {
        end_state.anchored_accept = entry;
      }
    } else {
      if (!end_state.unanchored_accept.IsValid() ||
          entry.priority < end_state.unanchored_accept.priority) {
        end_state.unanchored_accept = entry;
      }
    }
  }

  auto epsilon_closure =
      [&nfa](const absl::flat_hash_set<int>& seeds) -> absl::btree_set<int> {
    absl::btree_set<int> closure(seeds.begin(), seeds.end());
    std::vector<int> stack(seeds.begin(), seeds.end());
    while (!stack.empty()) {
      int s = stack.back();
      stack.pop_back();
      for (int next : nfa[static_cast<size_t>(s)].epsilon) {
        auto [it, inserted] = closure.emplace(next);
        if (inserted) {
          stack.push_back(next);
        }
      }
    }
    return closure;
  };

  absl::flat_hash_map<absl::btree_set<int>, int> subset_to_dfa;
  std::vector<absl::btree_set<int>> dfa_subsets;
  dfa_subsets.push_back({});
  subset_to_dfa[{}] = 0;

  absl::btree_set<int> start_set = epsilon_closure({root_nfa});
  subset_to_dfa[start_set] = 1;
  dfa_subsets.push_back(start_set);

  std::vector<int32_t> transitions(static_cast<size_t>(2 * num_classes), 0);
  std::vector<std::vector<KeyAutomaton::AcceptEntry>> unanchored_accept(2);
  std::vector<std::vector<KeyAutomaton::AcceptEntry>> anchored_accept(2);

  auto build_chain = [](std::vector<KeyAutomaton::AcceptEntry> candidates) {
    std::sort(candidates.begin(), candidates.end());
    std::vector<KeyAutomaton::AcceptEntry> chain;
    absl::flat_hash_set<std::string> seen_flags;
    for (KeyAutomaton::AcceptEntry& entry : candidates) {
      if (entry.flag_name.empty()) {
        chain.push_back(std::move(entry));
        break;
      }
      auto [it, inserted] = seen_flags.emplace(entry.flag_name);
      if (inserted) {
        chain.push_back(std::move(entry));
      }
    }
    return chain;
  };

  auto compute_accepts = [&](const absl::btree_set<int>& subset,
                             int dfa_state) {
    std::vector<KeyAutomaton::AcceptEntry> u_candidates;
    std::vector<KeyAutomaton::AcceptEntry> a_candidates;
    for (int s : subset) {
      const KeyAutomaton::AcceptEntry& u =
          nfa[static_cast<size_t>(s)].unanchored_accept;
      if (u.IsValid()) {
        u_candidates.push_back(u);
      }
      const KeyAutomaton::AcceptEntry& a =
          nfa[static_cast<size_t>(s)].anchored_accept;
      if (a.IsValid()) {
        a_candidates.push_back(a);
      }
    }
    unanchored_accept[static_cast<size_t>(dfa_state)] =
        build_chain(std::move(u_candidates));
    anchored_accept[static_cast<size_t>(dfa_state)] =
        build_chain(std::move(a_candidates));
  };

  compute_accepts(start_set, 1);

  for (size_t queue_idx = 1; queue_idx < dfa_subsets.size(); ++queue_idx) {
    absl::btree_set<int> current_set = dfa_subsets[queue_idx];
    for (int ec = 0; ec < num_classes; ++ec) {
      absl::flat_hash_set<int> moved;
      for (int s : current_set) {
        for (const auto& [bits, target] :
             nfa[static_cast<size_t>(s)].transitions) {
          if (bits.Contains(ec)) {
            moved.insert(target);
          }
        }
      }
      if (moved.empty()) {
        transitions[queue_idx * static_cast<size_t>(num_classes) +
                    static_cast<size_t>(ec)] = 0;
        continue;
      }
      absl::btree_set<int> next_subset = epsilon_closure(moved);
      int candidate_state = static_cast<int>(dfa_subsets.size());
      auto [it, inserted] = subset_to_dfa.emplace(next_subset, candidate_state);
      int next_state = it->second;
      if (inserted) {
        dfa_subsets.push_back(next_subset);
        transitions.insert(transitions.end(), static_cast<size_t>(num_classes),
                           0);
        unanchored_accept.emplace_back();
        anchored_accept.emplace_back();
        compute_accepts(next_subset, next_state);
      }
      transitions[queue_idx * static_cast<size_t>(num_classes) +
                  static_cast<size_t>(ec)] = static_cast<int32_t>(next_state);
    }
  }

  int32_t min_prio_in_dfa = std::numeric_limits<int32_t>::max();
  for (int r_id : rule_ids) {
    int32_t p = static_cast<int32_t>(
        compiled_rules[static_cast<size_t>(r_id)].priority);
    if (p < min_prio_in_dfa) min_prio_in_dfa = p;
  }
  if (rule_ids.empty()) min_prio_in_dfa = 0;

  for (size_t s = 1; s < dfa_subsets.size(); ++s) {
    const std::vector<KeyAutomaton::AcceptEntry>& u = unanchored_accept[s];
    if (!u.empty() && u[0].flag_name.empty() &&
        u[0].priority == min_prio_in_dfa) {
      anchored_accept[s].clear();
      size_t row_base = s * static_cast<size_t>(num_classes);
      for (int ec = 0; ec < num_classes; ++ec) {
        transitions[row_base + static_cast<size_t>(ec)] = 0;
      }
    }
  }

  KeyAutomaton raw_dfa;
  raw_dfa.num_classes = num_classes;
  raw_dfa.min_priority = min_prio_in_dfa;
  raw_dfa.transitions = std::move(transitions);
  raw_dfa.unanchored_accept = std::move(unanchored_accept);
  raw_dfa.anchored_accept = std::move(anchored_accept);
  return MinimizeDFA(raw_dfa);
}

}  // namespace

// AutomatonCompiler Public Functions

bool CompileAutomaton(std::string_view source, CompiledAutomaton& out_automaton,
                      RuleCompileError& out_error) {
  GrammarSpec spec;
  if (!ParseGrammarSpec(source, spec, out_error)) {
    return false;
  }

  // 1. Assign dense integer IDs to `%map` declarations (sorted by name)
  std::vector<std::string> sorted_map_names;
  sorted_map_names.reserve(spec.maps.size());
  for (const auto& [name, map] : spec.maps) {
    sorted_map_names.push_back(name);
  }
  std::sort(sorted_map_names.begin(), sorted_map_names.end());

  absl::flat_hash_map<std::string, int> map_names;
  std::vector<CharacterMap> maps_list;
  maps_list.reserve(sorted_map_names.size());
  for (const std::string& name : sorted_map_names) {
    auto it = spec.maps.find(name);
    if (it != spec.maps.end()) {
      map_names[name] = static_cast<int>(maps_list.size());
      maps_list.push_back(it->second);
    }
  }

  // 2. Expand macros & specialize `$0`-dependent rules, collecting character
  // universe
  absl::flat_hash_set<uint16_t> universe;
  for (const RuleDecl& rule : spec.rules) {
    rule.input_predicate.ReferencedCharacters(universe);
  }

  std::vector<ExpandedRuleSpec> expanded_rules;
  for (size_t prio = 0; prio < spec.rules.size(); ++prio) {
    const RuleDecl& decl = spec.rules[prio];
    bool uses_key_bound = ContainsKeyBoundMacro(decl.lhs);
    absl::flat_hash_set<uint16_t> trigger_keys =
        ResolveTriggerKeys(decl.input_predicate, universe);

    if (uses_key_bound) {
      std::vector<uint16_t> sorted_trigger_keys(trigger_keys.begin(),
                                                trigger_keys.end());
      std::sort(sorted_trigger_keys.begin(), sorted_trigger_keys.end());
      for (uint16_t key : sorted_trigger_keys) {
        absl::flat_hash_set<std::string> visiting;
        RegexAST resolved_lhs;
        if (!ExpandMacros(decl.lhs, spec, key, visiting, resolved_lhs,
                          out_error)) {
          return false;
        }
        CollectUniverse(resolved_lhs, universe);
        expanded_rules.push_back(ExpandedRuleSpec{
            decl.flag_name, std::move(resolved_lhs),
            CharPredicate::Positive({key}), decl.rhs, static_cast<int>(prio),
            decl.line, decl.capture_count, decl.is_anchored});
      }
    } else {
      absl::flat_hash_set<std::string> visiting;
      RegexAST resolved_lhs;
      if (!ExpandMacros(decl.lhs, spec, std::nullopt, visiting, resolved_lhs,
                        out_error)) {
        return false;
      }
      CollectUniverse(resolved_lhs, universe);
      expanded_rules.push_back(ExpandedRuleSpec{
          decl.flag_name, std::move(resolved_lhs), decl.input_predicate,
          decl.rhs, static_cast<int>(prio), decl.line, decl.capture_count,
          decl.is_anchored});
    }
  }

  // 3. Build Flex-style Character Equivalence Classes (`ec_table`)
  std::vector<uint16_t> ec_table;
  int num_classes = 0;
  absl::flat_hash_map<uint16_t, int> char_to_ec;
  BuildEquivalenceClasses(expanded_rules, universe, ec_table, num_classes,
                          char_to_ec);
  if (num_classes > kMaxEquivalenceClasses) {
    out_error = RuleCompileError{
        1, absl::Substitute(
               "Automaton requires $0 equivalence classes, exceeding the "
               "maximum of $1.",
               num_classes, kMaxEquivalenceClasses)};
    return false;
  }

  // 4. Compile each expanded rule into `CompiledRule` (PikeVM + CompiledRHS)
  std::vector<CompiledRule> compiled_rules;
  compiled_rules.reserve(expanded_rules.size());

  for (const ExpandedRuleSpec& exp : expanded_rules) {
    int atom_slot_count = 0;
    CompiledRegex compiled_regex =
        CompileTopLevelRegex(exp.lhs, char_to_ec, universe, atom_slot_count);

    FixedSlicePlan fixed_plan;
    bool has_fixed_plan = BuildFixedSlicePlan(compiled_regex, exp.capture_count,
                                              atom_slot_count, fixed_plan);

    RuleMatcher matcher =
        BuildPikeVMMatcher(compiled_regex, exp.capture_count, atom_slot_count);

    std::vector<CompiledRHSItem> rhs_items;
    int rhs_literal_count = 0;
    for (const RHSItem& item : exp.rhs) {
      switch (item.kind) {
        case RHSItem::Kind::kLiteral:
          rhs_items.push_back(CompiledRHSItem{CompiledRHSItem::Kind::kLiteral,
                                              item.literal_char, 0, 0, false,
                                              false});
          ++rhs_literal_count;
          break;
        case RHSItem::Kind::kInputKey:
          rhs_items.push_back(CompiledRHSItem{CompiledRHSItem::Kind::kInputKey,
                                              0, 0, 0, false, false});
          break;
        case RHSItem::Kind::kCapture:
          rhs_items.push_back(CompiledRHSItem{CompiledRHSItem::Kind::kCapture,
                                              0, item.capture_index, 0, false,
                                              false});
          break;
        case RHSItem::Kind::kMapProjection: {
          auto mit = map_names.find(item.map_name);
          if (mit == map_names.end()) {
            out_error = RuleCompileError{
                exp.line, absl::Substitute("Unknown map '$0'.", item.map_name)};
            return false;
          }
          rhs_items.push_back(CompiledRHSItem{
              CompiledRHSItem::Kind::kMapProjection, 0, item.capture_index,
              mit->second, item.inverted, item.uses_input_key});
          break;
        }
      }
    }

    compiled_rules.push_back(CompiledRule{
        exp.priority, exp.flag_name, exp.is_anchored, exp.capture_count,
        atom_slot_count, has_fixed_plan, std::move(fixed_plan),
        std::move(matcher), std::move(rhs_items), rhs_literal_count});
  }

  // 5. Partition trigger keys into identical rule-list signatures and
  // determinize each into a `KeyAutomaton`
  absl::flat_hash_set<uint16_t> all_candidate_trigger_chars = universe;
  for (uint16_t ascii = 0x0020; ascii <= 0x007E; ++ascii) {
    all_candidate_trigger_chars.insert(ToLower(ascii));
  }
  std::vector<uint16_t> sorted_candidate_trigger_chars(
      all_candidate_trigger_chars.begin(), all_candidate_trigger_chars.end());
  std::sort(sorted_candidate_trigger_chars.begin(),
            sorted_candidate_trigger_chars.end());

  absl::flat_hash_map<std::vector<int>, int> signature_to_automaton_idx;
  std::vector<KeyAutomaton> key_automata;
  std::vector<int16_t> trigger_table(65536, -1);

  std::vector<int> default_trigger_rule_ids;
  for (size_t idx = 0; idx < expanded_rules.size(); ++idx) {
    if (expanded_rules[idx].trigger_predicate.Contains(0xFFFD)) {
      default_trigger_rule_ids.push_back(static_cast<int>(idx));
    }
  }
  if (!default_trigger_rule_ids.empty()) {
    int default_auto_idx = static_cast<int>(key_automata.size());
    key_automata.push_back(BuildReverseDFA(default_trigger_rule_ids,
                                           expanded_rules, compiled_rules,
                                           num_classes, char_to_ec, universe));
    signature_to_automaton_idx[default_trigger_rule_ids] = default_auto_idx;
    std::fill(trigger_table.begin(), trigger_table.end(),
              static_cast<int16_t>(default_auto_idx));
  }

  for (uint16_t lower_key : sorted_candidate_trigger_chars) {
    std::vector<int> matching_rule_ids;
    for (size_t idx = 0; idx < expanded_rules.size(); ++idx) {
      if (expanded_rules[idx].trigger_predicate.Contains(lower_key)) {
        matching_rule_ids.push_back(static_cast<int>(idx));
      }
    }

    int16_t auto_idx = -1;
    if (!matching_rule_ids.empty()) {
      int candidate_idx = static_cast<int>(key_automata.size());
      auto [it, inserted] =
          signature_to_automaton_idx.emplace(matching_rule_ids, candidate_idx);
      if (inserted) {
        key_automata.push_back(
            BuildReverseDFA(matching_rule_ids, expanded_rules, compiled_rules,
                            num_classes, char_to_ec, universe));
      }
      auto_idx = static_cast<int16_t>(it->second);
    }

    trigger_table[lower_key] = auto_idx;
    uint16_t upper_key = ToUpper(lower_key);
    trigger_table[upper_key] = auto_idx;
  }

  out_automaton.name = spec.name;
  out_automaton.has_name = spec.has_name;
  out_automaton.flags = spec.flags;
  out_automaton.enabled_flags.clear();
  for (const FlagDecl& f : spec.flags) {
    if (f.default_value) {
      out_automaton.enabled_flags.insert(f.name);
    }
  }
  out_automaton.ec_table = std::move(ec_table);
  out_automaton.num_classes = num_classes;
  out_automaton.trigger_table = std::move(trigger_table);
  out_automaton.key_automata = std::move(key_automata);
  out_automaton.rules = std::move(compiled_rules);
  out_automaton.maps = std::move(maps_list);
  out_automaton.tests = spec.tests;
  return true;
}

}  // namespace okeytre
