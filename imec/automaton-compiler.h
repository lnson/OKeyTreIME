// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Compiles a `.ime` grammar specification into a table-driven
// `CompiledAutomaton` (Lex/Parse -> Equivalence-Class Partitioning -> Reverse
// Thompson NFA -> Powerset DFA + Forward PikeVM).

#ifndef OKEYTRE_ENGINE_AUTOMATONCOMPILER_HPP_
#define OKEYTRE_ENGINE_AUTOMATONCOMPILER_HPP_

#include <string_view>

#include "compiled-automaton.h"
#include "rule-ast.h"

namespace okeytre {

bool CompileAutomaton(std::string_view source, CompiledAutomaton& out_automaton,
                      RuleCompileError& out_error);

}  // namespace okeytre

#endif  // OKEYTRE_ENGINE_AUTOMATONCOMPILER_HPP_
