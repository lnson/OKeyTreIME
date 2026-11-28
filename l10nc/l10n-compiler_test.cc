// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "l10n-compiler.h"

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "gtest/gtest.h"

namespace okeytre {
namespace {

TEST(L10nCompilerTest, ParsesCommentsBomEscapesAndOverrides) {
  const std::string en_base =
      "\xEF\xBB\xBF"
      "/* Block comment */\n"
      "\"menu.off\" = \"Off\";\n"
      "// Line comment\n"
      "\"settings.quote\" = \"Say \\\"hi\\\"\\nNext\";\n";
  const std::string en_overrides = "\"menu.off\" = \"Disabled\";\n";
  const std::string vi_base = "\"menu.off\" = \"Tắt\";\n";

  const absl::StatusOr<CompiledL10nCatalog> catalog =
      CompileL10nCatalog(en_base, en_overrides, vi_base, "");
  ASSERT_TRUE(catalog.ok()) << catalog.status();
  ASSERT_EQ(catalog->sorted_keys.size(), 2u);
  EXPECT_EQ(catalog->sorted_keys[0], "menu.off");
  EXPECT_EQ(catalog->sorted_keys[1], "settings.quote");
  EXPECT_EQ(catalog->enum_names[0], "kMenuOff");
  EXPECT_EQ(catalog->enum_names[1], "kSettingsQuote");

  const std::string keys_h = std::string(GenerateCppL10nKeysHeader(*catalog));
  EXPECT_TRUE(absl::StrContains(keys_h, "kMenuOff,"));
  EXPECT_TRUE(absl::StrContains(keys_h, "kSettingsQuote,"));

  const std::string tables_h =
      std::string(GenerateCppL10nTablesHeader(*catalog));
  // Override applied to English.
  EXPECT_TRUE(absl::StrContains(tables_h, "L\"Disabled\","));
  // Vietnamese has its own value and falls back to English for missing key.
  EXPECT_TRUE(absl::StrContains(tables_h, "L\"Tắt\","));
  EXPECT_TRUE(absl::StrContains(tables_h, "L\"Say \\\"hi\\\"\\nNext\","));
}

TEST(L10nCompilerTest, RejectsDuplicateEnumIdentifiers) {
  const absl::StatusOr<CompiledL10nCatalog> catalog = CompileL10nCatalog(
      "\"foo.bar\" = \"A\";\n\"foo_bar\" = \"B\";\n", "", "", "");
  EXPECT_EQ(catalog.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(L10nCompilerTest, RejectsMalformedStringsSyntax) {
  absl::flat_hash_map<std::string, std::string> table;
  EXPECT_EQ(ParseStringsSource("\"key\" \"missing equals\";", "test", table)
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseStringsSource("\"key\" = \"missing semicolon\"", "test", table)
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseStringsSource("\"unterminated", "test", table).code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace okeytre
