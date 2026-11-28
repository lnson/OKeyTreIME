// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// CLI entry point for `l10nc`: compiles Apple `.strings` files (base + Windows
// overrides) into constexpr C++ localization headers.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/substitute.h"
#include "l10n-compiler.h"

ABSL_FLAG(std::string, en_base, "", "Path to base English .strings file");
ABSL_FLAG(std::string, en_overrides, "",
          "Path to English override .strings file");
ABSL_FLAG(std::string, vi_base, "", "Path to base Vietnamese .strings file");
ABSL_FLAG(std::string, vi_overrides, "",
          "Path to Vietnamese override .strings file");
ABSL_FLAG(std::string, out_keys, "", "Path to output L10nKey enum header");
ABSL_FLAG(std::string, out_tables, "",
          "Path to output localization tables header");

namespace {

absl::StatusOr<std::string> ReadFileToString(std::string_view path) {
  std::ifstream in_file{std::string(path), std::ios::in | std::ios::binary};
  if (!in_file) {
    return absl::NotFoundError(
        absl::Substitute("l10nc error: unable to open $0", path));
  }
  std::ostringstream ss;
  ss << in_file.rdbuf();
  return ss.str();
}

absl::Status WriteCordToFile(const absl::Cord& content, std::string_view path) {
  std::ofstream out_file{std::string(path),
                         std::ios::out | std::ios::binary | std::ios::trunc};
  if (!out_file) {
    return absl::InternalError(
        absl::Substitute("l10nc error: unable to write $0", path));
  }
  for (std::string_view chunk : content.Chunks()) {
    out_file.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
  }
  out_file.close();
  if (!out_file) {
    return absl::InternalError(
        absl::Substitute("l10nc error: failed writing $0", path));
  }
  return absl::OkStatus();
}

absl::Status Run(std::string_view en_base_path,
                 std::string_view en_overrides_path,
                 std::string_view vi_base_path,
                 std::string_view vi_overrides_path,
                 std::string_view out_keys_path,
                 std::string_view out_tables_path) {
  ABSL_ASSIGN_OR_RETURN(std::string en_base, ReadFileToString(en_base_path));
  ABSL_ASSIGN_OR_RETURN(std::string en_overrides,
                        ReadFileToString(en_overrides_path));
  ABSL_ASSIGN_OR_RETURN(std::string vi_base, ReadFileToString(vi_base_path));
  ABSL_ASSIGN_OR_RETURN(std::string vi_overrides,
                        ReadFileToString(vi_overrides_path));

  ABSL_ASSIGN_OR_RETURN(
      okeytre::CompiledL10nCatalog catalog,
      okeytre::CompileL10nCatalog(en_base, en_overrides, vi_base,
                                  vi_overrides));

  ABSL_RETURN_IF_ERROR(
      WriteCordToFile(okeytre::GenerateCppL10nKeysHeader(catalog),
                      out_keys_path));
  ABSL_RETURN_IF_ERROR(
      WriteCordToFile(okeytre::GenerateCppL10nTablesHeader(catalog),
                      out_tables_path));

  std::cout << absl::Substitute("Generated $0 and $1 ($2 keys)\n",
                                out_keys_path, out_tables_path,
                                catalog.sorted_keys.size())
            << std::endl;
  return absl::OkStatus();
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  const std::string en_base = absl::GetFlag(FLAGS_en_base);
  const std::string en_overrides = absl::GetFlag(FLAGS_en_overrides);
  const std::string vi_base = absl::GetFlag(FLAGS_vi_base);
  const std::string vi_overrides = absl::GetFlag(FLAGS_vi_overrides);
  const std::string out_keys = absl::GetFlag(FLAGS_out_keys);
  const std::string out_tables = absl::GetFlag(FLAGS_out_tables);

  if (en_base.empty() || en_overrides.empty() || vi_base.empty() ||
      vi_overrides.empty() || out_keys.empty() || out_tables.empty()) {
    std::cerr << "Usage: l10nc --en_base=<file> --en_overrides=<file> "
                 "--vi_base=<file> --vi_overrides=<file> "
                 "--out_keys=<keys.h> --out_tables=<tables.h>\n";
    return EXIT_FAILURE;
  }

  const absl::Status status =
      Run(en_base, en_overrides, vi_base, vi_overrides, out_keys, out_tables);
  if (!status.ok()) {
    std::cerr << status << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
