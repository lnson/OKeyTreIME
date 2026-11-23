// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "cpp-automaton-codegen.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"

namespace okeytre {

namespace {

class Sink {
 public:
  absl::Cord Output() && { return std::move(data_); }

  void WriteLn() { data_.Append("\n"); }

  void WriteLn(std::string_view line) {
    data_.Append(line);
    data_.Append("\n");
  }

  template <typename FormatStr, typename Arg0, typename... Args>
  void WriteLn(FormatStr&& format, Arg0&& arg0, Args&&... args) {
    data_.Append(absl::Substitute(std::forward<FormatStr>(format),
                                  std::forward<Arg0>(arg0),
                                  std::forward<Args>(args)...));
    data_.Append("\n");
  }

 private:
  absl::Cord data_;
};

std::string Hex16(uint16_t c) {
  return absl::StrCat("0x", absl::Hex(c, absl::kZeroPad4));
}

std::string Hex64(uint64_t v) {
  return absl::StrCat("0x", absl::Hex(v, absl::kZeroPad16), "ULL");
}

std::string HeaderGuardName(std::string_view type_name) {
  std::string upper;
  upper.reserve(type_name.size());
  for (unsigned char c : type_name) {
    if (std::isalnum(c)) {
      upper.push_back(static_cast<char>(std::toupper(c)));
    } else {
      upper.push_back('_');
    }
  }
  return absl::Substitute("OKEYTRE_ENGINE_$0_GENERATED_H_", upper);
}

void EmitCaseLabels(const std::vector<uint16_t>& chars, std::string_view indent,
                    Sink& sink) {
  constexpr size_t kChunkSize = 8;
  size_t idx = 0;
  while (idx < chars.size()) {
    size_t end = std::min(idx + kChunkSize, chars.size());
    std::string line(indent);
    for (size_t i = idx; i < end; ++i) {
      if (i > idx) absl::StrAppend(&line, " ");
      absl::StrAppend(&line, "case ", Hex16(chars[i]), ":");
    }
    sink.WriteLn(line);
    idx = end;
  }
}

bool DfaUsesFlags(const KeyAutomaton& dfa) {
  for (const std::vector<KeyAutomaton::AcceptEntry>& chain :
       dfa.unanchored_accept) {
    for (const KeyAutomaton::AcceptEntry& acc : chain) {
      if (acc.IsValid() && !acc.flag_name.empty()) return true;
    }
  }
  for (const std::vector<KeyAutomaton::AcceptEntry>& chain :
       dfa.anchored_accept) {
    for (const KeyAutomaton::AcceptEntry& acc : chain) {
      if (acc.IsValid() && !acc.flag_name.empty()) return true;
    }
  }
  return false;
}

std::string MapFunctionName(int map_id, std::string_view map_name,
                            bool inverted, bool uses_input_key) {
  std::string_view dir = inverted ? "Inv" : "Fwd";
  std::string_view key_tag = uses_input_key ? "_Key" : "";
  return absl::Substitute("Map_$0_$1_$2$3", map_id, map_name, dir, key_tag);
}

// 1. Entry Point (`RewriteToBuffer`)

void EmitRewriteEntryPoint(const CompiledAutomaton& compiled, Sink& sink) {
  size_t max_capture_refs = 1;
  size_t max_fixed_extra = 0;
  for (const CompiledRule& rule : compiled.rules) {
    size_t capture_refs = 0;
    size_t fixed_extra = 0;
    for (const CompiledRHSItem& item : rule.rhs_items) {
      switch (item.kind) {
        case CompiledRHSItem::Kind::kCapture:
        case CompiledRHSItem::Kind::kMapProjection:
          ++capture_refs;
          break;
        case CompiledRHSItem::Kind::kLiteral:
        case CompiledRHSItem::Kind::kInputKey:
          ++fixed_extra;
          break;
      }
    }
    max_capture_refs = std::max(max_capture_refs, capture_refs);
    max_fixed_extra = std::max(max_fixed_extra, fixed_extra);
  }

  sink.WriteLn("  inline int32_t RewriteToBuffer(const uint16_t* word,");
  sink.WriteLn("                                 int32_t word_len,");
  sink.WriteLn("                                 uint16_t input,");
  sink.WriteLn("                                 uint16_t* out_buf,");
  sink.WriteLn(
      "                                 int32_t out_capacity) const noexcept "
      "{");

  if (compiled.key_automata.empty() || compiled.rules.empty()) {
    sink.WriteLn("    (void)word;");
    sink.WriteLn("    (void)word_len;");
    sink.WriteLn("    (void)input;");
    sink.WriteLn("    (void)out_buf;");
    sink.WriteLn("    (void)out_capacity;");
    sink.WriteLn("    return -1;");
    sink.WriteLn("  }");
    return;
  }

  int default_auto = static_cast<int>(compiled.trigger_table[0xFFFD]);
  absl::btree_map<int, std::vector<uint16_t>> groups;
  for (uint32_t code = 0; code < 65536; ++code) {
    int idx = static_cast<int>(compiled.trigger_table[code]);
    if (idx != default_auto) {
      groups[idx].push_back(static_cast<uint16_t>(code));
    }
  }

  sink.WriteLn("    int32_t rule_id = -1;");
  sink.WriteLn("    int32_t match_start = 0;");
  sink.WriteLn("    switch (input) {");
  for (const auto& [auto_idx, chars] : groups) {
    if (chars.empty()) continue;
    if (auto_idx < 0) {
      EmitCaseLabels(chars, "      ", sink);
      sink.WriteLn("        return -1;");
    } else {
      const KeyAutomaton& dfa =
          compiled.key_automata[static_cast<size_t>(auto_idx)];
      std::string_view call_prefix = DfaUsesFlags(dfa) ? "this->" : "";
      EmitCaseLabels(chars, "      ", sink);
      sink.WriteLn(
          "        if (!$0MatchDfa_$1(word, word_len, rule_id, match_start)) "
          "return -1;",
          call_prefix, auto_idx);
      sink.WriteLn("        break;");
    }
  }
  if (default_auto >= 0) {
    const KeyAutomaton& dfa =
        compiled.key_automata[static_cast<size_t>(default_auto)];
    std::string_view call_prefix = DfaUsesFlags(dfa) ? "this->" : "";
    sink.WriteLn("      default:");
    sink.WriteLn(
        "        if (!$0MatchDfa_$1(word, word_len, rule_id, match_start)) "
        "return -1;",
        call_prefix, default_auto);
    sink.WriteLn("        break;");
  } else {
    sink.WriteLn("      default:");
    sink.WriteLn("        return -1;");
  }
  sink.WriteLn("    }");
  sink.WriteLn();
  sink.WriteLn("    const uint16_t* matched_slice = word + match_start;");
  sink.WriteLn("    const int32_t slice_len = word_len - match_start;");
  sink.WriteLn("    const int32_t max_repl_cap = slice_len * $0 + $1;",
               max_capture_refs, max_fixed_extra);
  sink.WriteLn("    uint16_t stack_repl_buf[2600];");
  sink.WriteLn("    std::vector<uint16_t> heap_repl_buf;");
  sink.WriteLn("    uint16_t* repl_ptr = stack_repl_buf;");
  sink.WriteLn("    if (max_repl_cap > 2600) {");
  sink.WriteLn(
      "      heap_repl_buf.resize(static_cast<size_t>(max_repl_cap));");
  sink.WriteLn("      repl_ptr = heap_repl_buf.data();");
  sink.WriteLn("    }");
  sink.WriteLn("    const int32_t repl_len = ApplyRule(");
  sink.WriteLn(
      "        rule_id, matched_slice, slice_len, input, repl_ptr, "
      "max_repl_cap);");
  sink.WriteLn("    if (repl_len < 0) return -1;");
  sink.WriteLn();
  sink.WriteLn("    const int32_t total_len = match_start + repl_len;");
  sink.WriteLn("    if (out_buf != nullptr && total_len <= out_capacity) {");
  sink.WriteLn("      if (match_start > 0) {");
  sink.WriteLn("        std::memcpy(out_buf, word,");
  sink.WriteLn(
      "                    static_cast<size_t>(match_start) * "
      "sizeof(uint16_t));");
  sink.WriteLn("      }");
  sink.WriteLn("      if (repl_len > 0) {");
  sink.WriteLn("        std::memcpy(out_buf + match_start, repl_ptr,");
  sink.WriteLn(
      "                    static_cast<size_t>(repl_len) * sizeof(uint16_t));");
  sink.WriteLn("      }");
  sink.WriteLn("    }");
  sink.WriteLn("    return total_len;");
  sink.WriteLn("  }");
}

// 2. Equivalence Classifier (`Classify`)

void EmitClassifier(const CompiledAutomaton& compiled, Sink& sink) {
  absl::btree_map<int, std::vector<uint16_t>> ec_groups;
  for (uint32_t code = 0; code < 65536; ++code) {
    int ec = static_cast<int>(compiled.ec_table[code]);
    if (ec != 0) {
      ec_groups[ec].push_back(static_cast<uint16_t>(code));
    }
  }

  sink.WriteLn("  static inline int32_t Classify(uint16_t c) noexcept {");
  if (ec_groups.empty()) {
    sink.WriteLn("    (void)c;");
    sink.WriteLn("    return 0;");
  } else {
    sink.WriteLn("    switch (c) {");
    for (const auto& [ec, chars] : ec_groups) {
      if (chars.empty()) continue;
      EmitCaseLabels(chars, "      ", sink);
      sink.WriteLn("        return $0;", ec);
    }
    sink.WriteLn("      default:");
    sink.WriteLn("        return 0;");
    sink.WriteLn("    }");
  }
  sink.WriteLn("  }");
}

// 3. Fast Case Helpers

void CheckNeedsCaseHelpers(const CompiledAutomaton& compiled,
                           bool& out_needs_is_upper,
                           bool& out_needs_fast_to_lower) {
  out_needs_is_upper = false;
  out_needs_fast_to_lower = false;
  for (const CompiledRule& rule : compiled.rules) {
    if (rule.rhs_literal_count > 0) {
      out_needs_is_upper = true;
    }
    for (const CompiledRHSItem& item : rule.rhs_items) {
      if (item.kind == CompiledRHSItem::Kind::kMapProjection &&
          item.uses_input_key) {
        out_needs_fast_to_lower = true;
      }
    }
  }
}

void EmitIsUpperHelper(const CompiledAutomaton& compiled, Sink& sink) {
  absl::btree_set<uint16_t> non_ascii_uppers;
  for (uint32_t code = 0x0080; code < 65536; ++code) {
    uint16_t ch = static_cast<uint16_t>(code);
    if (compiled.ec_table[code] != 0 || compiled.trigger_table[code] >= 0) {
      if (IsUpper(ch)) {
        non_ascii_uppers.insert(ch);
      }
    }
  }
  for (const CharacterMap& map : compiled.maps) {
    for (uint16_t c : map.domain_set) {
      uint16_t u = ToUpper(c);
      if (u >= 0x0080 && u != c) non_ascii_uppers.insert(u);
    }
    for (uint16_t c : map.inverse_domain_set) {
      uint16_t u = ToUpper(c);
      if (u >= 0x0080 && u != c) non_ascii_uppers.insert(u);
    }
  }

  sink.WriteLn("  static inline bool IsUpperChar(uint16_t c) noexcept {");
  sink.WriteLn("    if (c >= 0x0041 && c <= 0x005A) return true;");
  sink.WriteLn("    if (c < 0x0080) return false;");
  if (!non_ascii_uppers.empty()) {
    std::vector<uint16_t> sorted_uppers(non_ascii_uppers.begin(),
                                        non_ascii_uppers.end());
    sink.WriteLn("    switch (c) {");
    EmitCaseLabels(sorted_uppers, "      ", sink);
    sink.WriteLn("        return true;");
    sink.WriteLn("      default:");
    sink.WriteLn("        return okeytre::IsUpper(c);");
    sink.WriteLn("    }");
  } else {
    sink.WriteLn("    return okeytre::IsUpper(c);");
  }
  sink.WriteLn("  }");
}

void EmitFastToLowerHelper(Sink& sink) {
  sink.WriteLn("  static inline uint16_t FastToLower(uint16_t c) noexcept {");
  sink.WriteLn(
      "    if (c >= 0x0041 && c <= 0x005A) return static_cast<uint16_t>(c + "
      "32);");
  sink.WriteLn("    if (c < 0x0080) return c;");
  sink.WriteLn("    return okeytre::ToLower(c);");
  sink.WriteLn("  }");
}

// 4. Case-Baked `%map` Functions

struct MapFuncUsage {
  int map_id;
  bool inverted;
  bool uses_input_key;

