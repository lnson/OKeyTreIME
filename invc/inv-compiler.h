// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Ahead-of-time inversion matcher compiler turning a declarative `.inv`
// specification of disallowed UTF-16 patterns into a minimized, zero-allocation
// C++ DFA for word-reversion checks.

#ifndef OKEYTRE_INVC_INV_COMPILER_H_
#define OKEYTRE_INVC_INV_COMPILER_H_

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"

namespace okeytre {

// Inline compile-time verification assertion from Section 3 of a `.inv` file
// (`reject "word"` or `accept "word"`).
struct InvTestAssertion {
  std::string word_utf8;
  int32_t line = 1;
  bool expect_match = false;
};

// Minimized deterministic finite automaton recognizing the union of all
// disallowed patterns in a `.inv` specification (case-insensitively over BMP
// UTF-16 code units).
//
// State `0` is the dead trap state; state `1` is the start state.
struct CompiledInvDfa {
  std::string name;
  // Flattened transition table of size `state_count * num_classes`.
  std::vector<int32_t> transitions;
  // Per-state accepting flag (`accepting[s] == true` when the input matches at
  // least one disallowed pattern in the `.inv` specification).
  std::vector<bool> accepting;
  // Per-state flag indicating an absorbing accepting state (every outgoing
  // transition stays in `s`, allowing early return during forward scans).
  std::vector<bool> absorbing_accept;
  std::vector<InvTestAssertion> assertions;
  // Maps each BMP UTF-16 code unit (`0..65535`) to its equivalence class ID
  // (`0..<num_classes`), with uppercase/lowercase pairs folded together.
  std::array<uint16_t, 65536> ec_table{};
  int32_t state_count = 0;
  int32_t num_classes = 1;
  int32_t pattern_count = 0;

  // Evaluates the compiled inversion DFA against a UTF-16 word slice.
  bool Matches(std::u16string_view word) const noexcept;
};

// Converts a UTF-8 string to UTF-16 via simdutf.
std::u16string InvUtf8ToU16(std::string_view utf8);

// Parses, determinizes, and minimizes a `.inv` specification into a single
// `CompiledInvDfa`.
absl::StatusOr<CompiledInvDfa> CompileInvSpec(std::string_view source);

// Runs all inline `reject` / `accept` assertions declared in Section 3.
absl::Status VerifyInvAssertions(const CompiledInvDfa& compiled,
                                 std::string_view input_path);

// Emits a standalone, zero-dependency C++ header defining `struct_name` with
// `static inline bool Matches(const uint16_t* word, int32_t word_len)`.
absl::Cord GenerateCppInvAutomaton(const CompiledInvDfa& compiled,
                                   std::string_view struct_name);

}  // namespace okeytre

#endif  // OKEYTRE_INVC_INV_COMPILER_H_
