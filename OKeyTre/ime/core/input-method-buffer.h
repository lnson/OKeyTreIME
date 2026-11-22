// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Stateful C++ IME keystroke buffer, cursor tracker, raw word restorer, and
// Longest-Common-Prefix (LCP) action synthesizer.

#ifndef OKEYTRE_ENGINE_INPUTMETHODBUFFER_HPP_
#define OKEYTRE_ENGINE_INPUTMETHODBUFFER_HPP_

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "telex-automaton.generated.h"
#include "unicode-utils.h"
#include "viqr-automaton.generated.h"
#include "vni-automaton.generated.h"

namespace okeytre {

enum class InputMethodStyle : int32_t {
  kTelex = 0,
  kVNI = 1,
  kVIQR = 2,
};

// Navigation, editing & control Unicode code units recognized by `HandleInput`.
enum UnicodeKey : uint16_t {
  kBackspaceCtrl = 0x0008,
  kTab = 0x0009,
  kLF = 0x000A,
  kCR = 0x000D,
  kBackTab = 0x0019,
  kEscape = 0x001B,
  kLeftCtrl = 0x001C,
  kRightCtrl = 0x001D,
  kUpCtrl = 0x001E,
  kDownCtrl = 0x001F,
  kBackspace = 0x007F,
  kUp = 0xF700,
  kDown = 0xF701,
  kLeft = 0xF702,
  kRight = 0xF703,
  kForwardDelete = 0xF728,
  kHome = 0xF729,
  kEnd = 0xF72B,
  kPageUp = 0xF72C,
  kPageDown = 0xF72D,
};

// Portable modifier bitmask flags
enum Modifier : uint32_t {
  kModNone = 0u,
  kModShift = 1u << 0,
  kModControl = 1u << 1,
  kModOption = 1u << 2,
  kModCommand = 1u << 3,
  kDisruptingModifiers = kModCommand | kModControl | kModOption,
};

// Edit command returned to the host frontend (e.g. `CGEventTap`) when a
// keystroke or dual-Shift modifier event rewrites the active word before the
// caret. When `IsNoOp()` is true (`backspace_count == 0` and
// `text_to_insert.empty()`), the frontend should pass the original key event
// through untouched.
struct TextEditAction {
  // Replacement UTF-16 code units to insert after the Backspace presses.
  std::u16string text_to_insert;
  // Number of UTF-16 code units before the caret to delete via Backspace
  // (computed via Longest Common Prefix against the existing word).
  int32_t backspace_count = 0;

  bool IsNoOp() const noexcept {
    return backspace_count == 0 && text_to_insert.empty();
  }
};

// Stateful IME session managing the active word/context buffer and dispatching
// keystrokes into the ahead-of-time generated automata (`TelexAutomaton`,
// `VNIAutomaton`, `VIQRAutomaton`).
class InputMethodBuffer {
 public:
  static constexpr int32_t kDefaultMaxBufferSize = 16;
  static constexpr int32_t kMinBufferSize = 10;
  static constexpr int32_t kMaxAllowedBufferSize = 512;

  explicit InputMethodBuffer(InputMethodStyle style = InputMethodStyle::kTelex,
                             bool modern_tone = false, bool uo_horn = false,
                             bool ng_to_ngh = false, bool ngh_to_ng = false,
                             int32_t max_buffer_size = kDefaultMaxBufferSize);

  void Reset() noexcept;

  InputMethodStyle style() const noexcept;
  void SetStyle(InputMethodStyle new_style);

  bool modern_tone() const noexcept;
  void SetModernTone(bool enabled);

  bool uo_horn() const noexcept;
  void SetUoHorn(bool enabled);

  bool ng_to_ngh() const noexcept;
  void SetNgToNgh(bool enabled);

  bool ngh_to_ng() const noexcept;
  void SetNghToNg(bool enabled);

  int32_t max_buffer_size() const noexcept { return max_buffer_size_; }
  void SetMaxBufferSize(int32_t size);

  // Returns true once after creation or `Reset()` so the host OS layer can
  // optionally query Accessibility surrounding text before feeding a key.
  bool ConsumeCaretReadRequest() noexcept;

  // Populates the buffer and caret offset from host Accessibility text.
  void SeedWithSurroundingText(const uint16_t* word, int32_t word_len,
                               int32_t caret_pos);

  // Restores the current word before the caret back to its raw typed
  // keystrokes and suppresses further transforms until the next word boundary.
  TextEditAction RestoreCurrentWord();

  // Feeds typed UTF-16 code units and modifier flags into the buffer,
  // returning a minimal backspace + replacement `TextEditAction` if rewritten.
  TextEditAction HandleInput(const uint16_t* input, int32_t input_len,
                             uint32_t modifiers = kModNone);

 private:
  using AutomatonVariant =
      std::variant<TelexAutomaton, VNIAutomaton, VIQRAutomaton>;

  // Raw keystroke history and restore state for a single word starting at
  // `word_start` within `characters_`. Retained across word boundaries until
  // trimmed out of the `max_buffer_size_` sliding window.
  struct WordRawHistory {
    std::vector<uint16_t> raw_keystrokes;
    int32_t word_start = 0;
    bool is_restored = false;
  };

  static int32_t ClampBufferSize(int32_t size) noexcept;
  static AutomatonVariant MakeAutomaton(InputMethodStyle style,
                                        bool modern_tone, bool uo_horn,
                                        bool ng_to_ngh,
                                        bool ngh_to_ng) noexcept;
  void ClearBuffer(bool reset_should_read_at_caret) noexcept;
  void ClearRawWordHistory() noexcept;
  WordRawHistory* FindWordHistory(int32_t word_start) noexcept;
  const WordRawHistory* FindWordHistory(int32_t word_start) const noexcept;
  WordRawHistory& EnsureWordHistory(int32_t word_start);
  void TrimBufferToMaxSize() noexcept;
  void InsertChar(uint16_t c);
  void MoveCursorLeft() noexcept;
  void MoveCursorRight() noexcept;
  void BackSpace() noexcept;
  void ForwardDelete() noexcept;
  int32_t CurrentWordStartIndex() const noexcept;
  int32_t RewriteWord(const uint16_t* word, int32_t word_len, uint16_t key,
                      uint16_t* out_buf, int32_t out_capacity) const noexcept;
  TextEditAction ApplyWordSuffixReplacement(int32_t word_start,
                                            const uint16_t* replacement,
                                            int32_t replacement_len,
                                            int32_t lcp);
  TextEditAction StepAutomaton(uint16_t key);

  std::vector<uint16_t> characters_;
  std::vector<WordRawHistory> word_history_;
  int32_t cursor_pos_ = 0;
  int32_t max_buffer_size_ = kDefaultMaxBufferSize;
  AutomatonVariant automaton_;
  bool should_read_at_caret_ = true;
};

}  // namespace okeytre

#endif  // OKEYTRE_ENGINE_INPUTMETHODBUFFER_HPP_
