// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "inv-compiler.h"

#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "gtest/gtest.h"

namespace okeytre {
namespace {

TEST(InvCompilerTest,
     CompilesMacrosAnchorsNegatedClassesAndMinimizesCaseInsensitiveDfa) {
  constexpr std::string_view kSpec = R"inv(
/* Block comment in Section 1 */
%name "Sample Inversion Rules"

VOWEL ([aeê]|i|o|u)
STOP  (?:ch|[cpt])

%%
// 1. Forbidden letters anywhere in word
[fjwz0-9]
// 2. Start-anchored 'c' before front vowel
^c[eêi]
// 3. End-anchored stop consonant after plain vowel
^[b-df-hj-np-tv-z]+{VOWEL}+{STOP}$   /* trailing comment */
// 4. Escaped literal metacharacters
\^\$
%%
reject "fô"
reject "FÔ"
reject "utf8"
reject "cê"
reject "mêt"
reject "MÊT"
reject "a^$b"
accept "cô"
accept "kê"
accept "tiếng"
)inv";

  absl::StatusOr<CompiledInvDfa> compiled = CompileInvSpec(kSpec);
  ASSERT_TRUE(compiled.ok()) << compiled.status();
  EXPECT_EQ(compiled->name, "Sample Inversion Rules");
  EXPECT_EQ(compiled->pattern_count, 4);
  EXPECT_GT(compiled->state_count, 1);

  const absl::Status verified = VerifyInvAssertions(*compiled, "test.inv");
  EXPECT_TRUE(verified.ok()) << verified;

  const absl::Cord code =
      GenerateCppInvAutomaton(*compiled, "SampleInvAutomaton");
  const std::string header = std::string(code);
  EXPECT_TRUE(absl::StrContains(header, "struct SampleInvAutomaton"));
  EXPECT_TRUE(absl::StrContains(header, "static constexpr uint8_t kTrans[]"));
  EXPECT_TRUE(absl::StrContains(header, "kStateFlags"));
}

TEST(InvCompilerTest, ReportsLexYaccAndSemanticDiagnostics) {
  constexpr std::string_view kUndefinedMacroSpec = R"inv(
%%
^{MISSING}$
)inv";
  EXPECT_FALSE(CompileInvSpec(kUndefinedMacroSpec).ok());

  constexpr std::string_view kCyclicMacroSpec = R"inv(
A {B}
B {A}
%%
^{A}$
)inv";
  EXPECT_FALSE(CompileInvSpec(kCyclicMacroSpec).ok());

  constexpr std::string_view kNestedCaretSpec = R"inv(
%%
(?:^|a)bc$
)inv";
  EXPECT_FALSE(CompileInvSpec(kNestedCaretSpec).ok());

  constexpr std::string_view kUnclosedParenSpec = R"inv(
%%
^(abc$
)inv";
  EXPECT_FALSE(CompileInvSpec(kUnclosedParenSpec).ok());

  constexpr std::string_view kUnterminatedCommentSpec = R"inv(
/* unclosed
%%
^abc$
)inv";
  EXPECT_FALSE(CompileInvSpec(kUnterminatedCommentSpec).ok());

  constexpr std::string_view kFailingAssertionSpec = R"inv(
%%
^abc$
%%
accept "abc"
)inv";
  absl::StatusOr<CompiledInvDfa> compiled =
      CompileInvSpec(kFailingAssertionSpec);
  ASSERT_TRUE(compiled.ok());
  EXPECT_FALSE(VerifyInvAssertions(*compiled, "bad.inv").ok());
}

}  // namespace
}  // namespace okeytre
