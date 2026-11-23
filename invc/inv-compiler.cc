// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "inv-compiler.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "inv-ast.h"
#include "unicode-utils.h"

namespace okeytre {

bool CompiledInvDfa::Matches(std::u16string_view word) const noexcept {
  if (state_count <= 1) {
    return false;
  }
  int32_t state = 1;
  for (char16_t ch : word) {
    const uint16_t ec = ec_table[static_cast<uint16_t>(ch)];
    state = transitions[static_cast<size_t>(state * num_classes + ec)];
    if (state == 0) {
      return false;
    }
    if (absorbing_accept[static_cast<size_t>(state)]) {
      return true;
    }
  }
  return accepting[static_cast<size_t>(state)];
}

namespace {

absl::Status FormatError(int32_t line, std::string_view msg) {
  return absl::InvalidArgumentError(absl::Substitute("line $0: $1", line, msg));
}

absl::Status ExpandMacros(
    InvExprNode& node,
    const absl::flat_hash_map<std::string, InvExprNode>& macros,
    absl::flat_hash_set<std::string>& active_stack) {
  if (node.kind == InvExprNode::Kind::kMacroRef) {
    auto it = macros.find(node.macro_name);
    if (it == macros.end()) {
      return FormatError(
          node.line, absl::Substitute("undefined macro '$0'", node.macro_name));
    }
    auto [stack_it, inserted] = active_stack.insert(node.macro_name);
    if (!inserted) {
      return FormatError(
          node.line, absl::Substitute("recursive macro cycle involving '$0'",
                                      node.macro_name));
    }
    InvExprNode expanded = it->second;
    const absl::Status st = ExpandMacros(expanded, macros, active_stack);
    active_stack.erase(node.macro_name);
    if (!st.ok()) {
      return st;
    }
    node = std::move(expanded);
    return absl::OkStatus();
  }
  for (InvExprNode& child : node.children) {
    const absl::Status st = ExpandMacros(child, macros, active_stack);
    if (!st.ok()) {
      return st;
    }
  }
  return absl::OkStatus();
}

void CollectPredicates(const InvExprNode& node,
                       std::vector<const CharClassPred*>& out_preds) {
  if (node.kind == InvExprNode::Kind::kCharClass &&
      node.pred.kind != CharClassPred::Kind::kAny) {
    out_preds.push_back(&node.pred);
  }
  for (const InvExprNode& child : node.children) {
    CollectPredicates(child, out_preds);
  }
}

void BuildEquivalenceClasses(const std::vector<InvPatternRule>& rules,
                             std::array<uint16_t, 65536>& out_ec_table,
                             int32_t& out_num_classes,
                             std::vector<uint16_t>& out_class_representatives) {
  std::vector<const CharClassPred*> preds;
  for (const InvPatternRule& rule : rules) {
    CollectPredicates(rule.expr, preds);
  }

  // Partition all lowercase BMP code units by their boolean membership
  // signature across every character-class predicate in the specification.
  std::vector<uint16_t> lower_to_ec(65536, 0);
  absl::flat_hash_map<std::vector<uint8_t>, uint16_t> sig_to_ec;

  const size_t sig_bytes = (preds.size() + 7) / 8;
  std::vector<uint8_t> zero_sig(sig_bytes, 0);
  for (size_t idx = 0; idx < preds.size(); ++idx) {
    if (preds[idx]->kind == CharClassPred::Kind::kNegative &&
        !preds[idx]->chars.contains(0xFFFD)) {
      zero_sig[idx / 8] |= static_cast<uint8_t>(1u << (idx % 8));
    }
  }
  sig_to_ec.emplace(zero_sig, uint16_t{0});
  out_class_representatives.clear();
  out_class_representatives.push_back(0xFFFD);

  std::vector<uint8_t> sig(sig_bytes, 0);
  for (uint32_t code = 0; code < 65536; ++code) {
    const uint16_t ch = static_cast<uint16_t>(code);
    if (ToLower(ch) != ch) {
      continue;
    }
    std::fill(sig.begin(), sig.end(), uint8_t{0});
    for (size_t idx = 0; idx < preds.size(); ++idx) {
      if (preds[idx]->MatchesLower(ch)) {
        sig[idx / 8] |= static_cast<uint8_t>(1u << (idx % 8));
      }
    }
    const uint16_t candidate_ec =
        static_cast<uint16_t>(out_class_representatives.size());
    auto [it, inserted] = sig_to_ec.try_emplace(sig, candidate_ec);
    if (inserted) {
      out_class_representatives.push_back(ch);
    }
    lower_to_ec[code] = it->second;
  }

  for (uint32_t code = 0; code < 65536; ++code) {
    const uint16_t lower = ToLower(static_cast<uint16_t>(code));
    out_ec_table[code] = lower_to_ec[lower];
  }
  out_num_classes = static_cast<int32_t>(out_class_representatives.size());
}

// Materializes equivalence-class bitmasks on every `kCharClass` leaf and folds
// single-character alternation branches (`[a]|[b]|...`) into a single merged
// `kCharClass` node so macros like `{V_ANY}` compile into 1 NFA transition.
void SimplifyExprMasks(InvExprNode& node, int32_t num_classes,
                       const std::vector<uint16_t>& class_reps) {
  for (InvExprNode& child : node.children) {
    SimplifyExprMasks(child, num_classes, class_reps);
  }

  if (node.kind == InvExprNode::Kind::kCharClass) {
    if (node.ec_mask.empty()) {
      node.ec_mask.assign(static_cast<size_t>(num_classes), 0);
      for (int32_t ec = 0; ec < num_classes; ++ec) {
        if (node.pred.MatchesLower(class_reps[static_cast<size_t>(ec)])) {
          node.ec_mask[static_cast<size_t>(ec)] = 1;
        }
      }
    }
    return;
  }

  if (node.kind == InvExprNode::Kind::kAlt) {
    std::vector<uint8_t> merged_mask(static_cast<size_t>(num_classes), 0);
    bool has_char_class = false;
    std::vector<InvExprNode> remaining;
    remaining.reserve(node.children.size());

    for (InvExprNode& branch : node.children) {
      if (branch.kind == InvExprNode::Kind::kAlt) {
        for (InvExprNode& nested : branch.children) {
          if (nested.kind == InvExprNode::Kind::kCharClass) {
            has_char_class = true;
            for (int32_t ec = 0; ec < num_classes; ++ec) {
              merged_mask[static_cast<size_t>(ec)] |=
                  nested.ec_mask[static_cast<size_t>(ec)];
            }
          } else {
            remaining.push_back(std::move(nested));
          }
        }
      } else if (branch.kind == InvExprNode::Kind::kCharClass) {
        has_char_class = true;
        for (int32_t ec = 0; ec < num_classes; ++ec) {
          merged_mask[static_cast<size_t>(ec)] |=
              branch.ec_mask[static_cast<size_t>(ec)];
        }
      } else {
        remaining.push_back(std::move(branch));
      }
    }

    if (has_char_class) {
      InvExprNode merged;
      merged.kind = InvExprNode::Kind::kCharClass;
      merged.line = node.line;
      merged.ec_mask = std::move(merged_mask);
      if (remaining.empty()) {
        node = std::move(merged);
        return;
      }
      remaining.insert(remaining.begin(), std::move(merged));
    }
    node.children = std::move(remaining);
    if (node.children.size() == 1) {
      InvExprNode single = std::move(node.children[0]);
      node = std::move(single);
    }
  }
}

// Thompson NFA builder over equivalence classes `0..<num_classes` with shared
// unanchored-prefix (`.*`) and absorbing-accept (`.*` suffix) states.
struct NfaState {
  std::vector<int32_t> epsilon_outs;
  std::vector<uint8_t> class_mask;  // size == num_classes when `class_out >= 0`
  int32_t class_out = -1;
  bool is_accept = false;
};

struct NfaFragment {
  int32_t start = 0;
  int32_t accept = 0;
};

class NfaBuilder {
 public:
  explicit NfaBuilder(int32_t num_classes) : num_classes_(num_classes) {
    root_start_ = NewState();
    unanchored_start_ = NewState();
    states_[static_cast<size_t>(unanchored_start_)].class_mask.assign(
        static_cast<size_t>(num_classes_), 1);
    states_[static_cast<size_t>(unanchored_start_)].class_out =
        unanchored_start_;

    absorbing_accept_ = NewState();
    states_[static_cast<size_t>(absorbing_accept_)].class_mask.assign(
        static_cast<size_t>(num_classes_), 1);
    states_[static_cast<size_t>(absorbing_accept_)].class_out =
        absorbing_accept_;
    states_[static_cast<size_t>(absorbing_accept_)].is_accept = true;
  }