  bool operator<(const MapFuncUsage& o) const {
    if (map_id != o.map_id) return map_id < o.map_id;
    if (inverted != o.inverted) return !inverted && o.inverted;
    return !uses_input_key && o.uses_input_key;
  }
};

void EmitCasePreservingCharSwitch(
    const absl::flat_hash_map<uint16_t, uint16_t>& dict,
    std::string_view indent, Sink& sink) {
  absl::btree_map<uint16_t, absl::btree_set<uint16_t>> target_to_sources;
  for (const auto& [lower_src, lower_dst] : dict) {
    if (lower_src != lower_dst) {
      target_to_sources[lower_dst].insert(lower_src);
    }
    uint16_t upper_src = ToUpper(lower_src);
    uint16_t upper_dst = ToUpper(lower_dst);
    if (upper_src != lower_src && upper_src != upper_dst) {
      target_to_sources[upper_dst].insert(upper_src);
    }
  }

  if (target_to_sources.empty()) {
    sink.WriteLn("$0return c;", indent);
    return;
  }

  sink.WriteLn("$0switch (c) {", indent);
  std::string case_indent = absl::StrCat(indent, "  ");
  for (const auto& [target, source_set] : target_to_sources) {
    std::vector<uint16_t> sources(source_set.begin(), source_set.end());
    EmitCaseLabels(sources, case_indent, sink);
    sink.WriteLn("$0    return $1;", indent, Hex16(target));
  }
  sink.WriteLn("$0  default:", indent);
  sink.WriteLn("$0    return c;", indent);
  sink.WriteLn("$0}", indent);
}

void EmitMapFunctions(const CompiledAutomaton& compiled, Sink& sink) {
  absl::btree_set<MapFuncUsage> used;
  for (const CompiledRule& rule : compiled.rules) {
    for (const CompiledRHSItem& item : rule.rhs_items) {
      if (item.kind == CompiledRHSItem::Kind::kMapProjection) {
        used.insert(
            MapFuncUsage{item.map_id, item.inverted, item.uses_input_key});
      }
    }
  }

  for (const MapFuncUsage& usage : used) {
    const CharacterMap& map = compiled.maps[static_cast<size_t>(usage.map_id)];
    std::string func_name = MapFunctionName(
        usage.map_id, map.name, usage.inverted, usage.uses_input_key);

    if (usage.uses_input_key && map.is_2d) {
      sink.WriteLn(
          "  static inline uint16_t $0(uint16_t lower_key, uint16_t c) "
          "noexcept {",
          func_name);
      sink.WriteLn("    switch (lower_key) {");
      for (uint16_t key : map.keys) {
        sink.WriteLn("      case $0:", Hex16(key));
        const absl::flat_hash_map<uint16_t, uint16_t>* dict_ptr = nullptr;
        if (usage.inverted) {
          auto it = map.inverse_by_key.find(key);
          dict_ptr =
              (it != map.inverse_by_key.end()) ? &it->second : &map.inverse_all;
        } else {
          auto it = map.forward_by_key.find(key);
          if (it != map.forward_by_key.end()) dict_ptr = &it->second;
        }
        if (dict_ptr != nullptr) {
          EmitCasePreservingCharSwitch(*dict_ptr, "        ", sink);
        } else {
          sink.WriteLn("        return c;");
        }
      }
      sink.WriteLn("      default:");
      sink.WriteLn("        return c;");
      sink.WriteLn("    }");
      sink.WriteLn("  }");
      sink.WriteLn();
    } else {
      const absl::flat_hash_map<uint16_t, uint16_t>& dict =
          usage.inverted ? map.inverse_all : map.forward_1d;
      sink.WriteLn("  static inline uint16_t $0(uint16_t c) noexcept {",
                   func_name);
      EmitCasePreservingCharSwitch(dict, "    ", sink);
      sink.WriteLn("  }");
      sink.WriteLn();
    }
  }
}

// 5. Fallback `RuleMatcher` Functions

void EmitFallbackMatchers(const CompiledAutomaton& compiled, Sink& sink) {
  for (size_t rule_id = 0; rule_id < compiled.rules.size(); ++rule_id) {
    const CompiledRule& rule = compiled.rules[rule_id];
    if (rule.has_fixed_plan) continue;

    sink.WriteLn("  static inline bool MatchPikeVm_$0(", rule_id);
    sink.WriteLn("      const uint16_t* slice,");
    sink.WriteLn("      int32_t slice_len,");
    sink.WriteLn("      std::vector<int>& out_tags) {");
    sink.WriteLn("    static const okeytre::RuleMatcher kMatcher = {");
    sink.WriteLn("        {");
    for (const PikeVMInst& inst : rule.matcher.instructions) {
      switch (inst.op) {
        case PikeVMInst::Op::kMatch:
          sink.WriteLn("            okeytre::PikeVMInst::MakeMatch(),");
          break;
        case PikeVMInst::Op::kJump:
          sink.WriteLn("            okeytre::PikeVMInst::MakeJump($0),",
                       inst.arg1);
          break;
        case PikeVMInst::Op::kSplit:
          sink.WriteLn("            okeytre::PikeVMInst::MakeSplit($0, $1),",
                       inst.arg1, inst.arg2);
          break;
        case PikeVMInst::Op::kSaveTag:
          sink.WriteLn("            okeytre::PikeVMInst::MakeSaveTag($0),",
                       inst.arg1);
          break;
        case PikeVMInst::Op::kCharClass: {
          uint64_t lo = inst.char_class.LowWord();
          uint64_t hi = inst.char_class.HighWord();
          if (hi == 0) {
            sink.WriteLn(
                "            "
                "okeytre::PikeVMInst::MakeCharClass(okeytre::ECBitSet($0)),",
                Hex64(lo));
          } else {
            sink.WriteLn(
                "            "
                "okeytre::PikeVMInst::MakeCharClass(okeytre::"
                "ECBitSet($0, $1)),",
                Hex64(lo), Hex64(hi));
          }
          break;
        }
      }
    }
    sink.WriteLn("        },");
    sink.WriteLn("        $0", rule.matcher.num_tags);
    sink.WriteLn("    };");
    sink.WriteLn("    return kMatcher.MatchSliceWithClassifier(");
    sink.WriteLn("        slice, static_cast<int>(slice_len),");
    sink.WriteLn(
        "        [](uint16_t c) { return static_cast<int>(Classify(c)); }, "
        "out_tags);");
    sink.WriteLn("  }");
    sink.WriteLn();
  }
}

// 6. Direct-Coded Reverse DFA (`MatchDfa_K`)

void EmitAcceptSwitch(
    const std::vector<std::vector<KeyAutomaton::AcceptEntry>>& accepts,
    int state_count, std::string_view match_start_expr, bool updates_priority,
    std::string_view indent, Sink& sink) {
  absl::btree_map<std::vector<KeyAutomaton::AcceptEntry>, std::vector<int>>
      chain_to_states;
  for (int s = 1; s < state_count; ++s) {
    const std::vector<KeyAutomaton::AcceptEntry>& chain =
        accepts[static_cast<size_t>(s)];
    if (!chain.empty()) {
      chain_to_states[chain].push_back(s);
    }
  }

  sink.WriteLn("$0switch (state) {", indent);
  for (const auto& [chain, states] : chain_to_states) {
    std::string case_line = absl::StrCat(indent, "  ");
    for (size_t i = 0; i < states.size(); ++i) {
      if (i > 0) absl::StrAppend(&case_line, " ");
      absl::StrAppend(&case_line, "case ", states[i], ":");
    }
    sink.WriteLn(case_line);
    for (size_t i = 0; i < chain.size(); ++i) {
      const KeyAutomaton::AcceptEntry& entry = chain[i];
      std::string_view prefix = (i == 0) ? "if" : "} else if";
      if (!entry.flag_name.empty()) {
        sink.WriteLn("$0    $1 (this->$2 && $3 < best_priority) {", indent,
                     prefix, entry.flag_name, entry.priority);
      } else {
        sink.WriteLn("$0    $1 ($2 < best_priority) {", indent, prefix,
                     entry.priority);
      }
      if (updates_priority) {
        sink.WriteLn("$0      best_priority = $1;", indent, entry.priority);
      }
      sink.WriteLn("$0      best_rule_id = $1;", indent, entry.rule_id);
      sink.WriteLn("$0      best_match_start = $1;", indent, match_start_expr);
    }
    sink.WriteLn("$0    }", indent);
    sink.WriteLn("$0    break;", indent);
  }
  sink.WriteLn("$0  default: break;", indent);
  sink.WriteLn("$0}", indent);
}

void EmitStateTransitionSwitch(const KeyAutomaton& dfa, int state_count,
                               int num_classes, std::string_view indent,
                               Sink& sink) {
  sink.WriteLn("$0switch (state) {", indent);
  for (int s = 1; s < state_count; ++s) {
    absl::btree_map<int32_t, std::vector<int>> target_to_ecs;
    for (int ec = 0; ec < num_classes; ++ec) {
      int32_t next = dfa.transitions[static_cast<size_t>(s * num_classes + ec)];
      if (next != 0) {
        target_to_ecs[next].push_back(ec);
      }
    }
    if (target_to_ecs.empty()) continue;

    sink.WriteLn("$0  case $1:", indent, s);
    sink.WriteLn("$0    switch (ec) {", indent);
    for (const auto& [target, ecs] : target_to_ecs) {
      std::string cases = absl::StrCat(indent, "      ");
      for (size_t i = 0; i < ecs.size(); ++i) {
        if (i > 0) absl::StrAppend(&cases, " ");
        absl::StrAppend(&cases, "case ", ecs[i], ":");
      }
      absl::StrAppend(&cases, " state = ", target, "; break;");
      sink.WriteLn(cases);
    }
    sink.WriteLn("$0      default: state = 0; break;", indent);
    sink.WriteLn("$0    }", indent);
    sink.WriteLn("$0    break;", indent);
  }
  sink.WriteLn("$0  default: state = 0; break;", indent);
  sink.WriteLn("$0}", indent);
}

void EmitDFAFunction(size_t index, const KeyAutomaton& dfa, Sink& sink) {
  int state_count = dfa.StateCount();
  int num_classes = dfa.num_classes;
  bool has_unanchored = false;
  bool has_anchored = false;
  for (const std::vector<KeyAutomaton::AcceptEntry>& u :
       dfa.unanchored_accept) {
    if (!u.empty()) {
      has_unanchored = true;
      break;
    }
  }
  for (const std::vector<KeyAutomaton::AcceptEntry>& a : dfa.anchored_accept) {
    if (!a.empty()) {
      has_anchored = true;
      break;
    }
  }
  bool use_table = state_count > 8;
  bool uses_flags = DfaUsesFlags(dfa);
  std::string_view func_qual =
      uses_flags ? ") const noexcept {" : ") noexcept {";

  sink.WriteLn(uses_flags ? "  inline bool MatchDfa_$0("
                          : "  static inline bool MatchDfa_$0(",
               index);
  sink.WriteLn("      const uint16_t* word,");
  sink.WriteLn("      int32_t word_len,");
  sink.WriteLn("      int32_t& out_rule_id,");
  sink.WriteLn("      int32_t& out_match_start$0", func_qual);

  if (!has_unanchored && !has_anchored) {
    sink.WriteLn("    (void)word;");
    sink.WriteLn("    (void)word_len;");
    sink.WriteLn("    (void)out_rule_id;");
    sink.WriteLn("    (void)out_match_start;");
    sink.WriteLn("    return false;");
    sink.WriteLn("  }");
    return;
  }

  if (use_table) {
    std::string_view elem_type = state_count <= 255 ? "uint8_t" : "uint16_t";
    sink.WriteLn("    static constexpr $0 kTrans[] = {", elem_type);
    constexpr size_t kChunkSize = 24;
    size_t offset = 0;
    while (offset < dfa.transitions.size()) {
      size_t end = std::min(offset + kChunkSize, dfa.transitions.size());
      std::string row = "        ";
      for (size_t i = offset; i < end; ++i) {
        if (i > offset) absl::StrAppend(&row, ", ");
        absl::StrAppend(&row, dfa.transitions[i]);
      }
      if (end < dfa.transitions.size()) absl::StrAppend(&row, ",");
      sink.WriteLn(row);
      offset = end;
    }
    sink.WriteLn("    };");
  }

  // Fast pure-anchored path
  if (!has_unanchored && has_anchored) {
    sink.WriteLn("    int32_t state = 1;");
    sink.WriteLn("    int32_t cursor = word_len;");
    sink.WriteLn("    while (cursor > 0) {");
    sink.WriteLn("      --cursor;");
    sink.WriteLn("      const int32_t ec = Classify(word[cursor]);");
    if (use_table) {
      sink.WriteLn(
          "      state = static_cast<int32_t>(kTrans[state * $0 + ec]);",
          num_classes);
    } else {
      EmitStateTransitionSwitch(dfa, state_count, num_classes, "      ", sink);
    }
    sink.WriteLn("      if (state == 0) return false;");
    sink.WriteLn("    }");
    absl::btree_map<std::vector<KeyAutomaton::AcceptEntry>, std::vector<int>>
        anc_to_states;
    for (int s = 1; s < state_count; ++s) {
      const std::vector<KeyAutomaton::AcceptEntry>& chain =
          dfa.anchored_accept[static_cast<size_t>(s)];
      if (!chain.empty()) {
        anc_to_states[chain].push_back(s);
      }
    }
    sink.WriteLn("    switch (state) {");
    for (const auto& [chain, states] : anc_to_states) {
      std::string case_line = "      ";
      for (size_t i = 0; i < states.size(); ++i) {
        if (i > 0) absl::StrAppend(&case_line, " ");
        absl::StrAppend(&case_line, "case ", states[i], ":");
      }
      sink.WriteLn(case_line);
      bool has_unconditional = false;
      for (const KeyAutomaton::AcceptEntry& anc : chain) {
        if (!anc.flag_name.empty()) {
          sink.WriteLn(
              "        if (this->$0) { out_rule_id = $1; out_match_start = 0; "
              "return true; }",
              anc.flag_name, anc.rule_id);
        } else {
          sink.WriteLn(
              "        out_rule_id = $0; out_match_start = 0; return true;",
              anc.rule_id);
          has_unconditional = true;
          break;
        }
      }
      if (!has_unconditional) {
        sink.WriteLn("        return false;");
      }
    }
    sink.WriteLn("      default: return false;");
    sink.WriteLn("    }");
    sink.WriteLn("  }");
    return;
  }

  // General path with unanchored (and optional anchored) accepting states
  sink.WriteLn("    int32_t state = 1;");
  sink.WriteLn("    int32_t best_priority = 0x7FFFFFFF;");
  sink.WriteLn("    int32_t best_rule_id = -1;");
  sink.WriteLn("    int32_t best_match_start = 0;");
  const std::vector<KeyAutomaton::AcceptEntry>& init_chain =
      dfa.unanchored_accept[1];
  for (size_t i = 0; i < init_chain.size(); ++i) {
    const KeyAutomaton::AcceptEntry& entry = init_chain[i];
    if (!entry.flag_name.empty()) {
      std::string_view prefix = (i == 0) ? "if" : "} else if";
      sink.WriteLn("    $0 (this->$1) {", prefix, entry.flag_name);
      sink.WriteLn("      best_priority = $0;", entry.priority);
      sink.WriteLn("      best_rule_id = $0;", entry.rule_id);
      sink.WriteLn("      best_match_start = word_len;");
      if (i + 1 == init_chain.size()) {
        sink.WriteLn("    }");
      }
    } else {
      if (i == 0) {
        sink.WriteLn("    best_priority = $0;", entry.priority);
        sink.WriteLn("    best_rule_id = $0;", entry.rule_id);
        sink.WriteLn("    best_match_start = word_len;");
      } else {
        sink.WriteLn("    } else {");
        sink.WriteLn("      best_priority = $0;", entry.priority);
        sink.WriteLn("      best_rule_id = $0;", entry.rule_id);
        sink.WriteLn("      best_match_start = word_len;");
        sink.WriteLn("    }");
      }
      break;
    }
  }

  bool has_min_prio_accept = false;
  for (const std::vector<KeyAutomaton::AcceptEntry>& chain :
       dfa.unanchored_accept) {
    for (const KeyAutomaton::AcceptEntry& u : chain) {
      if (u.IsValid() && u.priority == dfa.min_priority) {
        has_min_prio_accept = true;
        break;
      }
    }
    if (has_min_prio_accept) break;
  }

  sink.WriteLn("    int32_t cursor = word_len;");
  sink.WriteLn("    while (cursor > 0) {");
  sink.WriteLn("      --cursor;");
  sink.WriteLn("      const int32_t ec = Classify(word[cursor]);");
  if (use_table) {
    sink.WriteLn("      state = static_cast<int32_t>(kTrans[state * $0 + ec]);",
                 num_classes);
  } else {
    EmitStateTransitionSwitch(dfa, state_count, num_classes, "      ", sink);
  }
  sink.WriteLn("      if (state == 0) break;");
  EmitAcceptSwitch(dfa.unanchored_accept, state_count, "cursor", true, "      ",
                   sink);
  if (has_min_prio_accept) {
    sink.WriteLn("      if (best_priority == $0) break;", dfa.min_priority);
  }
  sink.WriteLn("    }");

  if (has_anchored) {
    sink.WriteLn("    if (cursor == 0 && state != 0) {");
    EmitAcceptSwitch(dfa.anchored_accept, state_count, "0", false, "      ",
                     sink);
    sink.WriteLn("    }");
  }

  sink.WriteLn("    if (best_rule_id < 0) return false;");
  sink.WriteLn("    out_rule_id = best_rule_id;");
  sink.WriteLn("    out_match_start = best_match_start;");
  sink.WriteLn("    return true;");
  sink.WriteLn("  }");
}

// 7. Direct-Coded Yacc Semantic Rule Actions (`ApplyRule`)

std::string OffsetExpr(const SliceOffset& off) {
  if (off.from_end) {
    return off.delta == 0 ? "slice_len"
                          : absl::Substitute("(slice_len - $0)", -off.delta);
  } else {
    return absl::StrCat(off.delta);
  }
}

void EmitFallbackCaseFlags(std::string_view indent, Sink& sink) {
  sink.WriteLn(
      "$0const bool fallback_first_upper = (slice_len > 0) && "
      "IsUpperChar(matched_slice[0]);",
      indent);
  sink.WriteLn("$0bool fallback_all_upper = fallback_first_upper;", indent);
  sink.WriteLn("$0if (fallback_all_upper) {", indent);
  sink.WriteLn("$0  for (int32_t i = 0; i < slice_len; ++i) {", indent);
  sink.WriteLn("$0    const uint16_t ch = matched_slice[i];", indent);
  sink.WriteLn(
      "$0    if (okeytre::ToUpper(ch) != okeytre::ToLower(ch) && "
      "!IsUpperChar(ch)) "
      "{",
      indent);
  sink.WriteLn("$0      fallback_all_upper = false;", indent);
  sink.WriteLn("$0      break;", indent);
  sink.WriteLn("$0    }", indent);
  sink.WriteLn("$0  }", indent);
  sink.WriteLn("$0}", indent);
}

void EmitFallbackLiteralAppend(uint16_t target_char, int literal_idx,
                               std::string_view indent, Sink& sink) {
  uint16_t lower_target = ToLower(target_char);
  uint16_t upper_target = ToUpper(target_char);
  if (lower_target == upper_target) {
    sink.WriteLn("$0out_buf[out_len++] = $1;", indent, Hex16(target_char));
  } else if (literal_idx == 0) {
    sink.WriteLn(
        "$0out_buf[out_len++] = (fallback_all_upper || fallback_first_upper) ? "
        "$1 : $2;",
        indent, Hex16(upper_target), Hex16(lower_target));
  } else {
    sink.WriteLn("$0out_buf[out_len++] = fallback_all_upper ? $1 : $2;", indent,
                 Hex16(upper_target), Hex16(lower_target));
  }
}

void EmitFixedPlanRuleBody(const CompiledRule& rule, const FixedSlicePlan& plan,
                           const std::vector<CharacterMap>& maps,
                           std::string_view indent, Sink& sink) {
  if (rule.IsPassThrough()) {
    sink.WriteLn("$0return -1;", indent);
    return;
  }

  bool aligns_positionally = (rule.rhs_literal_count == rule.atom_slot_count);
  bool uses_input_key_map = false;
  for (const CompiledRHSItem& item : rule.rhs_items) {
    if (item.kind == CompiledRHSItem::Kind::kMapProjection &&
        item.uses_input_key) {
      uses_input_key_map = true;
      break;
    }
  }

  if (uses_input_key_map) {
    sink.WriteLn("$0const uint16_t lower_key = FastToLower(input_key);",
                 indent);
  }
  if (!aligns_positionally && rule.rhs_literal_count > 0) {
    EmitFallbackCaseFlags(indent, sink);
  }

  sink.WriteLn("$0int32_t out_len = 0;", indent);

  int literal_idx = 0;
  for (const CompiledRHSItem& item : rule.rhs_items) {
    switch (item.kind) {
      case CompiledRHSItem::Kind::kLiteral: {
        uint16_t target_char = item.literal_char;
        uint16_t lower_target = ToLower(target_char);
        uint16_t upper_target = ToUpper(target_char);
        if (aligns_positionally) {
          const SliceOffset& off =
              plan.atom_offsets[static_cast<size_t>(literal_idx)];
          if (lower_target == upper_target) {
            sink.WriteLn("$0out_buf[out_len++] = $1;", indent,
                         Hex16(target_char));
          } else {
            sink.WriteLn(
                "$0out_buf[out_len++] = IsUpperChar(matched_slice[$1]) ? $2 : "
                "$3;",
                indent, OffsetExpr(off), Hex16(upper_target),
                Hex16(lower_target));
          }
        } else {
          EmitFallbackLiteralAppend(target_char, literal_idx, indent, sink);
        }
        ++literal_idx;
        break;
      }
      case CompiledRHSItem::Kind::kInputKey:
        sink.WriteLn("$0out_buf[out_len++] = input_key;", indent);
        break;

      case CompiledRHSItem::Kind::kCapture: {
        const auto& [start, end] =
            plan.capture_spans[static_cast<size_t>(item.capture_index - 1)];
        if (start.from_end == end.from_end && (end.delta - start.delta) == 1) {
          sink.WriteLn("$0out_buf[out_len++] = matched_slice[$1];", indent,
                       OffsetExpr(start));
        } else if (start.from_end == end.from_end && end.delta == start.delta) {
          // Statically empty capture
        } else {
          sink.WriteLn("$0for (int32_t idx = $1; idx < $2; ++idx) {", indent,
                       OffsetExpr(start), OffsetExpr(end));
          sink.WriteLn("$0  out_buf[out_len++] = matched_slice[idx];", indent);
          sink.WriteLn("$0}", indent);
        }
        break;
      }
      case CompiledRHSItem::Kind::kMapProjection: {
        const CharacterMap& map = maps[static_cast<size_t>(item.map_id)];
        std::string fn = MapFunctionName(item.map_id, map.name, item.inverted,
                                         item.uses_input_key);
        const auto& [start, end] =
            plan.capture_spans[static_cast<size_t>(item.capture_index - 1)];
        if (start.from_end == end.from_end && (end.delta - start.delta) == 1) {
          std::string elem_expr =
              absl::Substitute("matched_slice[$0]", OffsetExpr(start));
          std::string call_expr =
              item.uses_input_key
                  ? absl::Substitute("$0(lower_key, $1)", fn, elem_expr)
                  : absl::Substitute("$0($1)", fn, elem_expr);
          sink.WriteLn("$0out_buf[out_len++] = $1;", indent, call_expr);
        } else {
          sink.WriteLn("$0for (int32_t idx = $1; idx < $2; ++idx) {", indent,
                       OffsetExpr(start), OffsetExpr(end));
          std::string call_expr =
              item.uses_input_key
                  ? absl::Substitute("$0(lower_key, matched_slice[idx])", fn)
                  : absl::Substitute("$0(matched_slice[idx])", fn);
          sink.WriteLn("$0  out_buf[out_len++] = $1;", indent, call_expr);
          sink.WriteLn("$0}", indent);
        }
        break;
      }
    }
  }

  sink.WriteLn("$0return out_len;", indent);
}

void EmitPikeVMRuleBody(size_t rule_id, const CompiledRule& rule,
                        const std::vector<CharacterMap>& maps,
                        std::string_view indent, Sink& sink) {
  sink.WriteLn("$0std::vector<int> tags;", indent);
  sink.WriteLn(
      "$0if (!MatchPikeVm_$1(matched_slice, slice_len, tags)) return -1;",
      indent, rule_id);

  bool aligns_positionally = (rule.rhs_literal_count == rule.atom_slot_count);
  int atom_tag_base = rule.capture_count * 2;
  bool uses_input_key_map = false;
  for (const CompiledRHSItem& item : rule.rhs_items) {
    if (item.kind == CompiledRHSItem::Kind::kMapProjection &&
        item.uses_input_key) {
      uses_input_key_map = true;
      break;
    }
  }

  if (uses_input_key_map) {
    sink.WriteLn("$0const uint16_t lower_key = FastToLower(input_key);",
                 indent);
  }
  if (!aligns_positionally && rule.rhs_literal_count > 0) {
    EmitFallbackCaseFlags(indent, sink);
  }

  sink.WriteLn("$0int32_t out_len = 0;", indent);

  int literal_idx = 0;
  for (const CompiledRHSItem& item : rule.rhs_items) {
    switch (item.kind) {
      case CompiledRHSItem::Kind::kLiteral: {
        uint16_t target_char = item.literal_char;
        uint16_t lower_target = ToLower(target_char);
        uint16_t upper_target = ToUpper(target_char);
        if (aligns_positionally) {
          int tag_idx = atom_tag_base + literal_idx;
          if (lower_target == upper_target) {
            sink.WriteLn("$0out_buf[out_len++] = $1;", indent,
                         Hex16(target_char));
          } else {
            std::string off_var = absl::StrCat("off_", literal_idx);
            sink.WriteLn("$0const int $1 = tags[$2];", indent, off_var,
                         tag_idx);
            sink.WriteLn(
                "$0out_buf[out_len++] = ($1 >= 0 && "
                "IsUpperChar(matched_slice[$1])) ? $2 : $3;",
                indent, off_var, Hex16(upper_target), Hex16(lower_target));
          }
        } else {
          EmitFallbackLiteralAppend(target_char, literal_idx, indent, sink);
        }
        ++literal_idx;
        break;
      }
      case CompiledRHSItem::Kind::kInputKey:
        sink.WriteLn("$0out_buf[out_len++] = input_key;", indent);
        break;

      case CompiledRHSItem::Kind::kCapture: {
        int s_tag = (item.capture_index - 1) * 2;
        int e_tag = s_tag + 1;
        sink.WriteLn("$0if (tags[$1] >= 0 && tags[$2] > tags[$1]) {", indent,
                     s_tag, e_tag);
        sink.WriteLn("$0  for (int idx = tags[$1]; idx < tags[$2]; ++idx) {",
                     indent, s_tag, e_tag);
        sink.WriteLn("$0    out_buf[out_len++] = matched_slice[idx];", indent);
        sink.WriteLn("$0  }", indent);
        sink.WriteLn("$0}", indent);
        break;
      }
      case CompiledRHSItem::Kind::kMapProjection: {
        const CharacterMap& map = maps[static_cast<size_t>(item.map_id)];
        std::string fn = MapFunctionName(item.map_id, map.name, item.inverted,
                                         item.uses_input_key);
        int s_tag = (item.capture_index - 1) * 2;
        int e_tag = s_tag + 1;
        sink.WriteLn("$0if (tags[$1] >= 0 && tags[$2] > tags[$1]) {", indent,
                     s_tag, e_tag);
        sink.WriteLn("$0  for (int idx = tags[$1]; idx < tags[$2]; ++idx) {",
                     indent, s_tag, e_tag);
        std::string call_expr =
            item.uses_input_key
                ? absl::Substitute("$0(lower_key, matched_slice[idx])", fn)
                : absl::Substitute("$0(matched_slice[idx])", fn);
        sink.WriteLn("$0    out_buf[out_len++] = $1;", indent, call_expr);
        sink.WriteLn("$0  }", indent);
        sink.WriteLn("$0}", indent);
        break;
      }
    }
  }

  sink.WriteLn("$0return out_len;", indent);
}

void EmitApplyRuleFunction(const CompiledAutomaton& compiled, Sink& sink) {
  sink.WriteLn("  static inline int32_t ApplyRule(int32_t rule_id,");
  sink.WriteLn(
      "                                  const uint16_t* matched_slice,");
  sink.WriteLn("                                  int32_t slice_len,");
  sink.WriteLn("                                  uint16_t input_key,");
  sink.WriteLn("                                  uint16_t* out_buf,");
  sink.WriteLn("                                  int32_t out_cap) noexcept {");
  sink.WriteLn("    (void)out_cap;");

  if (compiled.rules.empty()) {
    sink.WriteLn("    (void)rule_id;");
    sink.WriteLn("    (void)matched_slice;");
    sink.WriteLn("    (void)slice_len;");
    sink.WriteLn("    (void)input_key;");
    sink.WriteLn("    (void)out_buf;");
    sink.WriteLn("    return -1;");
    sink.WriteLn("  }");
    return;
  }

  sink.WriteLn("    switch (rule_id) {");
  for (size_t rule_id = 0; rule_id < compiled.rules.size(); ++rule_id) {
    const CompiledRule& rule = compiled.rules[rule_id];
    sink.WriteLn("      case $0: {", rule_id);
    if (rule.has_fixed_plan) {
      EmitFixedPlanRuleBody(rule, rule.fixed_plan, compiled.maps, "        ",
                            sink);
    } else {
      EmitPikeVMRuleBody(rule_id, rule, compiled.maps, "        ", sink);
    }
    sink.WriteLn("      }");
  }
  sink.WriteLn("      default:");
  sink.WriteLn("        return -1;");
  sink.WriteLn("    }");
  sink.WriteLn("  }");
}

}  // namespace

// C++ Automaton Codegen Public Functions

absl::Cord GenerateCppAutomaton(const CompiledAutomaton& compiled,
                                std::string_view type_name) {
  Sink sink;

  std::string_view spec_title =
      compiled.has_name ? std::string_view(compiled.name) : type_name;
  std::string guard_name = HeaderGuardName(type_name);
  sink.WriteLn(
      "// AUTO-GENERATED BY CppAutomatonCodegen \xE2\x80\x94 DO NOT EDIT "
      "MANUALLY.");
  sink.WriteLn("// Specification: $0", spec_title);
  sink.WriteLn();
  sink.WriteLn("#ifndef $0", guard_name);
  sink.WriteLn("#define $0", guard_name);
  sink.WriteLn();
  sink.WriteLn("#include <cstdint>");
  sink.WriteLn("#include <cstring>");
  sink.WriteLn("#include <vector>");
  sink.WriteLn();
  sink.WriteLn("#include \"pike-vm.h\"");
  sink.WriteLn("#include \"unicode-utils.h\"");
  sink.WriteLn();
  sink.WriteLn("struct $0 {", type_name);
  for (const FlagDecl& flag : compiled.flags) {
    sink.WriteLn("  bool $0 = $1;", flag.name,
                 flag.default_value ? "true" : "false");
  }
  if (!compiled.flags.empty()) {
    sink.WriteLn();
  }

  // 1. Entry point `RewriteToBuffer(...)`
  EmitRewriteEntryPoint(compiled, sink);
  sink.WriteLn();

  // 2. Character equivalence classifier `Classify(uint16_t c)`
  EmitClassifier(compiled, sink);
  sink.WriteLn();

  // 3. Fast case helpers
  bool needs_is_upper = false;
  bool needs_fast_to_lower = false;
  CheckNeedsCaseHelpers(compiled, needs_is_upper, needs_fast_to_lower);
  if (needs_is_upper) {
    EmitIsUpperHelper(compiled, sink);
    sink.WriteLn();
  }
  if (needs_fast_to_lower) {
    EmitFastToLowerHelper(sink);
    sink.WriteLn();
  }

  // 4. Case-baked `%map` functions
  EmitMapFunctions(compiled, sink);

  // 5. Fallback `RuleMatcher` helpers
  EmitFallbackMatchers(compiled, sink);

  // 6. Direct-coded Reverse DFAs
  for (size_t idx = 0; idx < compiled.key_automata.size(); ++idx) {
    EmitDFAFunction(idx, compiled.key_automata[idx], sink);
    sink.WriteLn();
  }

  // 7. Direct-coded Yacc semantic rule evaluator `ApplyRule`
  EmitApplyRuleFunction(compiled, sink);

  sink.WriteLn("};");
  sink.WriteLn();
  sink.WriteLn("#endif  // $0", guard_name);

  return std::move(sink).Output();
}

}  // namespace okeytre
