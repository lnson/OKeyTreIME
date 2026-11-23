// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Google Benchmark suite exercising the most expensive Telex automaton and
// stateful buffer execution paths:
//   1. 75-instruction, 10-tag PikeVM triphthong tone placement & retargeting
//      (Rules 24-26: `nguyền` + 's', `ngưỡi` + 's', `nghiễu` + 'j').
//   2. Full worst-case multi-keystroke syllable lifecycle with all 4 runtime
//      flags enabled (`modernTone`, `uoHorn`, `ngToNgh`, `nghToNg`), 5-way tone
//      retargeting, Kleene-star tone undo, and horn diphthong undo.
//   3. Dual-Shift `RestoreCurrentWord()` after multi-rule Telex rewrites.
//   4. Steady-state typing at maximum sliding-window capacity (`512` UTF-16
//      code units) with ~64 active `WordRawHistory` entries shifting per key.
//   5. Parameterized Kleene-star `{V_ANY}*` tone-undo scaling (`8..512` UTF-16
//      code units) across both `TelexAutomaton` and `InputMethodBuffer`.
//   6. End-to-end C-ABI (`okeytre_buffer_handle_input`) worst-case keystroke
//      round-trip matching the macOS `CGEventTap` hot path.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "benchmark/benchmark.h"
#include "input-method-buffer.h"
#include "okeytre-engine.h"

