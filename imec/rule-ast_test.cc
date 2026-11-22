// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "rule-ast.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "gtest/gtest.h"

namespace okeytre {
namespace {

TEST(RuleAstTest, RuleCompileErrorAndCharPredicateSemantics) {
  EXPECT_EQ(MatchCase('A', 0x0111), 0x0110);
  EXPECT_EQ(MatchCase('a', 0x0110), 0x0111);
  EXPECT_EQ(MatchCase(0x01AF, 'a'), 'A');
  EXPECT_EQ(Utf8ToU16("Tiếng Việt"), u"Tiếng Việt");

  const RuleCompileError err{42, "Unexpected token."};
  EXPECT_EQ(err.Description(), "Line 42: Unexpected token.");

  const CharPredicate any = CharPredicate::AnyWordChar();
  EXPECT_EQ(any.kind, CharPredicate::Kind::kAnyWordChar);
  EXPECT_TRUE(any.Contains('a'));
  EXPECT_TRUE(any.Contains(0x0111));  // 'đ'
  EXPECT_TRUE(any.Contains(' '));

  absl::flat_hash_set<uint16_t> universe;
  any.ReferencedCharacters(universe);
  EXPECT_TRUE(universe.empty());

  const CharPredicate pos = CharPredicate::Positive({'a', 0x00E2});
  EXPECT_EQ(pos.kind, CharPredicate::Kind::kPositive);
  EXPECT_TRUE(pos.Contains('a'));
  EXPECT_TRUE(pos.Contains(0x00E2));
  EXPECT_FALSE(pos.Contains('b'));
  pos.ReferencedCharacters(universe);
  EXPECT_EQ(universe, (absl::flat_hash_set<uint16_t>{'a', 0x00E2}));

  const CharPredicate neg = CharPredicate::Negative({'x', 'y'});
  EXPECT_EQ(neg.kind, CharPredicate::Kind::kNegative);
  EXPECT_FALSE(neg.Contains('x'));
  EXPECT_FALSE(neg.Contains('y'));
  EXPECT_TRUE(neg.Contains('a'));
  EXPECT_TRUE(neg.Contains(0x1EA5));  // 'ấ'
  neg.ReferencedCharacters(universe);
  EXPECT_EQ(universe, (absl::flat_hash_set<uint16_t>{'a', 0x00E2, 'x', 'y'}));

  EXPECT_EQ(pos, CharPredicate::Positive({'a', 0x00E2}));
  EXPECT_NE(pos, CharPredicate::Negative({'a', 0x00E2}));
  EXPECT_NE(pos, CharPredicate::Positive({'a'}));
  EXPECT_NE(any, CharPredicate::Positive({}));
}

TEST(RuleAstTest, CharacterMap1DMultiSourceCaseFoldingAndSelfMapping) {
  CharacterMap map_1d;
  RuleCompileError err;

  // Empty map or empty source rows must fail.
  EXPECT_FALSE(CharacterMap::Make1D("EMPTY", {}, 7, map_1d, err));
  EXPECT_EQ(err.line, 7);
  EXPECT_TRUE(absl::StrContains(err.message, "at least one mapping"));

  EXPECT_FALSE(CharacterMap::Make1D("EMPTY_ROWS", {{{}, 'a'}}, 9, map_1d, err));
  EXPECT_EQ(err.line, 9);

  // Uppercase inputs/targets are lowercased; canonical base is `sources[0]`;
  // self-mapping ('i' -> 'i') is excluded from `inverse_all`.
  ASSERT_TRUE(CharacterMap::Make1D(
      "CIRC",
      {
          {{'A', 0x00C1, 0x00C0}, 0x00C2},  // [AÁÀ] : Â -> canonical 'a'
          {{'I'}, 'I'},                     // self-mapping
          {{}, 'z'},                        // empty row ignored when non-empty
      },
      12, map_1d, err))
      << err.Description();

  EXPECT_FALSE(map_1d.is_2d);
  EXPECT_EQ(map_1d.name, "CIRC");
  EXPECT_EQ(map_1d.domain_set,
            (absl::flat_hash_set<uint16_t>{'a', 0x00E1, 0x00E0, 'i'}));
  EXPECT_EQ(map_1d.inverse_domain_set, (absl::flat_hash_set<uint16_t>{0x00E2}));

  EXPECT_EQ(map_1d.Apply('a', /*inverted=*/false, std::nullopt), 0x00E2);
  EXPECT_EQ(map_1d.Apply(0x00E1, /*inverted=*/false, std::nullopt), 0x00E2);
  EXPECT_EQ(map_1d.Apply(0x00E0, /*inverted=*/false, std::nullopt), 0x00E2);
  EXPECT_EQ(map_1d.Apply('i', /*inverted=*/false, std::nullopt), 'i');
  EXPECT_EQ(map_1d.Apply('u', /*inverted=*/false, std::nullopt), std::nullopt);

  // Inverse maps 'â' back to canonical 'a', while self-mapped 'i' has no
  // inverse entry.
  EXPECT_EQ(map_1d.Apply(0x00E2, /*inverted=*/true, std::nullopt), 'a');
  EXPECT_EQ(map_1d.Apply('i', /*inverted=*/true, std::nullopt), std::nullopt);
  EXPECT_EQ(map_1d.Apply('a', /*inverted=*/true, std::nullopt), std::nullopt);
}

TEST(RuleAstTest, CharacterMap2DKeyBoundProjectionsAndValidation) {
  CharacterMap map_2d;
  RuleCompileError err;

  // Missing keys, missing rows, and column count mismatches must fail.
  EXPECT_FALSE(
      CharacterMap::Make2D("TONE", {}, {{{'a'}, {'b'}}}, 5, map_2d, err));
  EXPECT_EQ(err.line, 5);
  EXPECT_TRUE(absl::StrContains(err.message, "at least one key"));

  EXPECT_FALSE(CharacterMap::Make2D("TONE", {'s', 'f'}, {}, 6, map_2d, err));
  EXPECT_EQ(err.line, 6);
  EXPECT_TRUE(absl::StrContains(err.message, "at least one mapping"));

  EXPECT_FALSE(CharacterMap::Make2D("TONE", {'s', 'f'}, {{{'a'}, {0x00E1}}}, 8,
                                    map_2d, err));
  EXPECT_EQ(err.line, 8);
  EXPECT_TRUE(absl::StrContains(err.message, "1 outputs, expected 2"));

  // Valid 2D map with uppercase keys/chars and a tone-stripping column ('Z').
  ASSERT_TRUE(
      CharacterMap::Make2D("TONE", {'S', 'F', 'Z'},
                           {
                               {{'A', 0x00C1, 0x00C0}, {0x00C1, 0x00C0, 'A'}},
                           },
                           10, map_2d, err))
      << err.Description();

  EXPECT_TRUE(map_2d.is_2d);
  EXPECT_EQ(map_2d.keys, (std::vector<uint16_t>{'s', 'f', 'z'}));
  EXPECT_EQ(map_2d.domain_set,
            (absl::flat_hash_set<uint16_t>{'a', 0x00E1, 0x00E0}));
  EXPECT_EQ(map_2d.inverse_domain_set,
            (absl::flat_hash_set<uint16_t>{0x00E1, 0x00E0}));

  // Forward 2D requires a valid trigger key.
  EXPECT_EQ(map_2d.Apply('a', /*inverted=*/false, 's'), 0x00E1);
  EXPECT_EQ(map_2d.Apply(0x00E0, /*inverted=*/false, 's'), 0x00E1);
  EXPECT_EQ(map_2d.Apply('a', /*inverted=*/false, 'f'), 0x00E0);
  EXPECT_EQ(map_2d.Apply(0x00E1, /*inverted=*/false, 'z'), 'a');
  EXPECT_EQ(map_2d.Apply('a', /*inverted=*/false, 'x'), std::nullopt);
  EXPECT_EQ(map_2d.Apply('e', /*inverted=*/false, 's'), std::nullopt);
  EXPECT_EQ(map_2d.Apply('a', /*inverted=*/false, std::nullopt), std::nullopt);

  // Per-key inverse tables partition toned outputs by trigger key.
  auto s_inv_it = map_2d.inverse_by_key.find('s');
  ASSERT_NE(s_inv_it, map_2d.inverse_by_key.end());
  EXPECT_TRUE(s_inv_it->second.contains(0x00E1));
  EXPECT_FALSE(s_inv_it->second.contains(0x00E0));

  auto z_inv_it = map_2d.inverse_by_key.find('z');
  ASSERT_NE(z_inv_it, map_2d.inverse_by_key.end());
  EXPECT_TRUE(z_inv_it->second.empty());

  EXPECT_EQ(map_2d.Apply(0x00E1, /*inverted=*/true, 's'), 'a');
  EXPECT_EQ(map_2d.Apply(0x00E0, /*inverted=*/true, std::nullopt), 'a');
  EXPECT_EQ(map_2d.Apply('a', /*inverted=*/true, 'z'), std::nullopt);
  EXPECT_EQ(map_2d.Apply('e', /*inverted=*/true, std::nullopt), std::nullopt);
}

TEST(RuleAstTest, RegexAstAndRhsItemFactoryNormalization) {
  const RegexAST empty = RegexAST::MakeEmpty();
  EXPECT_EQ(empty.kind, RegexAST::Kind::kEmpty);

  // MakeConcat and MakeAlternation normalize 0-element and 1-element lists.
  EXPECT_EQ(RegexAST::MakeConcat({}).kind, RegexAST::Kind::kEmpty);
  EXPECT_EQ(RegexAST::MakeAlternation({}).kind, RegexAST::Kind::kEmpty);

  const RegexAST single_concat = RegexAST::MakeConcat(
      {RegexAST::MakePredicate(CharPredicate::Positive({'a'}))});
  EXPECT_EQ(single_concat.kind, RegexAST::Kind::kPredicate);
  EXPECT_TRUE(single_concat.predicate.Contains('a'));

  const RegexAST single_alt = RegexAST::MakeAlternation(
      {RegexAST::MakePredicate(CharPredicate::Positive({'b'}))});
  EXPECT_EQ(single_alt.kind, RegexAST::Kind::kPredicate);
  EXPECT_TRUE(single_alt.predicate.Contains('b'));

  std::vector<RegexAST> pair;
  pair.push_back(RegexAST::MakePredicate(CharPredicate::Positive({'a'})));
  pair.push_back(RegexAST::MakePredicate(CharPredicate::Positive({'b'})));
  const RegexAST concat = RegexAST::MakeConcat(std::move(pair));
  EXPECT_EQ(concat.kind, RegexAST::Kind::kConcat);
  EXPECT_EQ(concat.children.size(), 2u);

  const RegexAST macro_ref = RegexAST::MakeMacroRef("TONE", /*inverted=*/true,
                                                    /*key_bound=*/true, 19);
  EXPECT_EQ(macro_ref.kind, RegexAST::Kind::kMacroRef);
  EXPECT_EQ(macro_ref.macro_name, "TONE");
  EXPECT_TRUE(macro_ref.macro_inverted);
  EXPECT_TRUE(macro_ref.macro_key_bound);
  EXPECT_EQ(macro_ref.line, 19);

  const RHSItem lit = RHSItem::MakeLiteral(0x0111);
  EXPECT_EQ(lit.kind, RHSItem::Kind::kLiteral);
  EXPECT_EQ(lit.literal_char, 0x0111);

  const RHSItem key = RHSItem::MakeInputKey();
  EXPECT_EQ(key.kind, RHSItem::Kind::kInputKey);

  const RHSItem cap = RHSItem::MakeCapture(3);
  EXPECT_EQ(cap.kind, RHSItem::Kind::kCapture);
  EXPECT_EQ(cap.capture_index, 3);

  const RHSItem proj = RHSItem::MakeMapProjection("TONE", /*inverted=*/true, 2,
                                                  /*uses_key=*/true);
  EXPECT_EQ(proj.kind, RHSItem::Kind::kMapProjection);
  EXPECT_EQ(proj.map_name, "TONE");
  EXPECT_TRUE(proj.inverted);
  EXPECT_EQ(proj.capture_index, 2);
  EXPECT_TRUE(proj.uses_input_key);
}

TEST(RuleAstTest, Section1DirectivesEscapesCrlfAndNonCapturingMacros) {
  // Minimal empty grammar without %name or Section 3.
  {
    GrammarSpec minimal;
    RuleCompileError err;
    ASSERT_TRUE(ParseGrammarSpec("%%\r\n", minimal, err)) << err.Description();
    EXPECT_FALSE(minimal.has_name);
    EXPECT_TRUE(minimal.name.empty());
    EXPECT_TRUE(minimal.flags.empty());
    EXPECT_TRUE(minimal.rules.empty());
    EXPECT_TRUE(minimal.tests.empty());
  }

  constexpr std::string_view kSource =
      "%name \"Telex\\n\\r\\t\\\"\\\\ \xC4\x90\"\r\n"
      "%flag f_true = TRUE\r\n"
      "%flag f_one = 1\r\n"
      "%flag f_false = FALSE\r\n"
      "%flag f_zero = 0\r\n"
      "%flag f_default\r\n"
      "%map ESC_KEYS(\r\n"
      "  \\,,\r\n"
      "  \\),\r\n"
      "  \\\\,\r\n"
      "  \\t\r\n"
      ") {\r\n"
      "  a : \xC3\xA1 \xC3\xA0 \xE1\xBA\xA3 \xC3\xA3,\r\n"
      "}\r\n"
      "OVER_MACRO [a]\r\n"
      "OVER_MACRO (ab|cd)+\r\n"
      "%%\r\n"
      "%%\r\n";

  GrammarSpec spec;
  RuleCompileError err;
  ASSERT_TRUE(ParseGrammarSpec(kSource, spec, err)) << err.Description();

  EXPECT_TRUE(spec.has_name);
  EXPECT_EQ(spec.name, "Telex\n\r\t\"\\ \xC4\x90");

  ASSERT_EQ(spec.flags.size(), 5u);
  EXPECT_EQ(spec.flags[0].name, "f_true");
  EXPECT_TRUE(spec.flags[0].default_value);
  EXPECT_EQ(spec.flags[0].line, 2);
  EXPECT_EQ(spec.flags[1].name, "f_one");
  EXPECT_TRUE(spec.flags[1].default_value);
  EXPECT_EQ(spec.flags[2].name, "f_false");
  EXPECT_FALSE(spec.flags[2].default_value);
  EXPECT_EQ(spec.flags[3].name, "f_zero");
  EXPECT_FALSE(spec.flags[3].default_value);
  EXPECT_EQ(spec.flags[4].name, "f_default");
  EXPECT_FALSE(spec.flags[4].default_value);

  auto map_it = spec.maps.find("ESC_KEYS");
  ASSERT_NE(map_it, spec.maps.end());
  EXPECT_TRUE(map_it->second.is_2d);
  EXPECT_EQ(map_it->second.keys, (std::vector<uint16_t>{',', ')', '\\', '\t'}));
  EXPECT_EQ(map_it->second.Apply('a', /*inverted=*/false, ','), 0x00E1);
  EXPECT_EQ(map_it->second.Apply('a', /*inverted=*/false, ')'), 0x00E0);
  EXPECT_EQ(map_it->second.Apply('a', /*inverted=*/false, '\\'), 0x1EA3);
  EXPECT_EQ(map_it->second.Apply('a', /*inverted=*/false, '\t'), 0x00E3);

  // Parentheses inside Section 1 macros must be parsed as non-capturing kGroup,
  // never kCapture, and redefining a macro replaces the earlier entry.
  auto macro_it = spec.macros.find("OVER_MACRO");
  ASSERT_NE(macro_it, spec.macros.end());
  const RegexAST& macro_ast = macro_it->second;
  ASSERT_EQ(macro_ast.kind, RegexAST::Kind::kQuantifier);
  EXPECT_EQ(macro_ast.quant_min, 1);
  EXPECT_EQ(macro_ast.quant_max, -1);
  ASSERT_EQ(macro_ast.children.size(), 1u);
  EXPECT_EQ(macro_ast.children[0].kind, RegexAST::Kind::kGroup);
  EXPECT_EQ(macro_ast.children[0].capture_index, 0);
}

TEST(RuleAstTest, RegexAstPrecedenceQuantifiersClassesAndBraceRefs) {
  constexpr std::string_view kSource = R"(
%map CIRC { a:â }
%map TONE(s, f) { [aáà] : á à }
VOWEL [a]
%%
a|bc* + x --> y
((a)(?:b|c)?(d+)*) + x --> $1$2$3
[a-dZ-X\t\]\\-] + x --> y
[\sa] + [^\sa-b] --> y
.\s\+\*\?\(\)\[\{\n\r\t + x --> y
{ VOWEL }{ !CIRC }{ !TONE($0) }{ !TONE ( $key ) } + x --> y
a+ + + --> y
a+? + \+ --> y
a + '+' --> y
)";

  GrammarSpec spec;
  RuleCompileError err;
  ASSERT_TRUE(ParseGrammarSpec(kSource, spec, err)) << err.Description();
  ASSERT_EQ(spec.rules.size(), 9u);

  // Rule 0: `a|bc*` -> Alternation(Predicate('a'), Concat(Predicate('b'), c*))
  const RegexAST& r0 = spec.rules[0].lhs;
  ASSERT_EQ(r0.kind, RegexAST::Kind::kAlternation);
  ASSERT_EQ(r0.children.size(), 2u);
  EXPECT_EQ(r0.children[0].kind, RegexAST::Kind::kPredicate);
  EXPECT_TRUE(r0.children[0].predicate.Contains('a'));
  ASSERT_EQ(r0.children[1].kind, RegexAST::Kind::kConcat);
  ASSERT_EQ(r0.children[1].children.size(), 2u);
  EXPECT_TRUE(r0.children[1].children[0].predicate.Contains('b'));
  EXPECT_EQ(r0.children[1].children[1].kind, RegexAST::Kind::kQuantifier);
  EXPECT_EQ(r0.children[1].children[1].quant_min, 0);
  EXPECT_EQ(r0.children[1].children[1].quant_max, -1);

  // Rule 1: `((a)(?:b|c)?(d+)*)` -> nested capture numbering skips `(?:...)`.
  const RuleDecl& rule1 = spec.rules[1];
  EXPECT_EQ(rule1.capture_count, 3);
  ASSERT_EQ(rule1.lhs.kind, RegexAST::Kind::kCapture);
  EXPECT_EQ(rule1.lhs.capture_index, 1);
  ASSERT_EQ(rule1.lhs.children.size(), 1u);
  const RegexAST& r1_inner = rule1.lhs.children[0];
  ASSERT_EQ(r1_inner.kind, RegexAST::Kind::kConcat);
  ASSERT_EQ(r1_inner.children.size(), 3u);
  EXPECT_EQ(r1_inner.children[0].kind, RegexAST::Kind::kCapture);
  EXPECT_EQ(r1_inner.children[0].capture_index, 2);
  ASSERT_EQ(r1_inner.children[1].kind, RegexAST::Kind::kQuantifier);
  EXPECT_EQ(r1_inner.children[1].quant_min, 0);
  EXPECT_EQ(r1_inner.children[1].quant_max, 1);
  EXPECT_EQ(r1_inner.children[1].children[0].kind, RegexAST::Kind::kGroup);
  ASSERT_EQ(r1_inner.children[2].kind, RegexAST::Kind::kQuantifier);
  ASSERT_EQ(r1_inner.children[2].children[0].kind, RegexAST::Kind::kCapture);
  EXPECT_EQ(r1_inner.children[2].children[0].capture_index, 3);

  // Rule 2: `[a-dZ-X\t\]\\-]` -> forward & reversed ranges, escapes, trailing
  // '-'.
  const CharPredicate& r2_pred = spec.rules[2].lhs.predicate;
  EXPECT_EQ(r2_pred.kind, CharPredicate::Kind::kPositive);
  for (uint16_t ch :
       {'a', 'b', 'c', 'd', 'x', 'y', 'z', '\t', ']', '\\', '-'}) {
    EXPECT_TRUE(r2_pred.Contains(ch));
  }
  EXPECT_FALSE(r2_pred.Contains('e'));

  // Rule 3: `[\sa] + [^\sa-b]` -> `\s` in positive class vs negated class.
  const CharPredicate& r3_lhs = spec.rules[3].lhs.predicate;
  EXPECT_EQ(r3_lhs.kind, CharPredicate::Kind::kPositive);
  EXPECT_TRUE(r3_lhs.Contains(' '));
  EXPECT_TRUE(r3_lhs.Contains('\t'));
  EXPECT_TRUE(r3_lhs.Contains('\n'));
  EXPECT_TRUE(r3_lhs.Contains('\r'));
  EXPECT_TRUE(r3_lhs.Contains('a'));

  const CharPredicate& r3_in = spec.rules[3].input_predicate;
  EXPECT_EQ(r3_in.kind, CharPredicate::Kind::kNegative);
  EXPECT_FALSE(r3_in.Contains('a'));
  EXPECT_FALSE(r3_in.Contains('b'));
  EXPECT_TRUE(r3_in.Contains('c'));

  // Rule 4: `.\s\+\*\?\(\)\[\{\n\r\t` -> wildcard, \s, and escaped metachars.
  const RegexAST& r4 = spec.rules[4].lhs;
  ASSERT_EQ(r4.kind, RegexAST::Kind::kConcat);
  ASSERT_EQ(r4.children.size(), 12u);
  EXPECT_EQ(r4.children[0].predicate.kind, CharPredicate::Kind::kAnyWordChar);
  EXPECT_TRUE(r4.children[1].predicate.Contains(' '));
  EXPECT_TRUE(r4.children[1].predicate.Contains('\t'));
  const std::vector<uint16_t> expected_escaped = {'+', '*', '?',  '(',  ')',
                                                  '[', '{', '\n', '\r', '\t'};
  for (size_t i = 0; i < expected_escaped.size(); ++i) {
    EXPECT_TRUE(r4.children[i + 2].predicate.Contains(expected_escaped[i]));
  }

  // Rule 5: `{ VOWEL }{ !CIRC }{ !TONE($0) }{ !TONE ( $key ) }`
  const RegexAST& r5 = spec.rules[5].lhs;
  ASSERT_EQ(r5.kind, RegexAST::Kind::kConcat);
  ASSERT_EQ(r5.children.size(), 4u);
  EXPECT_EQ(r5.children[0].macro_name, "VOWEL");
  EXPECT_FALSE(r5.children[0].macro_inverted);
  EXPECT_FALSE(r5.children[0].macro_key_bound);
  EXPECT_EQ(r5.children[1].macro_name, "CIRC");
  EXPECT_TRUE(r5.children[1].macro_inverted);
  EXPECT_FALSE(r5.children[1].macro_key_bound);
  EXPECT_EQ(r5.children[2].macro_name, "TONE");
  EXPECT_TRUE(r5.children[2].macro_inverted);
  EXPECT_TRUE(r5.children[2].macro_key_bound);
  EXPECT_EQ(r5.children[3].macro_name, "TONE");
  EXPECT_TRUE(r5.children[3].macro_inverted);
  EXPECT_TRUE(r5.children[3].macro_key_bound);

  // Rules 6..8: `+` quantifier vs ` + ` separator vs `+` input key.
  EXPECT_EQ(spec.rules[6].lhs.kind, RegexAST::Kind::kQuantifier);
  EXPECT_EQ(spec.rules[6].lhs.quant_min, 1);
  EXPECT_TRUE(spec.rules[6].input_predicate.Contains('+'));

  EXPECT_EQ(spec.rules[7].lhs.kind, RegexAST::Kind::kQuantifier);
  EXPECT_EQ(spec.rules[7].lhs.quant_min, 0);
  EXPECT_EQ(spec.rules[7].lhs.quant_max, 1);
  EXPECT_EQ(spec.rules[7].lhs.children[0].kind, RegexAST::Kind::kQuantifier);
  EXPECT_TRUE(spec.rules[7].input_predicate.Contains('+'));
  EXPECT_TRUE(spec.rules[8].input_predicate.Contains('+'));
}

TEST(RuleAstTest, Section2InputTriggersAndRhsItemSemantics) {
  constexpr std::string_view kSource = R"(
%flag guard = true
%map CIRC { a:â, e:ê }
%map TONE(s, f) { [aáà] : á à }
POS_TRIG [xz]
NEG_TRIG [^0-9]
%%
a + D --> b
a + Đ --> b
a + '\n' --> b
a + '\r' --> b
a + '\t' --> b
a + '\\' --> b
a + 'Ấ' --> b
a + . --> b
a + \s --> b
a + # --> \#\$\\\{\n\r\tđ
a + {POS_TRIG} --> b
a + {NEG_TRIG} --> b
a + {CIRC} --> b
?guard ^(a)(b)(c)(d)(e)(f)(g)(h)(i) + {TONE} --> $9$1$0$key{CIRC($1)}{!CIRC($1)}{TONE($2)}{TONE($2, $0)}{TONE($2, $key)}{!TONE($2)}{!TONE($2, $0)}{!TONE($2, $key)} # rhs comment
)";