  int32_t root_start() const noexcept { return root_start_; }
  int32_t absorbing_accept() const noexcept { return absorbing_accept_; }
  const std::vector<NfaState>& states() const noexcept { return states_; }

  void AddPattern(const InvPatternRule& rule) {
    const NfaFragment frag = BuildNode(rule.expr);
    if (rule.anchor_start) {
      states_[static_cast<size_t>(root_start_)].epsilon_outs.push_back(
          frag.start);
    } else {
      if (!has_unanchored_start_) {
        has_unanchored_start_ = true;
        states_[static_cast<size_t>(root_start_)].epsilon_outs.push_back(
            unanchored_start_);
      }
      states_[static_cast<size_t>(unanchored_start_)].epsilon_outs.push_back(
          frag.start);
    }

    if (rule.anchor_end) {
      states_[static_cast<size_t>(frag.accept)].is_accept = true;
    } else {
      states_[static_cast<size_t>(frag.accept)].epsilon_outs.push_back(
          absorbing_accept_);
    }
  }

 private:
  int32_t NewState() {
    const int32_t id = static_cast<int32_t>(states_.size());
    states_.emplace_back();
    return id;
  }

  NfaFragment BuildNode(const InvExprNode& node) {
    switch (node.kind) {
      case InvExprNode::Kind::kEmpty:
      case InvExprNode::Kind::kMacroRef: {
        const int32_t s = NewState();
        const int32_t e = NewState();
        states_[static_cast<size_t>(s)].epsilon_outs.push_back(e);
        return {s, e};
      }
      case InvExprNode::Kind::kCharClass: {
        const int32_t s = NewState();
        const int32_t e = NewState();
        states_[static_cast<size_t>(s)].class_mask = node.ec_mask;
        states_[static_cast<size_t>(s)].class_out = e;
        return {s, e};
      }
      case InvExprNode::Kind::kConcat: {
        NfaFragment cur = BuildNode(node.children[0]);
        for (size_t i = 1; i < node.children.size(); ++i) {
          const NfaFragment next = BuildNode(node.children[i]);
          states_[static_cast<size_t>(cur.accept)].epsilon_outs.push_back(
              next.start);
          cur.accept = next.accept;
        }
        return cur;
      }
      case InvExprNode::Kind::kAlt: {
        const int32_t s = NewState();
        const int32_t e = NewState();
        for (const InvExprNode& branch : node.children) {
          const NfaFragment sub = BuildNode(branch);
          states_[static_cast<size_t>(s)].epsilon_outs.push_back(sub.start);
          states_[static_cast<size_t>(sub.accept)].epsilon_outs.push_back(e);
        }
        return {s, e};
      }
      case InvExprNode::Kind::kStar: {
        const int32_t s = NewState();
        const int32_t e = NewState();
        const NfaFragment sub = BuildNode(node.children[0]);
        states_[static_cast<size_t>(s)].epsilon_outs.push_back(sub.start);
        states_[static_cast<size_t>(s)].epsilon_outs.push_back(e);
        states_[static_cast<size_t>(sub.accept)].epsilon_outs.push_back(
            sub.start);
        states_[static_cast<size_t>(sub.accept)].epsilon_outs.push_back(e);
        return {s, e};
      }
      case InvExprNode::Kind::kPlus: {
        const int32_t s = NewState();
        const int32_t e = NewState();
        const NfaFragment sub = BuildNode(node.children[0]);
        states_[static_cast<size_t>(s)].epsilon_outs.push_back(sub.start);
        states_[static_cast<size_t>(sub.accept)].epsilon_outs.push_back(
            sub.start);
        states_[static_cast<size_t>(sub.accept)].epsilon_outs.push_back(e);
        return {s, e};
      }
      case InvExprNode::Kind::kQuest: {
        const int32_t s = NewState();
        const int32_t e = NewState();
        const NfaFragment sub = BuildNode(node.children[0]);
        states_[static_cast<size_t>(s)].epsilon_outs.push_back(sub.start);
        states_[static_cast<size_t>(s)].epsilon_outs.push_back(e);
        states_[static_cast<size_t>(sub.accept)].epsilon_outs.push_back(e);
        return {s, e};
      }
    }
    const int32_t s = NewState();
    return {s, s};
  }

