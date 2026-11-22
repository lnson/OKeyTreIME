// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Ahead-of-time C++ code generator (`imec`) that compiles a `.ime` Lex/Yacc
// specification into a standalone, zero-allocation C++ automaton struct
// (`switch`-based equivalence classifier, case-baked `%map` jump tables,
// direct-coded Reverse DFAs, and static slice offsets).

#ifndef OKEYTRE_ENGINE_CPPAUTOMATONCODEGEN_HPP_
#define OKEYTRE_ENGINE_CPPAUTOMATONCODEGEN_HPP_

#include <string_view>

#include "absl/strings/cord.h"
#include "compiled-automaton.h"

namespace okeytre {

// Generates a standalone, header-only C++ automaton struct named `type_name`
// from `compiled` (including equivalence-class `Classify`, case-preserving
// `%map` helpers, direct-coded Reverse DFAs, and `ApplyRule`).
absl::Cord GenerateCppAutomaton(const CompiledAutomaton& compiled,
                                std::string_view type_name);

}  // namespace okeytre

#endif  // OKEYTRE_ENGINE_CPPAUTOMATONCODEGEN_HPP_