  GrammarSpec spec;
  RuleCompileError err;
  ASSERT_TRUE(ParseGrammarSpec(kSource, spec, err)) << err.Description();
  ASSERT_EQ(spec.rules.size(), 14u);

  // Input trigger case-folding, escapes, wildcards, macros, and maps.
  EXPECT_TRUE(spec.rules[0].input_predicate.Contains('d'));
  EXPECT_TRUE(spec.rules[1].input_predicate.Contains(0x0111));  // 'đ'
  EXPECT_TRUE(spec.rules[2].input_predicate.Contains('\n'));
  EXPECT_TRUE(spec.rules[3].input_predicate.Contains('\r'));
  EXPECT_TRUE(spec.rules[4].input_predicate.Contains('\t'));
  EXPECT_TRUE(spec.rules[5].input_predicate.Contains('\\'));
  EXPECT_TRUE(spec.rules[6].input_predicate.Contains(0x1EA5));  // 'ấ'
  EXPECT_EQ(spec.rules[7].input_predicate.kind,
            CharPredicate::Kind::kAnyWordChar);
  EXPECT_TRUE(spec.rules[8].input_predicate.Contains(' '));
  EXPECT_TRUE(spec.rules[8].input_predicate.Contains('\n'));
  EXPECT_TRUE(spec.rules[9].input_predicate.Contains('#'));

