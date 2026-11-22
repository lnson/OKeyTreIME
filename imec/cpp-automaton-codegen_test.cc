// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "cpp-automaton-codegen.h"

#include <string>
#include <string_view>

#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "automaton-compiler.h"
#include "gtest/gtest.h"

namespace okeytre {
namespace {

TEST(CppAutomatonCodegenTest, GeneratesDeterministicHeaderCode) {
  constexpr std::string_view kSource = R"(
%name "MiniTelex"
%flag modern_tone = false
%map TONE(s, f) {
  [aáà] : á à
}
%%
d + d --> đ
(a) + [sf] --> {TONE($1, $0)}
)";

  CompiledAutomaton compiled;
  RuleCompileError err;
  ASSERT_TRUE(CompileAutomaton(kSource, compiled, err)) << err.Description();

  std::string cpp_first(GenerateCppAutomaton(compiled, "MiniTelexAutomaton"));

  EXPECT_TRUE(absl::StrContains(cpp_first, "#include \"pike-vm.h\""));
  EXPECT_TRUE(absl::StrContains(cpp_first, "struct MiniTelexAutomaton {"));
  EXPECT_TRUE(absl::StrContains(cpp_first, "bool modern_tone = false;"));
  EXPECT_TRUE(absl::StrContains(cpp_first, "RewriteToBuffer("));
  EXPECT_TRUE(absl::StrContains(cpp_first, "Classify(uint16_t c)"));
  EXPECT_TRUE(absl::StrContains(cpp_first, "ApplyRule("));

  // Verify deterministic output across multiple runs.
  absl::Cord cpp_second = GenerateCppAutomaton(compiled, "MiniTelexAutomaton");
  EXPECT_EQ(cpp_first, cpp_second);
}

TEST(CppAutomatonCodegenTest, EmitsFallbackAcceptChainForGuardedRule) {
  constexpr std::string_view kSource = R"(
%flag modernTone = false
%map GRAVE { o:ò, a:à }
%%
?modernTone ^(h)(o)(a) + f --> $1$2{GRAVE($3)}
^(h)([oa])([oa])       + f --> $1{GRAVE($2)}$3
)";

  CompiledAutomaton compiled;
  RuleCompileError err;
  ASSERT_TRUE(CompileAutomaton(kSource, compiled, err)) << err.Description();

  std::string code(GenerateCppAutomaton(compiled, "FallbackAutomaton"));
  EXPECT_TRUE(
      absl::StrContains(code, "if (this->modernTone) { out_rule_id = 0;"));
  EXPECT_TRUE(absl::StrContains(
      code, "out_rule_id = 1; out_match_start = 0; return true;"));
}

}  // namespace
}  // namespace okeytre
