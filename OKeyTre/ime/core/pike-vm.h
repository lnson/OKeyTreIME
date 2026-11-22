// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Lightweight header-only PikeVM submatch extractor primitives shared by
// ahead-of-time generated automata (`*.generated.h`) and `imec`.

#ifndef OKEYTRE_ENGINE_PIKEVM_HPP_
#define OKEYTRE_ENGINE_PIKEVM_HPP_

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace okeytre {

inline constexpr int kMaxEquivalenceClasses = 128;

// Fixed-capacity bitset over equivalence class IDs (`0..<num_classes`).
struct ECBitSet {
  std::bitset<kMaxEquivalenceClasses> bits;

  constexpr ECBitSet() = default;
  constexpr explicit ECBitSet(std::bitset<kMaxEquivalenceClasses> b)
      : bits(b) {}
  constexpr explicit ECBitSet(uint64_t lo) : bits(lo) {}
  constexpr ECBitSet(uint64_t lo, uint64_t hi)
      : bits((std::bitset<kMaxEquivalenceClasses>(hi) << 64) |
             std::bitset<kMaxEquivalenceClasses>(lo)) {}

  constexpr bool operator==(const ECBitSet& other) const = default;

  constexpr void Insert(int ec) noexcept {
    if (ec < 0 || ec >= kMaxEquivalenceClasses) return;
    bits.set(static_cast<size_t>(ec));
  }

  constexpr bool Contains(int ec) const noexcept {
    if (ec < 0 || ec >= kMaxEquivalenceClasses) return false;
    return bits.test(static_cast<size_t>(ec));
  }

  constexpr uint64_t LowWord() const noexcept {
    constexpr std::bitset<kMaxEquivalenceClasses> kLowMask(~uint64_t{0});
    return (bits & kLowMask).to_ullong();
  }

  constexpr uint64_t HighWord() const noexcept {
    return (bits >> 64).to_ullong();
  }
};

static_assert(std::is_trivially_copyable_v<ECBitSet>);

// Bytecode instruction for the deterministic ordered Thompson/PikeVM submatch
// extractor.
struct PikeVMInst {
  enum class Op : uint8_t {
    kMatch,
    kCharClass,
    kJump,
    kSplit,
    kSaveTag,
  };

  ECBitSet char_class;
  int16_t arg1 = 0;
  int16_t arg2 = 0;
  Op op = Op::kMatch;

  static constexpr PikeVMInst MakeMatch() noexcept {
    return PikeVMInst{{}, 0, 0, Op::kMatch};
  }
  static constexpr PikeVMInst MakeCharClass(ECBitSet bits) noexcept {
    return PikeVMInst{bits, 0, 0, Op::kCharClass};
  }
  static constexpr PikeVMInst MakeJump(int target) noexcept {
    return PikeVMInst{{}, static_cast<int16_t>(target), 0, Op::kJump};
  }
  static constexpr PikeVMInst MakeSplit(int first, int second) noexcept {
    return PikeVMInst{{},
                      static_cast<int16_t>(first),
                      static_cast<int16_t>(second),
                      Op::kSplit};
  }
  static constexpr PikeVMInst MakeSaveTag(int tag_idx) noexcept {
    return PikeVMInst{{}, static_cast<int16_t>(tag_idx), 0, Op::kSaveTag};
  }
};

static_assert(sizeof(PikeVMInst) <= 24);

// Forward ordered Tagged-NFA (PikeVM) executed only on the winning rule's
// matched word slice when a rule has multiple variable-length segments.
struct RuleMatcher {
  std::vector<PikeVMInst> instructions;
  int num_tags = 0;

  template <typename ClassifyFn>
  bool MatchSliceWithClassifier(const uint16_t* slice, int slice_len,
                                ClassifyFn classify,
                                std::vector<int>& out_tags) const {
    if (instructions.empty()) return false;
    const size_t inst_count = instructions.size();
    const size_t tag_stride = num_tags > 0 ? static_cast<size_t>(num_tags) : 0;

    std::vector<uint8_t> visited(inst_count, 0);
    std::vector<int> current_pcs;
    std::vector<int> next_pcs;
    current_pcs.reserve(inst_count);
    next_pcs.reserve(inst_count);

    std::vector<int> current_tags;
    std::vector<int> next_tags;
    current_tags.reserve(inst_count * tag_stride);
    next_tags.reserve(inst_count * tag_stride);

    std::vector<int> initial_tags(tag_stride, -1);
    int* init_tag_ptr = tag_stride > 0 ? initial_tags.data() : nullptr;
    AddThread(0, init_tag_ptr, 0, current_pcs, current_tags, visited);

    for (int step = 0; step < slice_len; ++step) {
      if (current_pcs.empty()) return false;
      const uint16_t ch = slice[step];
      const int ec = classify(ch);

      next_pcs.clear();
      next_tags.clear();
      std::fill(visited.begin(), visited.end(), uint8_t{0});

      for (size_t i = 0; i < current_pcs.size(); ++i) {
        const int pc = current_pcs[i];
        const PikeVMInst& inst = instructions[static_cast<size_t>(pc)];
        if (inst.op == PikeVMInst::Op::kCharClass &&
            inst.char_class.Contains(ec)) {
          int* thread_tags =
              tag_stride > 0 ? &current_tags[i * tag_stride] : nullptr;
          AddThread(pc + 1, thread_tags, step + 1, next_pcs, next_tags,
                    visited);
        }
      }
      current_pcs.swap(next_pcs);
      current_tags.swap(next_tags);
    }

    for (size_t i = 0; i < current_pcs.size(); ++i) {
      const int pc = current_pcs[i];
      if (instructions[static_cast<size_t>(pc)].op == PikeVMInst::Op::kMatch) {
        if (tag_stride > 0) {
          const int* winning_tags = &current_tags[i * tag_stride];
          out_tags.assign(winning_tags, winning_tags + tag_stride);
        } else {
          out_tags.clear();
        }
        return true;
      }
    }
    return false;
  }

 private:
  void AddThread(int pc, int* working_tags, int offset, std::vector<int>& pcs,
                 std::vector<int>& flat_tags,
                 std::vector<uint8_t>& visited) const {
    if (pc < 0 || static_cast<size_t>(pc) >= instructions.size()) return;
    if (visited[static_cast<size_t>(pc)] != 0) return;
    visited[static_cast<size_t>(pc)] = 1;

    const PikeVMInst& inst = instructions[static_cast<size_t>(pc)];
    switch (inst.op) {
      case PikeVMInst::Op::kJump:
        AddThread(inst.arg1, working_tags, offset, pcs, flat_tags, visited);
        break;
      case PikeVMInst::Op::kSplit:
        AddThread(inst.arg1, working_tags, offset, pcs, flat_tags, visited);
        AddThread(inst.arg2, working_tags, offset, pcs, flat_tags, visited);
        break;
      case PikeVMInst::Op::kSaveTag: {
        const bool valid_tag =
            working_tags != nullptr && inst.arg1 >= 0 && inst.arg1 < num_tags;
        int prev_val = -1;
        if (valid_tag) {
          prev_val = working_tags[inst.arg1];
          working_tags[inst.arg1] = offset;
        }
        AddThread(pc + 1, working_tags, offset, pcs, flat_tags, visited);
        if (valid_tag) {
          working_tags[inst.arg1] = prev_val;
        }
        break;
      }
      case PikeVMInst::Op::kCharClass:
      case PikeVMInst::Op::kMatch:
        pcs.push_back(pc);
        if (working_tags != nullptr && num_tags > 0) {
          flat_tags.insert(flat_tags.end(), working_tags,
                           working_tags + num_tags);
        }
        break;
    }
  }
};

}  // namespace okeytre

#endif  // OKEYTRE_ENGINE_PIKEVM_HPP_
