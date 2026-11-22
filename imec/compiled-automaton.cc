// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "compiled-automaton.h"

#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace okeytre {

bool KeyAutomaton::FindBestMatch(
    std::u16string_view word, const std::vector<uint16_t>& ec_table,
    const absl::flat_hash_set<std::string>& enabled_flags, int& out_rule_id,
    int& out_match_start) const {
  auto is_enabled = [&enabled_flags](const AcceptEntry& entry) -> bool {
    if (entry.flag_name.empty()) return true;
    return enabled_flags.contains(entry.flag_name);
  };

  const int word_len = static_cast<int>(word.size());
  int state = 1;
  int32_t best_priority = std::numeric_limits<int32_t>::max();
  int32_t best_rule_id = -1;
  int best_match_start = 0;

  auto evaluate_chain = [&](const std::vector<AcceptEntry>& chain,
                            int match_start) {
    for (const AcceptEntry& entry : chain) {
      if (entry.priority >= best_priority) {
        break;
      }
      if (is_enabled(entry)) {
        best_priority = entry.priority;
        best_rule_id = entry.rule_id;
        best_match_start = match_start;
        break;
      }
    }
  };

  if (static_cast<size_t>(state) < unanchored_accept.size()) {
    evaluate_chain(unanchored_accept[static_cast<size_t>(state)], word_len);
  }

  int cursor = word_len;
  while (cursor > 0) {
    --cursor;
    uint16_t ch = static_cast<uint16_t>(word[static_cast<size_t>(cursor)]);
    int ec = static_cast<int>(ec_table[ch]);
    state = static_cast<int>(
        transitions[static_cast<size_t>(state * num_classes + ec)]);
    if (state == 0) {
      break;
    }

    evaluate_chain(unanchored_accept[static_cast<size_t>(state)], cursor);
    if (best_priority == min_priority) {
      break;
    }
  }

  if (cursor == 0 && state != 0 &&
      static_cast<size_t>(state) < anchored_accept.size()) {
    evaluate_chain(anchored_accept[static_cast<size_t>(state)], 0);
  }

  if (best_rule_id < 0) return false;
  out_rule_id = static_cast<int>(best_rule_id);
  out_match_start = best_match_start;
  return true;
}

bool CompiledAutomaton::Rewrite(std::u16string_view word, uint16_t input,
                                std::u16string& out_result) const {
  if (trigger_table.size() <= input) return false;
  int auto_idx = static_cast<int>(trigger_table[input]);
  if (auto_idx < 0 || static_cast<size_t>(auto_idx) >= key_automata.size()) {
    return false;
  }

  const KeyAutomaton& key_auto = key_automata[static_cast<size_t>(auto_idx)];
  int rule_id = -1;
  int match_start = 0;
  if (!key_auto.FindBestMatch(word, ec_table, enabled_flags, rule_id,
                              match_start)) {
    return false;
  }

  const CompiledRule& rule = rules[static_cast<size_t>(rule_id)];
  if (rule.IsPassThrough()) {
    return false;
  }

  std::u16string_view matched_slice =
      word.substr(static_cast<size_t>(match_start));
  const int slice_len = static_cast<int>(matched_slice.size());

  std::vector<int> tags;
  if (rule.has_fixed_plan) {
    tags.assign(
        static_cast<size_t>(rule.capture_count * 2 + rule.atom_slot_count), -1);
    for (size_t i = 0; i < rule.fixed_plan.capture_spans.size(); ++i) {
      const auto& [start, end] = rule.fixed_plan.capture_spans[i];
      tags[i * 2] = start.Resolve(slice_len);
      tags[i * 2 + 1] = end.Resolve(slice_len);
    }
    size_t atom_base = static_cast<size_t>(rule.capture_count * 2);
    for (size_t i = 0; i < rule.fixed_plan.atom_offsets.size(); ++i) {
      tags[atom_base + i] = rule.fixed_plan.atom_offsets[i].Resolve(slice_len);
    }
  } else {
    const bool matched = rule.matcher.MatchSliceWithClassifier(
        reinterpret_cast<const uint16_t*>(matched_slice.data()), slice_len,
        [&ec_table = this->ec_table](uint16_t ch) -> int {
          return static_cast<int>(ec_table[ch]);
        },
        tags);
    if (!matched) {
      return false;
    }
  }

  std::u16string replacement;
  EvaluateRHS(rule, matched_slice, tags, input, replacement);

  out_result.clear();
  out_result.reserve(static_cast<size_t>(match_start) + replacement.size());
  if (match_start > 0) {
    out_result.append(word.substr(0, static_cast<size_t>(match_start)));
  }
  out_result.append(replacement);
  return true;
}

