// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Compiles Apple `.strings` localization files (base + Windows overrides) into
// constexpr C++ lookup headers (`L10nKey` enum and O(1) string tables).

#ifndef L10NC_L10N_COMPILER_H_
#define L10NC_L10N_COMPILER_H_

#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"

namespace okeytre {

struct CompiledL10nCatalog {
  std::vector<std::string> sorted_keys;
  std::vector<std::string> enum_names;
  absl::flat_hash_map<std::string, std::string> en_table;
  absl::flat_hash_map<std::string, std::string> vi_table;
};

// Parses an Apple `.strings` payload (supporting UTF-8 BOM, `/* */` and `//`
// comments, and standard escape sequences) and inserts or overrides entries in
// `table`.
absl::Status ParseStringsSource(
    std::string_view source, std::string_view source_name,
    absl::flat_hash_map<std::string, std::string>& table);

// Compiles base + override `.strings` sources for English and Vietnamese,
// sorting keys and verifying unique `L10nKey` enum identifiers.
absl::StatusOr<CompiledL10nCatalog> CompileL10nCatalog(
    std::string_view en_base, std::string_view en_overrides,
    std::string_view vi_base, std::string_view vi_overrides);

// Emits `localization-keys.generated.h` defining `enum class L10nKey`.
absl::Cord GenerateCppL10nKeysHeader(const CompiledL10nCatalog& catalog);

// Emits `localization-tables.generated.h` defining `kEnglishStrings` and
// `kVietnameseStrings`.
absl::Cord GenerateCppL10nTablesHeader(const CompiledL10nCatalog& catalog);

}  // namespace okeytre

#endif  // L10NC_L10N_COMPILER_H_