  EXPECT_EQ(spec.rules[10].input_predicate.kind,
            CharPredicate::Kind::kPositive);
  EXPECT_TRUE(spec.rules[10].input_predicate.Contains('x'));
  EXPECT_TRUE(spec.rules[10].input_predicate.Contains('z'));

  EXPECT_EQ(spec.rules[11].input_predicate.kind,
            CharPredicate::Kind::kNegative);
  EXPECT_FALSE(spec.rules[11].input_predicate.Contains('5'));
  EXPECT_TRUE(spec.rules[11].input_predicate.Contains('a'));

  // {CIRC} as input spec resolves to 1D map's domain_set {'a', 'e'}.
  EXPECT_EQ(spec.rules[12].input_predicate.chars,
            (absl::flat_hash_set<uint16_t>{'a', 'e'}));

  // {TONE} as input spec resolves to 2D map's trigger keys {'s', 'f'}.
  const RuleDecl& r13 = spec.rules[13];
  EXPECT_EQ(r13.flag_name, "guard");
  EXPECT_TRUE(r13.is_anchored);
  EXPECT_EQ(r13.capture_count, 9);
  EXPECT_EQ(r13.input_predicate.chars,
            (absl::flat_hash_set<uint16_t>{'s', 'f'}));

  // Rule 9 RHS escaped literals: \# \$ \\ \{ \n \r \t đ
  const std::vector<uint16_t> expected_lits = {'#',  '$',  '\\', '{',
                                               '\n', '\r', '\t', 0x0111};
  ASSERT_EQ(spec.rules[9].rhs.size(), expected_lits.size());
  for (size_t i = 0; i < expected_lits.size(); ++i) {
    EXPECT_EQ(spec.rules[9].rhs[i].kind, RHSItem::Kind::kLiteral);
    EXPECT_EQ(spec.rules[9].rhs[i].literal_char, expected_lits[i]);
  }