void CompiledAutomaton::EvaluateRHS(const CompiledRule& rule,
                                    std::u16string_view matched_slice,
                                    const std::vector<int>& tags,
                                    uint16_t input_key,
                                    std::u16string& out_replacement) const {
  const int slice_len = static_cast<int>(matched_slice.size());
  out_replacement.clear();
  out_replacement.reserve(static_cast<size_t>(slice_len + 2));

  bool aligns_positionally = (rule.rhs_literal_count == rule.atom_slot_count);
  int atom_tag_base = rule.capture_count * 2;

  bool fallback_all_upper = false;
  bool fallback_first_upper = false;
  if (!aligns_positionally && slice_len > 0) {
    fallback_first_upper = IsUpper(static_cast<uint16_t>(matched_slice[0]));
    bool has_upper = false;
    bool has_lower = false;
    for (char16_t raw_ch : matched_slice) {
      uint16_t ch = static_cast<uint16_t>(raw_ch);
      if (IsUpper(ch)) {
        has_upper = true;
      } else if (ToUpper(ch) != ToLower(ch)) {
        has_lower = true;
      }
    }
    fallback_all_upper = has_upper && !has_lower;
  }

  int literal_index = 0;
  std::optional<uint16_t> lower_input_cached;

  for (const CompiledRHSItem& item : rule.rhs_items) {
    switch (item.kind) {
      case CompiledRHSItem::Kind::kLiteral: {
        uint16_t target_char = item.literal_char;
        if (aligns_positionally) {
          int offset = tags[static_cast<size_t>(atom_tag_base + literal_index)];
          if (offset >= 0 && offset < slice_len) {
            uint16_t src_char = static_cast<uint16_t>(
                matched_slice[static_cast<size_t>(offset)]);
            out_replacement.push_back(
                static_cast<char16_t>(MatchCase(src_char, target_char)));
          } else {
            out_replacement.push_back(static_cast<char16_t>(target_char));
          }
        } else if (fallback_all_upper) {
          out_replacement.push_back(
              static_cast<char16_t>(ToUpper(target_char)));
        } else if (literal_index == 0 && fallback_first_upper) {
          out_replacement.push_back(
              static_cast<char16_t>(ToUpper(target_char)));
        } else {
          out_replacement.push_back(static_cast<char16_t>(target_char));
        }
        ++literal_index;
        break;
      }
      case CompiledRHSItem::Kind::kInputKey:
        out_replacement.push_back(static_cast<char16_t>(input_key));
        break;

      case CompiledRHSItem::Kind::kCapture: {
        int start = tags[static_cast<size_t>((item.capture_index - 1) * 2)];
        int end = tags[static_cast<size_t>((item.capture_index - 1) * 2 + 1)];
        if (start >= 0 && end >= start && end <= slice_len) {
          out_replacement.append(matched_slice.substr(
              static_cast<size_t>(start), static_cast<size_t>(end - start)));
        }
        break;
      }
      case CompiledRHSItem::Kind::kMapProjection: {
        int start = tags[static_cast<size_t>((item.capture_index - 1) * 2)];
        int end = tags[static_cast<size_t>((item.capture_index - 1) * 2 + 1)];
        if (start < 0 || end < start || end > slice_len) {
          break;
        }
        const CharacterMap& map = maps[static_cast<size_t>(item.map_id)];
        std::optional<uint16_t> lower_key;
        if (item.uses_input_key) {
          if (!lower_input_cached.has_value()) {
            lower_input_cached = ToLower(input_key);
          }
          lower_key = lower_input_cached;
        }
        for (int idx = start; idx < end; ++idx) {
          uint16_t src_char =
              static_cast<uint16_t>(matched_slice[static_cast<size_t>(idx)]);
          uint16_t lower_src = ToLower(src_char);
          std::optional<uint16_t> mapped_lower =
              map.Apply(lower_src, item.inverted, lower_key);
          if (mapped_lower.has_value()) {
            out_replacement.push_back(
                static_cast<char16_t>(MatchCase(src_char, *mapped_lower)));
          } else {
            out_replacement.push_back(static_cast<char16_t>(src_char));
          }
        }
        break;
      }
    }
  }
}

}  // namespace okeytre
