// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Table-driven C++ Reverse-DFA + PikeVM word transducer and runtime
// primitives.

#ifndef OKEYTRE_ENGINE_COMPILEDAUTOMATON_HPP_
#define OKEYTRE_ENGINE_COMPILEDAUTOMATON_HPP_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "pike-vm.h"
#include "rule-ast.h"
#include "unicode-utils.h"

namespace okeytre {

// Closed-form offset `from_end ? (slice_len + delta) : delta` for rules with at
// most one variable segment.
struct SliceOffset {
  bool from_end = false;
  int delta = 0;

  inline int Resolve(int slice_count) const noexcept {
    return from_end ? (slice_count + delta) : delta;
  }
};

// Compile-time submatch layout eliminating VM execution when at most one
// top-level segment has variable length.
struct FixedSlicePlan {
  std::vector<std::pair<SliceOffset, SliceOffset>> capture_spans;
  std::vector<SliceOffset> atom_offsets;
};

// Compiled right-hand side item with resolved integer map indices.
struct CompiledRHSItem {
  enum class Kind : uint8_t {
    kLiteral,
    kInputKey,
    kCapture,
    kMapProjection,
  };

  Kind kind = Kind::kLiteral;
  uint16_t literal_char = 0;
  int capture_index = 0;
  int map_id = 0;
  bool inverted = false;
  bool uses_input_key = false;
};

// Compiled production rule with its optional static offset plan, fallback
// submatch extractor, and RHS template.
struct CompiledRule {
  int priority = 0;
  std::string flag_name;  // empty if unguarded
  bool is_anchored = false;
  int capture_count = 0;
  int atom_slot_count = 0;
  bool has_fixed_plan = false;
  FixedSlicePlan fixed_plan;
  RuleMatcher matcher;
  std::vector<CompiledRHSItem> rhs_items;
  int rhs_literal_count = 0;

  // True when the RHS is `$1$0` and `$1` spans the entire matched slice
  // (`0..<slice_len`), making the rule a zero-allocation pass-through /
  // negative guard.
  bool IsPassThrough() const noexcept {
    if (!has_fixed_plan || rhs_items.size() != 2 ||
        fixed_plan.capture_spans.empty()) {
      return false;
    }
    if (rhs_items[0].kind != CompiledRHSItem::Kind::kCapture ||
        rhs_items[0].capture_index != 1) {
      return false;
    }
    if (rhs_items[1].kind != CompiledRHSItem::Kind::kInputKey) {
      return false;
    }
    const auto& [start, end] = fixed_plan.capture_spans[0];
    return !start.from_end && start.delta == 0 && end.from_end &&
           end.delta == 0;
  }
};

// Flat deterministic Right-to-Left DFA for a trigger key (or equivalence group
// of trigger keys). State `0` is the dead trap state; State `1` is the start
// state at the caret (`pos`).
struct KeyAutomaton {
  struct AcceptEntry {
    int32_t priority = -1;
    int32_t rule_id = -1;
    std::string flag_name;

    bool IsValid() const noexcept { return rule_id >= 0; }

    bool operator==(const AcceptEntry& other) const {
      return priority == other.priority && rule_id == other.rule_id &&
             flag_name == other.flag_name;
    }
    bool operator<(const AcceptEntry& other) const {
      if (priority != other.priority) return priority < other.priority;
      if (rule_id != other.rule_id) return rule_id < other.rule_id;
      return flag_name < other.flag_name;
    }

    template <typename H>
    friend H AbslHashValue(H h, const AcceptEntry& entry) {
      return H::combine(std::move(h), entry.priority, entry.rule_id,
                        entry.flag_name);
    }
  };

  int num_classes = 0;
  int32_t min_priority = 0;
  // Flat row-major `[state * num_classes + ec] -> next_state`
  std::vector<int32_t> transitions;
  // Priority-ordered unanchored rules accepting at `state`, truncated after
  // the first unguarded rule (empty if `state` is non-accepting).
  std::vector<std::vector<AcceptEntry>> unanchored_accept;
  // Priority-ordered `^`-anchored rules accepting at `state` when `idx == 0`,
  // truncated after the first unguarded rule (empty if none).
  std::vector<std::vector<AcceptEntry>> anchored_accept;

  int StateCount() const noexcept {
    return num_classes > 0 ? static_cast<int>(transitions.size()) / num_classes
                           : 0;
  }

  bool FindBestMatch(std::u16string_view word,
                     const std::vector<uint16_t>& ec_table,
                     const absl::flat_hash_set<std::string>& enabled_flags,
                     int& out_rule_id, int& out_match_start) const;
};

// Pure table-driven C++ word transducer compiled dynamically from a `.ime`
// specification.
struct CompiledAutomaton {
  std::string name;
  bool has_name = false;
  std::vector<FlagDecl> flags;
  absl::flat_hash_set<std::string> enabled_flags;
  // 64K lookup table mapping any `uint16_t` character (case-folded) to its
  // equivalence class `0..<num_classes`.
  std::vector<uint16_t> ec_table;
  int num_classes = 0;
  // 64K lookup table mapping any `uint16_t` input key (upper or lower) to its
  // `KeyAutomaton` index, or `-1`.
  std::vector<int16_t> trigger_table;
  std::vector<KeyAutomaton> key_automata;
  std::vector<CompiledRule> rules;
  std::vector<CharacterMap> maps;
  std::vector<RuleTestVector> tests;

  CompiledAutomaton() = default;
  CompiledAutomaton(const CompiledAutomaton&) = default;
  CompiledAutomaton& operator=(const CompiledAutomaton&) = default;
  CompiledAutomaton(CompiledAutomaton&&) noexcept = default;
  CompiledAutomaton& operator=(CompiledAutomaton&&) noexcept = default;

  CompiledAutomaton WithEnabledFlags(
      const absl::flat_hash_set<std::string>& extra_flags) const {
    CompiledAutomaton copy = *this;
    copy.enabled_flags.insert(extra_flags.begin(), extra_flags.end());
    return copy;
  }

  bool Rewrite(std::u16string_view word, uint16_t input,
               std::u16string& out_result) const;

 private:
  void EvaluateRHS(const CompiledRule& rule, std::u16string_view matched_slice,
                   const std::vector<int>& tags, uint16_t input_key,
                   std::u16string& out_replacement) const;
};

static_assert(std::is_nothrow_move_constructible_v<CompiledAutomaton>);
static_assert(std::is_nothrow_move_assignable_v<CompiledAutomaton>);

}  // namespace okeytre

#endif  // OKEYTRE_ENGINE_COMPILEDAUTOMATON_HPP_