  // Rule 13 RHS captures, input key tokens, and 1D/2D map projections.
  ASSERT_EQ(r13.rhs.size(), 12u);
  EXPECT_EQ(r13.rhs[0].kind, RHSItem::Kind::kCapture);
  EXPECT_EQ(r13.rhs[0].capture_index, 9);
  EXPECT_EQ(r13.rhs[1].kind, RHSItem::Kind::kCapture);
  EXPECT_EQ(r13.rhs[1].capture_index, 1);
  EXPECT_EQ(r13.rhs[2].kind, RHSItem::Kind::kInputKey);
  EXPECT_EQ(r13.rhs[3].kind, RHSItem::Kind::kInputKey);

  // {CIRC($1)} and {!CIRC($1)}
  EXPECT_EQ(r13.rhs[4].map_name, "CIRC");
  EXPECT_FALSE(r13.rhs[4].inverted);
  EXPECT_EQ(r13.rhs[4].capture_index, 1);
  EXPECT_FALSE(r13.rhs[4].uses_input_key);

  EXPECT_EQ(r13.rhs[5].map_name, "CIRC");
  EXPECT_TRUE(r13.rhs[5].inverted);
  EXPECT_EQ(r13.rhs[5].capture_index, 1);
  EXPECT_FALSE(r13.rhs[5].uses_input_key);

