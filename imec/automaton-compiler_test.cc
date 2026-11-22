// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "automaton-compiler.h"

#include <string>
#include <string_view>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace okeytre {
namespace {

TEST(AutomatonCompilerTest, CompilesAndRewritesRules) {
  constexpr std::string_view kSource = R"(
%name "MiniTelex"
%flag enable_dd = true
%map TONE(s, f) {
  [aáà] : á à
}
%%
?enable_dd d + d --> đ
(a) + [sf] --> {TONE($1, $0)}
%%
)";

  CompiledAutomaton automaton;
  RuleCompileError err;
  ASSERT_TRUE(CompileAutomaton(kSource, automaton, err)) << err.Description();

  // "d" + 'd' -> "đ"
  std::u16string out;
  ASSERT_TRUE(automaton.Rewrite(u"d", 'd', out));
  EXPECT_EQ(out, u"đ");

  // "D" + 'd' -> "Đ" (case preserved)
  ASSERT_TRUE(automaton.Rewrite(u"D", 'd', out));
  EXPECT_EQ(out, u"Đ");

  // "ba" + 's' -> "bá"
  ASSERT_TRUE(automaton.Rewrite(u"ba", 's', out));
  EXPECT_EQ(out, u"bá");
}

TEST(AutomatonCompilerTest, FlaggedRuleFallsBackToLowerPriorityRuleWhenOff) {
  constexpr std::string_view kSource = R"(
%flag modernTone = false
%map GRAVE { o:ò, a:à }
%%
?modernTone ^(h)(o)(a) + f --> $1$2{GRAVE($3)}
^(h)([oa])([oa])       + f --> $1{GRAVE($2)}$3
)";

  CompiledAutomaton automaton;
  RuleCompileError err;
  ASSERT_TRUE(CompileAutomaton(kSource, automaton, err)) << err.Description();

  std::u16string out;
  ASSERT_TRUE(automaton.Rewrite(u"hoa", 'f', out));
  EXPECT_EQ(out, u"hòa");

  CompiledAutomaton modern = automaton.WithEnabledFlags({"modernTone"});
  ASSERT_TRUE(modern.Rewrite(u"hoa", 'f', out));
  EXPECT_EQ(out, u"hoà");
}

TEST(AutomatonCompilerTest, RejectsUndefinedMacroReference) {
  constexpr std::string_view kBadSource = R"(
%%
{UNDEFINED_MACRO} + a --> b
)";

  CompiledAutomaton automaton;
  RuleCompileError err;
  EXPECT_FALSE(CompileAutomaton(kBadSource, automaton, err));
  EXPECT_FALSE(err.message.empty());
}

TEST(AutomatonCompilerTest, RejectsTooManyEquivalenceClasses) {
  std::string source = "%%\n";
  // Emit 128 distinct caseless BMP characters (U+4E00..U+4E7F), plus default
  // class 0 -> 129 equivalence classes (> 128).
  for (int i = 0; i < kMaxEquivalenceClasses; ++i) {
    char32_t cp = static_cast<char32_t>(0x4E00 + i);
    char utf8[3] = {
        static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)),
        static_cast<char>(0x80 | ((cp >> 6) & 0x3F)),
        static_cast<char>(0x80 | (cp & 0x3F)),
    };
    absl::StrAppend(&source, std::string_view(utf8, 3), " + a --> b\n");
  }

  CompiledAutomaton automaton;
  RuleCompileError err;
  EXPECT_FALSE(CompileAutomaton(source, automaton, err));
  EXPECT_TRUE(absl::StrContains(err.message, "equivalence classes"));
}

}  // namespace
}  // namespace okeytre
