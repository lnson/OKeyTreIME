// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Ahead-of-Time `.ime` -> `.generated.hpp` Compiler Entry Point
// Usage:
// bazel build //:generated_automata

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/substitute.h"
#include "automaton-compiler.h"
#include "compiled-automaton.h"
#include "cpp-automaton-codegen.h"
#include "rule-ast.h"
#include "unicode-utils.h"

ABSL_FLAG(std::string, input, "", "Path to input .ime grammar file");
ABSL_FLAG(std::string, output, "", "Path to output C++ header file");
ABSL_FLAG(std::string, struct_name, "", "Generated C++ struct name");

namespace {

absl::StatusOr<std::string> ReadFileToString(std::string_view path) {
  std::ifstream in_file{std::string(path), std::ios::in | std::ios::binary};
  if (!in_file) {
    return absl::NotFoundError(
        absl::Substitute("imec error: unable to open $0", path));
  }
  std::ostringstream ss;
  ss << in_file.rdbuf();
  return ss.str();
}

absl::StatusOr<okeytre::CompiledAutomaton> CompileGrammarSource(
    std::string_view source) {
  okeytre::CompiledAutomaton compiled;
  okeytre::RuleCompileError err;
  if (!okeytre::CompileAutomaton(source, compiled, err)) {
    return absl::InvalidArgumentError(
        absl::Substitute("imec error at line $0: $1", err.line, err.message));
  }
  return compiled;
}

absl::Status RunOneTest(const okeytre::CompiledAutomaton& compiled,
                        const okeytre::RuleTestVector& vector,
                        std::string_view input_path) {
  std::u16string initial_chars = okeytre::Utf8ToU16(vector.existing_text);
  std::u16string expected_chars = okeytre::Utf8ToU16(vector.expected_output);
  size_t caret =
      (vector.caret_position >= 0 &&
       static_cast<size_t>(vector.caret_position) <= initial_chars.size())
          ? static_cast<size_t>(vector.caret_position)
          : initial_chars.size();
  okeytre::CompiledAutomaton test_automaton =
      vector.enabled_flags.empty()
          ? compiled
          : compiled.WithEnabledFlags(vector.enabled_flags);

  std::u16string updated;
  std::u16string rewritten;
  if (test_automaton.Rewrite(
          std::u16string_view(initial_chars).substr(0, caret), vector.input_key,
          rewritten)) {
    updated = std::move(rewritten);
    updated.append(initial_chars, caret);
  } else {
    updated = initial_chars;
    updated.insert(caret, 1, static_cast<char16_t>(vector.input_key));
  }
  if (updated != expected_chars) {
    return absl::FailedPreconditionError(absl::Substitute(
        "Assertion failed at $0:$1 for input \"$2\" + '$3' (expected \"$4\")",
        input_path, vector.line, vector.existing_text,
        static_cast<unsigned int>(vector.input_key), vector.expected_output));
  }
  return absl::OkStatus();
}

absl::Status RunTestCases(const okeytre::CompiledAutomaton& compiled,
                          std::string_view input_path) {
  for (const okeytre::RuleTestVector& vector : compiled.tests) {
    ABSL_RETURN_IF_ERROR(RunOneTest(compiled, vector, input_path));
  }
  return absl::OkStatus();
}

absl::Status WriteCordToFile(const absl::Cord& content, std::string_view path) {
  std::ofstream out_file{std::string(path),
                         std::ios::out | std::ios::binary | std::ios::trunc};
  if (!out_file) {
    return absl::InternalError(
        absl::Substitute("imec error: unable to write $0", path));
  }
  for (std::string_view chunk : content.Chunks()) {
    out_file.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
  }
  out_file.close();
  return absl::OkStatus();
}

absl::Status Run(std::string_view input_path, std::string_view output_path,
                 std::string_view type_name) {
  ABSL_ASSIGN_OR_RETURN(std::string source, ReadFileToString(input_path));
  ABSL_ASSIGN_OR_RETURN(okeytre::CompiledAutomaton compiled,
                        CompileGrammarSource(source));
  ABSL_RETURN_IF_ERROR(RunTestCases(compiled, input_path));

  absl::Status status = WriteCordToFile(
      okeytre::GenerateCppAutomaton(compiled, type_name), output_path);
  if (status.ok()) {
    std::cout
        << absl::Substitute(
               "Generated $0 ($1, $2 DFAs, $3 rules, $4 verified assertions)\n",
               output_path, type_name, compiled.key_automata.size(),
               compiled.rules.size(), compiled.tests.size())
        << std::endl;
  }
  return status;
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  const std::string input_path = absl::GetFlag(FLAGS_input);
  const std::string output_path = absl::GetFlag(FLAGS_output);
  const std::string type_name = absl::GetFlag(FLAGS_struct_name);

  if (input_path.empty() || output_path.empty() || type_name.empty()) {
    std::cerr << "Usage: imec --input=<input.ime> --output=<output.hpp> "
                 "--struct_name=<StructName>\n";
    return EXIT_FAILURE;
  }

  absl::Status status = Run(input_path, output_path, type_name);
  if (!status.ok()) {
    std::cerr << status << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