  // {TONE($2)} defaults uses_input_key=true because TONE is a forward 2D map.
  for (size_t idx : {6u, 7u, 8u}) {
    EXPECT_EQ(r13.rhs[idx].map_name, "TONE");
    EXPECT_FALSE(r13.rhs[idx].inverted);
    EXPECT_EQ(r13.rhs[idx].capture_index, 2);
    EXPECT_TRUE(r13.rhs[idx].uses_input_key);
  }

  // {!TONE($2)} has uses_input_key=false, whereas {!TONE($2, $0|$key)} sets it.
  EXPECT_EQ(r13.rhs[9].map_name, "TONE");
  EXPECT_TRUE(r13.rhs[9].inverted);
  EXPECT_FALSE(r13.rhs[9].uses_input_key);

  EXPECT_TRUE(r13.rhs[10].inverted);
  EXPECT_TRUE(r13.rhs[10].uses_input_key);
  EXPECT_TRUE(r13.rhs[11].inverted);
  EXPECT_TRUE(r13.rhs[11].uses_input_key);
}

TEST(RuleAstTest, Section3TestVectorsEscapesCommentsAndMultilineLayout) {
  constexpr std::string_view kSource = R"(
%flag f1 = false
%flag f2 = true
%%
a + s --> á
%%
"" + 'a' --> "a"
"trường" + 'z' --> "trương"
"\"\\\n\r\t" + '\'' --> "ok"
"nl" + '\n' --> "nl\n"
"utf8" + 'đ' --> "đ"
?f1
/* comment between flags */
?f2
"multi" // inline comment
  + # hash comment
  '\\'
  -->
  "line_out"
)";

