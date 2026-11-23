// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Google Benchmark suite measuring `VietnameseInvAutomaton::Matches` and
// `InputMethodBuffer` word-boundary auto-restoration:
//   1. Full-syllable DFA scans on valid Vietnamese words (including 3-letter
//      onsets, triphthongs, 2-letter codas, and mixed case).
//   2. Early-exit absorbing-accept rejections on mangled English/identifier
//      tokens (`(kStateFlags[state] & 2u) != 0u`).
//   3. End-anchored (`$`) rejections requiring full-syllable traversal (e.g.
//      stop-coda tone violations, open checked vowels/diphthongs, and velar
//      coda restrictions).
//   4. Long-token scaling (`8..512` UTF-16 code units) verifying constant-time
//      absorbing-accept short-circuiting on long non-Vietnamese tokens.
//   5. End-to-end `InputMethodBuffer` keystroke + word-boundary auto-restore
//      streams on both mangled English words and valid Vietnamese prose.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "benchmark/benchmark.h"
#include "input-method-buffer.h"

namespace okeytre {
namespace {

void BM_VietnameseInvAutomaton_ValidSyllables(benchmark::State& state) {
  constexpr std::u16string_view kValidWords[8] = {
      u"nghi\u1EBFng",  // nghiếng (3-letter onset + diphthong + 2-letter coda)
      u"thuy\u1EC1n",   // thuyền  (triphthong + coda)
      u"gi\u01B0\u1EDDng",  // giường  (gi onset + diphthong + 2-letter coda)
      u"khu\u1EBFch",       // khuếch  (labialized vowel + ch coda)
      u"ngo\u1EB1n",        // ngoằn   (oă diphthong + coda)
      u"khu\u1EF7u",        // khuỷu   (uyu triphthong)
      u"r\u01B0\u1EE3u",    // rượu    (ươu triphthong)
      u"Vi\u1EC7t",         // Việt    (capitalized initial)
  };

  size_t idx = 0;
  int64_t total_bytes = 0;
  while (state.KeepRunning()) {
    const std::u16string_view word = kValidWords[idx];
    idx = (idx + 1 == std::size(kValidWords)) ? 0 : (idx + 1);
    bool matched = VietnameseInvAutomaton::Matches(
        reinterpret_cast<const uint16_t*>(word.data()),
        static_cast<int32_t>(word.size()));
    assert(!matched);
    benchmark::DoNotOptimize(matched);
    total_bytes += static_cast<int64_t>(word.size() * sizeof(uint16_t));
  }
  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(total_bytes);
}
BENCHMARK(BM_VietnameseInvAutomaton_ValidSyllables);

void BM_VietnameseInvAutomaton_EarlyAbsorbingReject(benchmark::State& state) {
  constexpr std::u16string_view kMangledWords[8] = {
      u"utf8",         // non-Vietnamese 'f' and digit '8'
      u"b\u00E8ore",   // "before" typed in Telex
      u"r\u00E9tore",  // "restore" typed in Telex
      u"s\u1ECFuce",   // "source" typed in Telex
      u"vi\u00E9w",    // "views" / "view" with 'w'
      u"r\u1ED3top",   // "rooftop" typed in Telex
      u"h\u00F3ue",    // "house" typed in Telex
      u"c\u00E1e",     // "case" typed in Telex
  };

  size_t idx = 0;
  int64_t total_bytes = 0;
  while (state.KeepRunning()) {
    const std::u16string_view word = kMangledWords[idx];
    idx = (idx + 1 == std::size(kMangledWords)) ? 0 : (idx + 1);
    bool matched = VietnameseInvAutomaton::Matches(
        reinterpret_cast<const uint16_t*>(word.data()),
        static_cast<int32_t>(word.size()));
    assert(matched);
    benchmark::DoNotOptimize(matched);
    total_bytes += static_cast<int64_t>(word.size() * sizeof(uint16_t));
  }
  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(total_bytes);
}
BENCHMARK(BM_VietnameseInvAutomaton_EarlyAbsorbingReject);

void BM_VietnameseInvAutomaton_EndAnchoredReject(benchmark::State& state) {
  constexpr std::u16string_view kEndAnchoredInvalid[8] = {
      u"m\u00EAt",     // mêt    (plain vowel before stop coda 't')
      u"t\u1EC5t",     // tẽt    (ngã tone before stop coda 't')
      u"nghi\u00EAt",  // nghiêt (plain diphthong before stop coda 't')
      u"khu\u00EAch",  // khuêch (plain vowel before stop coda 'ch')
      u"t\u01A1ng",    // tơng   (single 'ơ' before velar 'ng')
      u"t\u00EDng",    // tíng   ('i' before velar 'ng')
      u"l\u0103",      // lă     (open 'ă' after onset)
      u"nghi\u00EA",   // nghiê  (open 'iê' after onset)
  };

  size_t idx = 0;
  int64_t total_bytes = 0;
  while (state.KeepRunning()) {
    const std::u16string_view word = kEndAnchoredInvalid[idx];
    idx = (idx + 1 == std::size(kEndAnchoredInvalid)) ? 0 : (idx + 1);
    bool matched = VietnameseInvAutomaton::Matches(
        reinterpret_cast<const uint16_t*>(word.data()),
        static_cast<int32_t>(word.size()));
    assert(matched);
    benchmark::DoNotOptimize(matched);
    total_bytes += static_cast<int64_t>(word.size() * sizeof(uint16_t));
  }
  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(total_bytes);
}
BENCHMARK(BM_VietnameseInvAutomaton_EndAnchoredReject);

void BM_VietnameseInvAutomaton_LongTokenScaling(benchmark::State& state) {
  const int32_t token_len = static_cast<int32_t>(state.range(0));
  // Prefix "nghiếng" (7 valid Vietnamese code units) followed by concatenated
  // letters, triggering multi-syllable / consonant-cluster absorbing rejection
  // within the first ~8 code units regardless of total token length.
  constexpr std::u16string_view kPrefix = u"nghi\u1EBFng";
  std::vector<uint16_t> token;
  token.reserve(static_cast<size_t>(token_len));
  for (int32_t i = 0; i < token_len; ++i) {
    if (static_cast<size_t>(i) < kPrefix.size()) {
      token.push_back(static_cast<uint16_t>(kPrefix[static_cast<size_t>(i)]));
    } else {
      token.push_back((i % 2 == 0) ? 'a' : 'n');
    }
  }

  while (state.KeepRunning()) {
    bool matched = VietnameseInvAutomaton::Matches(token.data(), token_len);
    assert(matched);
    benchmark::DoNotOptimize(matched);
  }
  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(token_len * sizeof(uint16_t)));
}
BENCHMARK(BM_VietnameseInvAutomaton_LongTokenScaling)
    ->Arg(8)
    ->Arg(32)
    ->Arg(128)
    ->Arg(InputMethodBuffer::kMaxAllowedBufferSize);

void BM_InputMethodBuffer_BoundaryAutoRestoreEnglishStream(
    benchmark::State& state) {
  constexpr std::u16string_view kEnglishStream =
      u"restore before views source user rooftop ";
  constexpr benchmark::IterationCount kBatchSize =
      static_cast<benchmark::IterationCount>(kEnglishStream.size());

  InputMethodBuffer ime(InputMethodStyle::kTelex, /*modern_tone=*/true,
                        /*uo_horn=*/true, /*ng_to_ngh=*/true,
                        /*ngh_to_ng=*/true,
                        InputMethodBuffer::kMaxAllowedBufferSize,
                        /*restore_invalid_words=*/true);

  while (state.KeepRunningBatch(kBatchSize)) {
    ime.Reset();
    for (char16_t raw_ch : kEnglishStream) {
      const uint16_t ch = static_cast<uint16_t>(raw_ch);
      TextEditAction action = ime.HandleInput(&ch, 1);
      benchmark::DoNotOptimize(action);
    }
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_InputMethodBuffer_BoundaryAutoRestoreEnglishStream);

void BM_InputMethodBuffer_BoundaryValidVietnameseStream(
    benchmark::State& state) {
  constexpr std::u16string_view kVietnameseStream =
      u"ngieengs thuyeefn giuowngf khueesch ngoawnf ";
  constexpr benchmark::IterationCount kBatchSize =
      static_cast<benchmark::IterationCount>(kVietnameseStream.size());

  InputMethodBuffer ime(InputMethodStyle::kTelex, /*modern_tone=*/true,
                        /*uo_horn=*/true, /*ng_to_ngh=*/true,
                        /*ngh_to_ng=*/true,
                        InputMethodBuffer::kMaxAllowedBufferSize,
                        /*restore_invalid_words=*/true);

  while (state.KeepRunningBatch(kBatchSize)) {
    ime.Reset();
    for (char16_t raw_ch : kVietnameseStream) {
      const uint16_t ch = static_cast<uint16_t>(raw_ch);
      TextEditAction action = ime.HandleInput(&ch, 1);
      benchmark::DoNotOptimize(action);
    }
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_InputMethodBuffer_BoundaryValidVietnameseStream);

}  // namespace
}  // namespace okeytre