namespace okeytre {
namespace {

struct TriphthongRewriteCase {
  std::u16string_view word;
  std::u16string_view expected;
  uint16_t key = 0;
};

std::vector<uint16_t> MakeKleeneToneUndoWord(int32_t total_len) {
  // Layout: "ngh" (3) + 'a' * prefix_vowels + 'ế' (1) + 'u' * suffix_vowels +
  // "ng" (2). Matches Rule 4:
  //   ^({ONSET}{V_ANY}*)({!TONE($0)})({V_ANY}*{CODA}) + {TONE_KEY}
  // forcing simultaneous NFA thread splitting across both `{V_ANY}*` loops.
  std::vector<uint16_t> word;
  word.reserve(static_cast<size_t>(total_len));
  word.push_back('n');
  word.push_back('g');
  word.push_back('h');
  const int32_t remaining_vowels = total_len - 6;
  const int32_t prefix_vowels = remaining_vowels / 2;
  const int32_t suffix_vowels = remaining_vowels - prefix_vowels;
  for (int32_t i = 0; i < prefix_vowels; ++i) {
    word.push_back('a');
  }
  word.push_back(0x1EBF);  // 'ế' (acute tone on 'ê')
  for (int32_t i = 0; i < suffix_vowels; ++i) {
    word.push_back('u');
  }
  word.push_back('n');
  word.push_back('g');
  return word;
}

void BM_TelexAutomaton_10TagTriphthongRewrite(benchmark::State& state) {
  TelexAutomaton telex;
  telex.modernTone = true;
  telex.uoHorn = true;
  telex.ngToNgh = true;
  telex.nghToNg = true;

  // Rules 24, 25, and 26 compile to the largest PikeVM programs in Telex:
  // 75 instructions + 10 capture tags ($1..$5) + 3 vowel tone-map lookups.
  constexpr TriphthongRewriteCase kCases[3] = {
      {u"nguy\u1EC1n", u"nguy\u1EBFn", 's'},  // nguyền + s -> nguyến (Rule 24)
      {u"ng\u01B0\u1EE1i", u"ng\u01B0\u1EDBi",
       's'},                                  // ngưỡi + s -> ngưới (Rule 25)
      {u"nghi\u1EC5u", u"nghi\u1EC7u", 'j'},  // nghiễu + j -> nghiệu (Rule 26)
  };

  uint16_t out[16] = {};
  size_t case_idx = 0;
  while (state.KeepRunning()) {
    const TriphthongRewriteCase& tc = kCases[case_idx];
    case_idx = (case_idx + 1 == 3) ? 0 : (case_idx + 1);
    int32_t out_len = telex.RewriteToBuffer(
        reinterpret_cast<const uint16_t*>(tc.word.data()),
        static_cast<int32_t>(tc.word.size()), tc.key, out, 16);
    benchmark::DoNotOptimize(out_len);
    benchmark::DoNotOptimize(out);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TelexAutomaton_10TagTriphthongRewrite);

void BM_InputMethodBuffer_HeavySyllableStream(benchmark::State& state) {
  constexpr std::u16string_view kKeystrokes = u"ngieengsfrxjjs nghuwoisfrxjjw ";
  constexpr benchmark::IterationCount kBatchSize =
      static_cast<benchmark::IterationCount>(kKeystrokes.size());

  InputMethodBuffer ime(InputMethodStyle::kTelex, /*modern_tone=*/true,
                        /*uo_horn=*/true, /*ng_to_ngh=*/true,
                        /*ngh_to_ng=*/true,
                        InputMethodBuffer::kMaxAllowedBufferSize);

  while (state.KeepRunningBatch(kBatchSize)) {
    ime.Reset();
    for (char16_t raw_ch : kKeystrokes) {
      const uint16_t ch = static_cast<uint16_t>(raw_ch);
      TextEditAction action = ime.HandleInput(&ch, 1);
      benchmark::DoNotOptimize(action);
    }
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_InputMethodBuffer_HeavySyllableStream);

void BM_InputMethodBuffer_DualShiftWordRestore(benchmark::State& state) {
  constexpr std::u16string_view kStem = u"ngieengs";
  InputMethodBuffer ime(InputMethodStyle::kTelex, /*modern_tone=*/true,
                        /*uo_horn=*/true, /*ng_to_ngh=*/true,
                        /*ngh_to_ng=*/true,
                        InputMethodBuffer::kMaxAllowedBufferSize);

  while (state.KeepRunning()) {
    ime.Reset();
    for (char16_t raw_ch : kStem) {
      const uint16_t ch = static_cast<uint16_t>(raw_ch);
      TextEditAction step = ime.HandleInput(&ch, 1);
      benchmark::DoNotOptimize(step);
    }
    TextEditAction restored = ime.RestoreCurrentWord();
    benchmark::DoNotOptimize(restored);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_InputMethodBuffer_DualShiftWordRestore);

void BM_InputMethodBuffer_512WindowHistoryChurn(benchmark::State& state) {
  InputMethodBuffer ime(InputMethodStyle::kTelex, /*modern_tone=*/true,
                        /*uo_horn=*/true, /*ng_to_ngh=*/true,
                        /*ngh_to_ng=*/true,
                        InputMethodBuffer::kMaxAllowedBufferSize);

  constexpr std::u16string_view kRepeatedWord = u"nguyeens ";
  constexpr size_t kWordLen = kRepeatedWord.size();
  for (int32_t w = 0; w < 80; ++w) {
    for (char16_t raw_ch : kRepeatedWord) {
      const uint16_t ch = static_cast<uint16_t>(raw_ch);
      TextEditAction warmup_action = ime.HandleInput(&ch, 1);
      benchmark::DoNotOptimize(warmup_action);
    }
  }

  size_t pos = 0;
  while (state.KeepRunning()) {
    const uint16_t ch = static_cast<uint16_t>(kRepeatedWord[pos]);
    pos = (pos + 1 == kWordLen) ? 0 : (pos + 1);
    TextEditAction action = ime.HandleInput(&ch, 1);
    benchmark::DoNotOptimize(action);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_InputMethodBuffer_512WindowHistoryChurn);

void BM_TelexAutomaton_KleeneStarToneUndo(benchmark::State& state) {
  const int32_t word_len = static_cast<int32_t>(state.range(0));
  const std::vector<uint16_t> word = MakeKleeneToneUndoWord(word_len);
  std::vector<uint16_t> out(static_cast<size_t>(word_len + 8), 0);

  TelexAutomaton telex;
  telex.modernTone = true;

  while (state.KeepRunning()) {
    int32_t out_len =
        telex.RewriteToBuffer(word.data(), word_len, 's', out.data(),
                              static_cast<int32_t>(out.size()));
    assert(out_len == word_len + 1);
    uint16_t* out_ptr = out.data();
    benchmark::DoNotOptimize(out_len);
    benchmark::DoNotOptimize(out_ptr);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(word_len * sizeof(uint16_t)));
}
BENCHMARK(BM_TelexAutomaton_KleeneStarToneUndo)
    ->Arg(8)
    ->Arg(64)
    ->Arg(256)
    ->Arg(InputMethodBuffer::kMaxAllowedBufferSize);

void BM_InputMethodBuffer_512CharKleeneUndoRewrite(benchmark::State& state) {
  constexpr int32_t kMaxLen = InputMethodBuffer::kMaxAllowedBufferSize;
  const std::vector<uint16_t> word = MakeKleeneToneUndoWord(kMaxLen);
  const uint16_t key_s = 's';

  InputMethodBuffer ime(InputMethodStyle::kTelex, /*modern_tone=*/true,
                        /*uo_horn=*/true, /*ng_to_ngh=*/true,
                        /*ngh_to_ng=*/true, kMaxLen);

  while (state.KeepRunning()) {
    ime.SeedWithSurroundingText(word.data(), kMaxLen, kMaxLen);
    TextEditAction action = ime.HandleInput(&key_s, 1);
    benchmark::DoNotOptimize(action);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_InputMethodBuffer_512CharKleeneUndoRewrite);

void BM_CAbi_512WindowHeavyTelexPerKey(benchmark::State& state) {
  OKTInputMethodBuffer* buf =
      okeytre_buffer_create(OKT_STYLE_TELEX, /*modern_tone=*/true,
                            /*uo_horn=*/true, /*ng_to_ngh=*/true,
                            /*ngh_to_ng=*/true, OKT_BUFFER_MAX_SIZE);
  assert(buf != nullptr);

  constexpr std::u16string_view kStream = u"nguyeensfrxjjs ";
  constexpr size_t kStreamLen = kStream.size();
  int32_t backspaces = 0;
  uint16_t out_text[1024] = {};
  int32_t out_len = 0;

  for (int32_t w = 0; w < 60; ++w) {
    for (char16_t raw_ch : kStream) {
      const uint16_t ch = static_cast<uint16_t>(raw_ch);
      (void)okeytre_buffer_handle_input(buf, &ch, 1, OKT_MOD_NONE, &backspaces,
                                        out_text, 1024, &out_len);
    }
  }

  size_t pos = 0;
  while (state.KeepRunning()) {
    const uint16_t ch = static_cast<uint16_t>(kStream[pos]);
    pos = (pos + 1 == kStreamLen) ? 0 : (pos + 1);
    bool rewritten = okeytre_buffer_handle_input(
        buf, &ch, 1, OKT_MOD_NONE, &backspaces, out_text, 1024, &out_len);
    benchmark::DoNotOptimize(rewritten);
    benchmark::DoNotOptimize(backspaces);
    benchmark::DoNotOptimize(out_len);
  }

  okeytre_buffer_destroy(buf);
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CAbi_512WindowHeavyTelexPerKey);

}  // namespace
}  // namespace okeytre