  GrammarSpec spec;
  RuleCompileError err;
  ASSERT_TRUE(ParseGrammarSpec(kSource, spec, err)) << err.Description();
  ASSERT_EQ(spec.tests.size(), 6u);

  EXPECT_EQ(spec.tests[0].existing_text, "");
  EXPECT_EQ(spec.tests[0].input_key, 'a');
  EXPECT_EQ(spec.tests[0].expected_output, "a");
  EXPECT_EQ(spec.tests[0].caret_position, -1);
  EXPECT_TRUE(spec.tests[0].enabled_flags.empty());

  EXPECT_EQ(spec.tests[1].existing_text, "trường");
  EXPECT_EQ(spec.tests[1].input_key, 'z');
  EXPECT_EQ(spec.tests[1].expected_output, "trương");

  EXPECT_EQ(spec.tests[2].existing_text, "\"\\\n\r\t");
  EXPECT_EQ(spec.tests[2].input_key, '\'');
  EXPECT_EQ(spec.tests[2].expected_output, "ok");

  EXPECT_EQ(spec.tests[3].input_key, '\n');
  EXPECT_EQ(spec.tests[3].expected_output, "nl\n");

  EXPECT_EQ(spec.tests[4].input_key, 0x0111);
  EXPECT_EQ(spec.tests[4].expected_output, "đ");

  EXPECT_EQ(spec.tests[5].enabled_flags,
            (absl::flat_hash_set<std::string>{"f1", "f2"}));
  EXPECT_EQ(spec.tests[5].existing_text, "multi");
  EXPECT_EQ(spec.tests[5].input_key, '\\');
  EXPECT_EQ(spec.tests[5].expected_output, "line_out");
}