  std::vector<NfaState> states_;
  int32_t num_classes_ = 1;
  int32_t root_start_ = 0;
  int32_t unanchored_start_ = 0;
  int32_t absorbing_accept_ = 0;
  bool has_unanchored_start_ = false;
};

void EpsilonClosure(const std::vector<NfaState>& nfa, int32_t absorbing_accept,
                    const std::vector<int32_t>& seeds,
                    std::vector<uint8_t>& seen_scratch,
                    std::vector<int32_t>& stack_scratch,
                    std::vector<int32_t>& out_sorted, bool& out_accept) {
  std::fill(seen_scratch.begin(), seen_scratch.end(), uint8_t{0});
  stack_scratch.clear();
  out_sorted.clear();
  out_accept = false;

  for (int32_t s : seeds) {
    if (seen_scratch[static_cast<size_t>(s)] == 0) {
      seen_scratch[static_cast<size_t>(s)] = 1;
      stack_scratch.push_back(s);
    }
  }

  while (!stack_scratch.empty()) {
    const int32_t u = stack_scratch.back();
    stack_scratch.pop_back();
    if (u == absorbing_accept) {
      out_sorted.assign(1, absorbing_accept);
      out_accept = true;
      return;
    }
    out_sorted.push_back(u);
    if (nfa[static_cast<size_t>(u)].is_accept) {
      out_accept = true;
    }
    for (int32_t v : nfa[static_cast<size_t>(u)].epsilon_outs) {
      if (seen_scratch[static_cast<size_t>(v)] == 0) {
        seen_scratch[static_cast<size_t>(v)] = 1;
        stack_scratch.push_back(v);
      }
    }
  }
  std::sort(out_sorted.begin(), out_sorted.end());
}

void DeterminizeAndMinimize(const NfaBuilder& nfa_builder, int32_t num_classes,
                            CompiledInvDfa& out_dfa) {
  const std::vector<NfaState>& nfa = nfa_builder.states();
  const int32_t absorbing_nfa = nfa_builder.absorbing_accept();
  absl::flat_hash_map<std::vector<int32_t>, int32_t> subset_to_id;
  std::vector<std::vector<int32_t>> id_to_subset;
  std::vector<int32_t> raw_trans;
  std::vector<bool> raw_accept;

  std::vector<uint8_t> seen_scratch(nfa.size(), 0);
  std::vector<int32_t> stack_scratch;
  stack_scratch.reserve(nfa.size());

  // State 0: empty dead state.
  id_to_subset.emplace_back();
  raw_accept.push_back(false);
  raw_trans.resize(static_cast<size_t>(num_classes), 0);
  subset_to_id.emplace(std::vector<int32_t>{}, 0);

  // State 1: epsilon-closure of NFA start state.
  std::vector<int32_t> start_set;
  bool start_accept = false;
  EpsilonClosure(nfa, absorbing_nfa, {nfa_builder.root_start()}, seen_scratch,
                 stack_scratch, start_set, start_accept);
  if (start_set.empty()) {
    out_dfa.state_count = 1;
    out_dfa.transitions.assign(static_cast<size_t>(num_classes), 0);
    out_dfa.accepting = {false};
    out_dfa.absorbing_accept = {false};
    return;
  }

  id_to_subset.push_back(start_set);
  raw_accept.push_back(start_accept);
  raw_trans.resize(static_cast<size_t>(2 * num_classes), 0);
  subset_to_id.emplace(start_set, 1);

  std::queue<int32_t> work;
  work.push(1);
  std::vector<int32_t> move_seeds;
  std::vector<int32_t> closed_next;

  while (!work.empty()) {
    const int32_t dfa_s = work.front();
    work.pop();
    const std::vector<int32_t> current_nfa_set =
        id_to_subset[static_cast<size_t>(dfa_s)];

    if (current_nfa_set.size() == 1 && current_nfa_set[0] == absorbing_nfa) {
      for (int32_t ec = 0; ec < num_classes; ++ec) {
        raw_trans[static_cast<size_t>(dfa_s * num_classes + ec)] = dfa_s;
      }
      continue;
    }

    for (int32_t ec = 0; ec < num_classes; ++ec) {
      move_seeds.clear();
      for (int32_t nfa_s : current_nfa_set) {
        const NfaState& st = nfa[static_cast<size_t>(nfa_s)];
        if (st.class_out >= 0 && st.class_mask[static_cast<size_t>(ec)] != 0) {
          move_seeds.push_back(st.class_out);
        }
      }
      if (move_seeds.empty()) {
        raw_trans[static_cast<size_t>(dfa_s * num_classes + ec)] = 0;
        continue;
      }
      bool next_accept = false;
      EpsilonClosure(nfa, absorbing_nfa, move_seeds, seen_scratch,
                     stack_scratch, closed_next, next_accept);
      const int32_t candidate_id = static_cast<int32_t>(id_to_subset.size());
      auto [it, inserted] = subset_to_id.try_emplace(closed_next, candidate_id);
      if (inserted) {
        id_to_subset.push_back(closed_next);
        raw_accept.push_back(next_accept);
        raw_trans.resize(id_to_subset.size() * static_cast<size_t>(num_classes),
                         0);
        work.push(candidate_id);
      }
      raw_trans[static_cast<size_t>(dfa_s * num_classes + ec)] = it->second;
    }
  }

  // Moore partition-refinement DFA minimization preserving dead state 0 and
  // canonical BFS state numbering from start state 1.
  const int32_t raw_count = static_cast<int32_t>(id_to_subset.size());
  std::vector<int32_t> group_of(static_cast<size_t>(raw_count), 0);
  int32_t num_groups = 1;
  bool has_accept = false;
  for (int32_t s = 0; s < raw_count; ++s) {
    if (raw_accept[static_cast<size_t>(s)]) {
      has_accept = true;
      group_of[static_cast<size_t>(s)] = 1;
    }
  }
  if (has_accept) {
    num_groups = 2;
  }

  bool changed = true;
  std::vector<int32_t> next_group_of(static_cast<size_t>(raw_count), 0);
  std::vector<int32_t> sig(static_cast<size_t>(num_classes + 1), 0);
  absl::flat_hash_map<std::vector<int32_t>, int32_t> sig_to_group;
  while (changed) {
    changed = false;
    sig_to_group.clear();
    int32_t next_group_count = 0;

    for (int32_t s = 0; s < raw_count; ++s) {
      sig[0] = group_of[static_cast<size_t>(s)];
      for (int32_t ec = 0; ec < num_classes; ++ec) {
        const int32_t target =
            raw_trans[static_cast<size_t>(s * num_classes + ec)];
        sig[static_cast<size_t>(ec + 1)] =
            group_of[static_cast<size_t>(target)];
      }
      auto [it, inserted] = sig_to_group.try_emplace(sig, next_group_count);
      if (inserted) {
        ++next_group_count;
      }
      next_group_of[static_cast<size_t>(s)] = it->second;
    }

    if (next_group_count != num_groups) {
      changed = true;
      num_groups = next_group_count;
      group_of.swap(next_group_of);
    }
  }

  const int32_t dead_group = group_of[0];
  const int32_t start_group = group_of[1];

  std::vector<int32_t> group_rep(static_cast<size_t>(num_groups), -1);
  for (int32_t s = 0; s < raw_count; ++s) {
    const int32_t g = group_of[static_cast<size_t>(s)];
    if (group_rep[static_cast<size_t>(g)] < 0) {
      group_rep[static_cast<size_t>(g)] = s;
    }
  }

  std::vector<int32_t> group_remap(static_cast<size_t>(num_groups), -1);
  group_remap[static_cast<size_t>(dead_group)] = 0;
  int32_t next_canon_id = 1;

  if (start_group != dead_group) {
    group_remap[static_cast<size_t>(start_group)] = next_canon_id++;
    std::queue<int32_t> bfs;
    bfs.push(start_group);
    while (!bfs.empty()) {
      const int32_t g = bfs.front();
      bfs.pop();
      const int32_t rep = group_rep[static_cast<size_t>(g)];
      for (int32_t ec = 0; ec < num_classes; ++ec) {
        const int32_t target_raw =
            raw_trans[static_cast<size_t>(rep * num_classes + ec)];
        const int32_t target_g = group_of[static_cast<size_t>(target_raw)];
        if (group_remap[static_cast<size_t>(target_g)] < 0) {
          group_remap[static_cast<size_t>(target_g)] = next_canon_id++;
          bfs.push(target_g);
        }
      }
    }
  }

  out_dfa.state_count = next_canon_id;
  out_dfa.transitions.assign(
      static_cast<size_t>(out_dfa.state_count * num_classes), 0);
  out_dfa.accepting.assign(static_cast<size_t>(out_dfa.state_count), false);
  out_dfa.absorbing_accept.assign(static_cast<size_t>(out_dfa.state_count),
                                  false);

  for (int32_t g = 0; g < num_groups; ++g) {
    const int32_t canon = group_remap[static_cast<size_t>(g)];
    if (canon < 0) {
      continue;
    }
    const int32_t rep = group_rep[static_cast<size_t>(g)];
    out_dfa.accepting[static_cast<size_t>(canon)] =
        raw_accept[static_cast<size_t>(rep)];
    bool all_self = raw_accept[static_cast<size_t>(rep)];
    for (int32_t ec = 0; ec < num_classes; ++ec) {
      const int32_t target_raw =
          raw_trans[static_cast<size_t>(rep * num_classes + ec)];
      const int32_t target_canon = group_remap[static_cast<size_t>(
          group_of[static_cast<size_t>(target_raw)])];
      out_dfa.transitions[static_cast<size_t>(canon * num_classes + ec)] =
          target_canon;
      if (target_canon != canon) {
        all_self = false;
      }
    }
    out_dfa.absorbing_accept[static_cast<size_t>(canon)] = all_self;
  }
}

std::string Hex16(uint16_t c) {
  return absl::StrCat("0x", absl::Hex(c, absl::kZeroPad4));
}

std::string HeaderGuardName(std::string_view struct_name) {
  std::string upper;
  upper.reserve(struct_name.size());
  for (unsigned char c : struct_name) {
    if (std::isalnum(c)) {
      upper.push_back(static_cast<char>(std::toupper(c)));
    } else {
      upper.push_back('_');
    }
  }
  return absl::Substitute("OKEYTRE_ENGINE_$0_GENERATED_H_", upper);
}

void AppendLine(absl::Cord& cord, std::string_view line = "") {
  cord.Append(line);
  cord.Append("\n");
}

void EmitCaseLabels(const std::vector<uint16_t>& chars, std::string_view indent,
                    absl::Cord& out) {
  constexpr size_t kChunk = 8;
  size_t idx = 0;
  while (idx < chars.size()) {
    const size_t end = std::min(idx + kChunk, chars.size());
    std::string line(indent);
    for (size_t i = idx; i < end; ++i) {
      if (i > idx) {
        absl::StrAppend(&line, " ");
      }
      absl::StrAppend(&line, "case ", Hex16(chars[i]), ":");
    }
    AppendLine(out, line);
    idx = end;
  }
}

}  // namespace

absl::StatusOr<CompiledInvDfa> CompileInvSpec(std::string_view source) {
  absl::StatusOr<ParsedInvSpec> parsed = ParseInvSpec(source);
  if (!parsed.ok()) {
    return parsed.status();
  }

  for (InvPatternRule& rule : parsed->rules) {
    absl::flat_hash_set<std::string> active_stack;
    const absl::Status st =
        ExpandMacros(rule.expr, parsed->macros, active_stack);
    if (!st.ok()) {
      return st;
    }
  }

  CompiledInvDfa compiled;
  compiled.name = std::move(parsed->name);
  compiled.assertions = std::move(parsed->assertions);
  compiled.pattern_count = static_cast<int32_t>(parsed->rules.size());

  std::vector<uint16_t> class_reps;
  BuildEquivalenceClasses(parsed->rules, compiled.ec_table,
                          compiled.num_classes, class_reps);

  for (InvPatternRule& rule : parsed->rules) {
    SimplifyExprMasks(rule.expr, compiled.num_classes, class_reps);
  }

  NfaBuilder nfa_builder(compiled.num_classes);
  for (const InvPatternRule& rule : parsed->rules) {
    nfa_builder.AddPattern(rule);
  }
  DeterminizeAndMinimize(nfa_builder, compiled.num_classes, compiled);
  return compiled;
}

absl::Status VerifyInvAssertions(const CompiledInvDfa& compiled,
                                 std::string_view input_path) {
  for (const InvTestAssertion& item : compiled.assertions) {
    const std::u16string u16 = InvUtf8ToU16(item.word_utf8);
    const bool matched = compiled.Matches(u16);
    if (matched != item.expect_match) {
      const std::string_view expected_label =
          item.expect_match ? "reject" : "accept";
      const std::string_view actual_label = matched ? "reject" : "accept";
      return absl::FailedPreconditionError(absl::Substitute(
          "$0:$1: assertion failed for \"$2\" (expected $3, got $4)",
          input_path, item.line, item.word_utf8, expected_label, actual_label));
    }
  }
  return absl::OkStatus();
}

absl::Cord GenerateCppInvAutomaton(const CompiledInvDfa& compiled,
                                   std::string_view struct_name) {
  absl::Cord out;
  const std::string guard = HeaderGuardName(struct_name);

  AppendLine(out, "// Auto-generated by invc. DO NOT EDIT.");
  if (!compiled.name.empty()) {
    AppendLine(out, absl::Substitute("// Specification: $0", compiled.name));
  }
  AppendLine(out, absl::Substitute(
                      "// Minimized DFA: $0 states, $1 equivalence classes",
                      compiled.state_count, compiled.num_classes));
  AppendLine(out);
  AppendLine(out, absl::Substitute("#ifndef $0", guard));
  AppendLine(out, absl::Substitute("#define $0", guard));
  AppendLine(out);
  AppendLine(out, "#include <cstdint>");
  AppendLine(out);
  AppendLine(out, "namespace okeytre {");
  AppendLine(out);
  AppendLine(out, absl::Substitute("struct $0 {", struct_name));

  // 1. Equivalence-class lookup switch (`Classify`).
  absl::btree_map<int32_t, std::vector<uint16_t>> ec_groups;
  for (uint32_t code = 0; code < 65536; ++code) {
    const int32_t ec = static_cast<int32_t>(compiled.ec_table[code]);
    if (ec != 0) {
      ec_groups[ec].push_back(static_cast<uint16_t>(code));
    }
  }
  AppendLine(out, "  static inline int32_t Classify(uint16_t c) noexcept {");
  if (ec_groups.empty()) {
    AppendLine(out, "    (void)c;");
    AppendLine(out, "    return 0;");
  } else {
    AppendLine(out, "    switch (c) {");
    for (const auto& [ec, chars] : ec_groups) {
      EmitCaseLabels(chars, "      ", out);
      AppendLine(out, absl::Substitute("        return $0;", ec));
    }
    AppendLine(out, "      default:");
    AppendLine(out, "        return 0;");
    AppendLine(out, "    }");
  }
  AppendLine(out, "  }");
  AppendLine(out);

  // 2. Minimized DFA transition table & `Matches`.
  AppendLine(
      out,
      "  static inline bool Matches(const uint16_t* word, int32_t word_len) "
      "noexcept {");
  if (compiled.state_count <= 1) {
    AppendLine(out, "    (void)word;");
    AppendLine(out, "    (void)word_len;");
    AppendLine(out, "    return false;");
    AppendLine(out, "  }");
    AppendLine(out, "};");
    AppendLine(out);
    AppendLine(out, "}  // namespace okeytre");
    AppendLine(out);
    AppendLine(out, absl::Substitute("#endif  // $0", guard));
    return out;
  }

  AppendLine(out, "    if (word == nullptr || word_len <= 0) return false;");
  const std::string_view state_elem_type =
      compiled.state_count <= 255 ? "uint8_t" : "uint16_t";
  AppendLine(out, absl::Substitute("    static constexpr $0 kTrans[] = {",
                                   state_elem_type));
  constexpr size_t kRowChunk = 24;
  size_t offset = 0;
  while (offset < compiled.transitions.size()) {
    const size_t end =
        std::min(offset + kRowChunk, compiled.transitions.size());
    std::string row = "        ";
    for (size_t i = offset; i < end; ++i) {
      if (i > offset) {
        absl::StrAppend(&row, ", ");
      }
      absl::StrAppend(&row, compiled.transitions[i]);
    }
    if (end < compiled.transitions.size()) {
      absl::StrAppend(&row, ",");
    }
    AppendLine(out, row);
    offset = end;
  }
  AppendLine(out, "    };");

  // State flags: bit 0 = accepting, bit 1 = absorbing_accept.
  std::string flags_row = "    static constexpr uint8_t kStateFlags[] = {";
  for (int32_t s = 0; s < compiled.state_count; ++s) {
    if (s > 0) {
      absl::StrAppend(&flags_row, ", ");
    }
    const uint8_t bits =
        (compiled.accepting[static_cast<size_t>(s)] ? 1u : 0u) |
        (compiled.absorbing_accept[static_cast<size_t>(s)] ? 2u : 0u);
    absl::StrAppend(&flags_row, static_cast<int>(bits));
  }
  absl::StrAppend(&flags_row, "};");
  AppendLine(out, flags_row);

  AppendLine(out, "    int32_t state = 1;");
  AppendLine(out, "    for (int32_t i = 0; i < word_len; ++i) {");
  AppendLine(out, absl::Substitute(
                      "      state = static_cast<int32_t>(kTrans[state * $0 + "
                      "Classify(word[i])]);",
                      compiled.num_classes));
  AppendLine(out, "      if (state == 0) return false;");
  AppendLine(out, "      if ((kStateFlags[state] & 2u) != 0u) return true;");
  AppendLine(out, "    }");
  AppendLine(out, "    return (kStateFlags[state] & 1u) != 0u;");
  AppendLine(out, "  }");
  AppendLine(out, "};");
  AppendLine(out);
  AppendLine(out, "}  // namespace okeytre");
  AppendLine(out);
  AppendLine(out, absl::Substitute("#endif  // $0", guard));
  return out;
}

}  // namespace okeytre
