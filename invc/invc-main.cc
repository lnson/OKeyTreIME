// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// CLI entry point for `invc`: compiles a `.inv` inversion-matcher specification
// of disallowed patterns into a standalone C++ DFA header after verifying all
// inline Section 3 assertions.

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
#include "inv-compiler.h"

ABSL_FLAG(std::string, input, "", "Path to input .inv specification file");
ABSL_FLAG(std::string, output, "", "Path to output C++ header file");
ABSL_FLAG(std::string, struct_name, "", "Generated C++ struct name");

namespace {

absl::StatusOr<std::string> ReadFileToString(std::string_view path) {
  std::ifstream in_file{std::string(path), std::ios::in | std::ios::binary};
  if (!in_file) {
    return absl::NotFoundError(
        absl::Substitute("invc error: unable to open $0", path));
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
        absl::Substitute("invc error: unable to write $0", path));
  }
  for (std::string_view chunk : content.Chunks()) {
    out_file.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
  }
  out_file.close();
  return absl::OkStatus();
}

absl::Status Run(std::string_view input_path, std::string_view output_path,
                 std::string_view struct_name) {
  ABSL_ASSIGN_OR_RETURN(std::string source, ReadFileToString(input_path));
  ABSL_ASSIGN_OR_RETURN(okeytre::CompiledInvDfa compiled,
                        okeytre::CompileInvSpec(source));
  ABSL_RETURN_IF_ERROR(okeytre::VerifyInvAssertions(compiled, input_path));

  absl::Status status = WriteCordToFile(
      okeytre::GenerateCppInvAutomaton(compiled, struct_name), output_path);
  if (status.ok()) {
    std::cout << absl::Substitute(
                     "Generated $0 ($1, $2 minimized DFA states, $3 classes, "
                     "$4 patterns, $5 verified assertions)\n",
                     output_path, struct_name, compiled.state_count,
                     compiled.num_classes, compiled.pattern_count,
                     compiled.assertions.size())
              << std::endl;
  }
  return status;
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  const std::string input_path = absl::GetFlag(FLAGS_input);
  const std::string output_path = absl::GetFlag(FLAGS_output);
  const std::string struct_name = absl::GetFlag(FLAGS_struct_name);

  if (input_path.empty() || output_path.empty() || struct_name.empty()) {
    std::cerr << "Usage: invc --input=<input.inv> --output=<output.h> "
                 "--struct_name=<StructName>\n";
    return EXIT_FAILURE;
  }

  const absl::Status status = Run(input_path, output_path, struct_name);
  if (!status.ok()) {
    std::cerr << status << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