TEST(RuleAstTest, ReportsAllGrammarErrorsWithAccurateLinesAndMessages) {
  struct ErrorCase {
    std::string_view name;
    std::string_view source;
    int expected_line;
    std::string_view expected_substr;
  };

  const std::vector<ErrorCase> kCases = {
      // Lexical & Section Delimiters
      {"unterminated_block_comment", "/* open\nstill open\n%%\n", 1,
       "Unterminated '/*'"},
      {"missing_section_delimiter", "%name \"NoRules\"\n", 2, "Missing '%%'"},
      {"unknown_directive", "%unknown foo\n%%\n", 1, "Unknown directive"},
      {"unexpected_section1_token", "123bad\n%%\n", 1,
       "Expected macro definition or directive"},

      // %name Directive
      {"name_unquoted", "%name Bare\n%%\n", 1,
       "Expected double-quoted string in '%name'"},
      {"name_unclosed_quote", "%name \"unclosed\n%%\n", 1,
       "Expected double-quoted string in '%name'"},
      {"name_trailing_backslash", "%name \"esc\\\n%%\n", 1,
       "Expected double-quoted string in '%name'"},
      {"name_trailing_tokens", "%name \"ok\" extra\n%%\n", 1,
       "Expected double-quoted string in '%name'"},

      // %flag Directive
      {"flag_missing_name", "%flag = true\n%%\n", 1, "syntax error"},
      {"flag_digit_start", "%flag 1bad = true\n%%\n", 1,
       "Invalid flag identifier"},
      {"flag_missing_value", "%flag f =\n%%\n", 1, "syntax error"},
      {"flag_invalid_bool", "%flag f = maybe\n%%\n", 1,
       "Invalid boolean default"},
      {"flag_trailing_token", "%flag f = true extra\n%%\n", 1, "syntax error"},
      {"flag_duplicate", "%flag f = true\n%flag f = false\n%%\n", 2,
       "Duplicate '%flag'"},

      // %map Directive
      {"map_missing_name", "%map { a:b }\n%%\n", 1, "syntax error"},
      {"map_2d_empty_keys", "%map M() { a:b }\n%%\n", 1, "syntax error"},
      {"map_2d_leading_comma", "%map M(,a) { a:b }\n%%\n", 1, "syntax error"},
      {"map_2d_trailing_comma_key", "%map M(a,) { a:b }\n%%\n", 1,
       "syntax error"},
      {"map_2d_bad_key_escape", "%map M(\\\n) { a:b }\n%%\n", 1,
       "Invalid escape in '%map' key list"},
      {"map_2d_missing_comma_or_rparen", "%map M(a b) { a:b }\n%%\n", 1,
       "syntax error"},
      {"map_missing_lbrace", "%map M a:b }\n%%\n", 1, "syntax error"},
      {"map_unclosed_eof", "%map M {\n  a:b\n", 1, "Unclosed '%map' block."},
      {"map_unclosed_before_delim", "%map M {\n  a:b\n%%\n", 1,
       "Unclosed '%map' block before '%%'."},
      {"map_row_unclosed_bracket", "%map M {\n  [ab : c\n}\n%%\n", 2,
       "Expected '[...]'"},
      {"map_row_empty_brackets", "%map M {\n  [] : c\n}\n%%\n", 2,
       "Empty character list '[]'"},
      {"map_row_unexpected_delim", "%map M {\n  : c\n}\n%%\n", 2,
       "syntax error"},
      {"map_row_missing_colon", "%map M {\n  a b\n}\n%%\n", 2, "syntax error"},
      {"map_row_bad_target_delim", "%map M {\n  a : [b]\n}\n%%\n", 2,
       "syntax error"},
      {"map_row_missing_target", "%map M {\n  a :\n}\n%%\n", 2, "syntax error"},
      {"map_1d_multiple_targets", "%map M {\n  a : b c\n}\n%%\n", 1,
       "Expected single target character"},
      {"map_1d_empty_body", "%map M {\n}\n%%\n", 2, "syntax error"},
      {"map_2d_empty_body", "%map M(s) {\n}\n%%\n", 2, "syntax error"},
      {"map_2d_wrong_target_count", "%map M(s, f) {\n  a : b\n}\n%%\n", 1,
       "row has 1 outputs, expected 2"},

      // Section 1 Macros & Regex Syntax Errors
      {"macro_empty_pattern", "EMPTY_MACRO   \n%%\n", 1, "syntax error"},
      {"regex_leading_star", "BAD *a\n%%\n", 1, "syntax error"},
      {"regex_leading_plus", "BAD +a\n%%\n", 1, "syntax error"},
      {"regex_leading_quest", "BAD ?a\n%%\n", 1, "syntax error"},
      {"regex_leading_rbracket", "BAD ]a\n%%\n", 1,
       "Unexpected metacharacter ']'"},
      {"regex_stray_rparen", "BAD a)\n%%\n", 1, "syntax error"},
      {"regex_leading_pipe", "BAD |a\n%%\n", 1, "syntax error"},
      {"regex_unclosed_paren", "BAD (ab\n%%\n", 1, "syntax error"},
      {"regex_unclosed_noncap", "BAD (?:ab\n%%\n", 1, "syntax error"},
      {"regex_unclosed_class", "BAD [ab\n%%\n", 1,
       "Unclosed '[' character class"},
      {"regex_escaped_rbracket_unclosed", "BAD [ab\\]\n%%\n", 1,
       "Unclosed '[' character class"},
      {"regex_unclosed_brace", "BAD {FOO\n%%\n", 1, "Unclosed '{' in pattern"},
      {"regex_stray_rbrace", "BAD }a\n%%\n", 1, "Unexpected metacharacter '}'"},
      {"regex_empty_brace", "BAD {}\n%%\n", 1,
       "Empty '{}' reference in pattern"},
      {"regex_empty_inv_brace", "BAD {!}\n%%\n", 1,
       "Empty '{}' reference in pattern"},
      {"regex_malformed_brace_call", "BAD {!M($0 extra}\n%%\n", 1,
       "Malformed macro reference"},
      {"regex_invalid_brace_arg", "BAD {!M($1)}\n%%\n", 1,
       "Expected '$0' inside"},
      {"regex_trailing_backslash", "BAD a\\\n%%\n", 1,
       "Trailing '\\' in pattern"},

      // Section 2 Rules, Input Triggers & RHS Replacements
      {"rule_bare_question_mark", "%%\n? a + b --> c\n", 2, "syntax error"},
      {"rule_undeclared_flag", "%%\n?undeclared a + b --> c\n", 2,
       "Undeclared flag '?undeclared'"},
      {"rule_flag_only_line", "%flag f = true\n%%\n?f\n", 3,
       "Expected pattern after flag guard in rule"},
      {"rule_anchored_empty_lhs", "%%\n^ + b --> c\n", 2, "syntax error"},
      {"rule_unanchored_leading_plus", "%%\n + b --> c\n", 2, "syntax error"},
      {"rule_missing_plus_separator_eol", "%%\nabc\n", 2,
       "Missing ' + ' input separator"},
      {"rule_missing_plus_separator_arrow", "%%\nabc --> d\n", 2,
       "Missing ' + ' input separator"},
      {"rule_missing_input_spec", "%%\na + --> b\n", 2, "syntax error"},
      {"rule_empty_char_literal", "%%\na + '' --> b\n", 2,
       "Invalid character literal"},
      {"rule_multichar_literal", "%%\na + 'xy' --> b\n", 2,
       "Invalid character literal"},
      {"rule_unclosed_char_literal", "%%\na + 'x\n", 2,
       "Invalid character literal"},
      {"rule_unclosed_input_brace", "%%\na + {UNCLOSED --> b\n", 2,
       "Unclosed '{' in input specification"},
      {"rule_multichar_input_macro", "SEQ ab\n%%\na + {SEQ} --> b\n", 3,
       "must resolve to a single character class"},
      {"rule_unknown_input_brace", "%%\na + {UNKNOWN} --> b\n", 2,
       "Unknown macro or map '{UNKNOWN}' in input specification"},
      {"rule_trailing_backslash_input", "%%\na + \\\n", 2,
       "Trailing '\\' in input specification"},
      {"rule_missing_arrow", "%%\na + b -> c\n", 2, "syntax error"},
      {"rule_empty_rhs_before_next_rule", "%%\na + b -->   \nc + d --> e\n", 2,
       "syntax error"},
      {"rule_empty_rhs_at_eof", "%%\na + b -->", 2, "syntax error"},
      {"rhs_capture_out_of_range", "%%\n(a) + b --> $2\n", 2,
       "only has 1 capture group(s)"},
      {"rhs_capture_with_zero_groups", "%%\na + b --> $1\n", 2,
       "only has 0 capture group(s)"},
      {"rhs_invalid_dollar_ident", "%%\na + b --> $x\n", 2,
       "Invalid '$' reference in replacement"},
      {"rhs_dangling_dollar", "%%\na + b --> $\n", 2,
       "Invalid '$' reference in replacement"},
      {"rhs_unclosed_projection_brace", "%%\n(a) + b --> {M($1)\n", 2,
       "Unclosed '{' in replacement"},
      {"rhs_projection_missing_parens", "%map M { a:b }\n%%\n(a) + b --> {M}\n",
       3, "Invalid map projection '{M}'"},
      {"rhs_projection_unknown_map", "%%\n(a) + b --> {NO_MAP($1)}\n", 2,
       "Unknown map 'NO_MAP'"},
      {"rhs_projection_too_many_args",
       "%map M(s) { a:b }\n%%\n(a) + s --> {M($1, $0, $0)}\n", 3,
       "expects 1 or 2 arguments"},
      {"rhs_projection_dollar_zero_first_arg",
       "%map M { a:b }\n%%\n(a) + b --> {M($0)}\n", 3,
       "First argument of '{M($0)}' must be a valid capture group"},
      {"rhs_projection_non_capture_first_arg",
       "%map M { a:b }\n%%\n(a) + b --> {M(abc)}\n", 3,
       "First argument of '{M(abc)}' must be a valid capture group"},
      {"rhs_projection_invalid_second_arg",
       "%map M(s) { a:b }\n%%\n(a) + s --> {M($1, $1)}\n", 3,
       "Second argument of '{M($1, $1)}' must be '$0'"},
      {"rhs_trailing_backslash", "%%\na + b --> c\\\n", 2,
       "Trailing '\\' in replacement"},

      // Section 3 Inline Verification Test Vectors
      {"test_extra_section_delimiter", "%%\n%%\n%%\n", 3,
       "Unexpected extra '%%'"},
      {"test_bare_question_mark", "%%\n%%\n? \"a\" + 'b' --> \"c\"\n", 3,
       "Unexpected token in test vector: '?'"},
      {"test_undeclared_flag", "%%\n%%\n?nope \"a\" + 'b' --> \"c\"\n", 3,
       "Undeclared flag '?nope' in test vector"},
      {"test_unquoted_existing", "%%\n%%\nbareword + 'b' --> \"c\"\n", 3,
       "Unexpected token in test vector"},
      {"test_missing_plus", "%%\n%%\n\"a\" 'b' --> \"c\"\n", 3, "syntax error"},
      {"test_string_instead_of_char_key", "%%\n%%\n\"a\" + \"b\" --> \"c\"\n",
       3, "syntax error"},
      {"test_missing_arrow", "%%\n%%\n\"a\" + 'b' -> \"c\"\n", 3,
       "Unexpected token in test vector: '-'"},
      {"test_unquoted_expected_output", "%%\n%%\n\"a\" + 'b' --> unquoted\n", 3,
       "Unexpected token in test vector: 'u'"},
  };

  for (const ErrorCase& tc : kCases) {
    GrammarSpec spec;
    RuleCompileError err;
    EXPECT_FALSE(ParseGrammarSpec(tc.source, spec, err))
        << "Expected failure for case: " << tc.name;
    EXPECT_EQ(err.line, tc.expected_line)
        << "Wrong line for case '" << tc.name << "': " << err.Description();
    EXPECT_TRUE(absl::StrContains(err.message, tc.expected_substr))
        << "Case '" << tc.name << "' got message '" << err.message
        << "', expected substring '" << tc.expected_substr << "'";
  }
}

}  // namespace
}  // namespace okeytre
