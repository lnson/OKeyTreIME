// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

#include "input-method-buffer.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace okeytre {

namespace {

constexpr bool IsUnsupportedDirection(uint16_t c) noexcept {
  switch (c) {
    case kUp:
    case kUpCtrl:
    case kDown:
    case kDownCtrl:
    case kHome:
    case kEnd:
    case kPageUp:
    case kPageDown:
    case kTab:
    case kBackTab:
    case kEscape:
      return true;
    default:
      return false;
  }
}

constexpr bool IsNonPrintableOrFunctionKey(uint16_t c) noexcept {
  return c < 0x0020 || (c >= 0xF700 && c <= 0xF8FF);
}

int32_t LongestCommonPrefixLength(const uint16_t* a, int32_t a_len,
                                  const uint16_t* b, int32_t b_len) noexcept {
  int32_t lcp = 0;
  const int32_t limit = std::min(a_len, b_len);
  while (lcp < limit && a[lcp] == b[lcp]) {
    ++lcp;
  }
  return lcp;
}

bool HasVietnameseDiacritic(const uint16_t* word, int32_t word_len) noexcept {
  for (int32_t i = 0; i < word_len; ++i) {
    if (word[i] >= 0x0080) {
      return true;
    }
  }
  return false;
}

}  // namespace

int32_t InputMethodBuffer::ClampBufferSize(int32_t size) noexcept {
  return std::clamp(size, kMinBufferSize, kMaxAllowedBufferSize);
}

InputMethodBuffer::AutomatonVariant InputMethodBuffer::MakeAutomaton(
    InputMethodStyle style, bool modern_tone, bool uo_horn, bool ng_to_ngh,
    bool ngh_to_ng) noexcept {
  switch (style) {
    case InputMethodStyle::kTelex: {
      TelexAutomaton telex;
      telex.modernTone = modern_tone;
      telex.uoHorn = uo_horn;
      telex.ngToNgh = ng_to_ngh;
      telex.nghToNg = ngh_to_ng;
      return telex;
    }
    case InputMethodStyle::kVNI: {
      VNIAutomaton vni;
      vni.modernTone = modern_tone;
      vni.uoHorn = uo_horn;
      vni.ngToNgh = ng_to_ngh;
      vni.nghToNg = ngh_to_ng;
      return vni;
    }
    case InputMethodStyle::kVIQR: {
      VIQRAutomaton viqr;
      viqr.modernTone = modern_tone;
      viqr.uoHorn = uo_horn;
      viqr.ngToNgh = ng_to_ngh;
      viqr.nghToNg = ngh_to_ng;
      return viqr;
    }
  }
  return TelexAutomaton{};
}

InputMethodBuffer::InputMethodBuffer(InputMethodStyle style, bool modern_tone,
                                     bool uo_horn, bool ng_to_ngh,
                                     bool ngh_to_ng, int32_t max_buffer_size,
                                     bool restore_invalid_words)
    : max_buffer_size_(ClampBufferSize(max_buffer_size)),
      automaton_(
          MakeAutomaton(style, modern_tone, uo_horn, ng_to_ngh, ngh_to_ng)),
      restore_invalid_words_(restore_invalid_words) {
  characters_.reserve(static_cast<size_t>(kMaxAllowedBufferSize + 16));
  word_history_.reserve(16);
}

void InputMethodBuffer::Reset() noexcept { ClearBuffer(true); }

InputMethodStyle InputMethodBuffer::style() const noexcept {
  if (std::holds_alternative<VNIAutomaton>(automaton_)) {
    return InputMethodStyle::kVNI;
  }
  if (std::holds_alternative<VIQRAutomaton>(automaton_)) {
    return InputMethodStyle::kVIQR;
  }
  return InputMethodStyle::kTelex;
}

void InputMethodBuffer::SetStyle(InputMethodStyle new_style) {
  if (style() == new_style) {
    return;
  }
  automaton_ = MakeAutomaton(new_style, modern_tone(), uo_horn(), ng_to_ngh(),
                             ngh_to_ng());
  Reset();
}

bool InputMethodBuffer::modern_tone() const noexcept {
  return std::visit(
      []<typename Automaton>(const Automaton& automaton) {
        return automaton.modernTone;
      },
      automaton_);
}

void InputMethodBuffer::SetModernTone(bool enabled) {
  std::visit([enabled]<typename Automaton>(
                 Automaton& automaton) { automaton.modernTone = enabled; },
             automaton_);
}

bool InputMethodBuffer::uo_horn() const noexcept {
  return std::visit(
      []<typename Automaton>(const Automaton& automaton) {
        return automaton.uoHorn;
      },
      automaton_);
}

void InputMethodBuffer::SetUoHorn(bool enabled) {
  std::visit([enabled]<typename Automaton>(
                 Automaton& automaton) { automaton.uoHorn = enabled; },
             automaton_);
}

bool InputMethodBuffer::ng_to_ngh() const noexcept {
  return std::visit(
      []<typename Automaton>(const Automaton& automaton) {
        return automaton.ngToNgh;
      },
      automaton_);
}

void InputMethodBuffer::SetNgToNgh(bool enabled) {
  std::visit([enabled]<typename Automaton>(
                 Automaton& automaton) { automaton.ngToNgh = enabled; },
             automaton_);
}

bool InputMethodBuffer::ngh_to_ng() const noexcept {
  return std::visit(
      []<typename Automaton>(const Automaton& automaton) {
        return automaton.nghToNg;
      },
      automaton_);
}

void InputMethodBuffer::SetNghToNg(bool enabled) {
  std::visit([enabled]<typename Automaton>(
                 Automaton& automaton) { automaton.nghToNg = enabled; },
             automaton_);
}

void InputMethodBuffer::SetMaxBufferSize(int32_t size) {
  max_buffer_size_ = ClampBufferSize(size);
  TrimBufferToMaxSize();
}

bool InputMethodBuffer::ConsumeCaretReadRequest() noexcept {
  if (!should_read_at_caret_) {
    return false;
  }
  should_read_at_caret_ = false;
  return true;
}

void InputMethodBuffer::SeedWithSurroundingText(const uint16_t* word,
                                                int32_t word_len,
                                                int32_t caret_pos) {
  should_read_at_caret_ = false;
  const int32_t safe_len = (word != nullptr && word_len > 0) ? word_len : 0;
  if (safe_len > 0) {
    characters_.assign(word, word + safe_len);
  } else {
    characters_.clear();
  }
  cursor_pos_ = std::clamp(caret_pos, 0, safe_len);
  word_history_.clear();
  const int32_t word_start = CurrentWordStartIndex();
  if (cursor_pos_ > word_start) {
    WordRawHistory entry;
    entry.raw_keystrokes.assign(characters_.begin() + word_start,
                                characters_.begin() + cursor_pos_);
    entry.word_start = word_start;
    word_history_.push_back(std::move(entry));
  }
}

void InputMethodBuffer::ClearBuffer(bool reset_should_read_at_caret) noexcept {
  characters_.clear();
  word_history_.clear();
  cursor_pos_ = 0;
  should_read_at_caret_ = reset_should_read_at_caret;
}

void InputMethodBuffer::ClearRawWordHistory() noexcept {
  word_history_.clear();
}

InputMethodBuffer::WordRawHistory* InputMethodBuffer::FindWordHistory(
    int32_t word_start) noexcept {
  for (size_t i = word_history_.size(); i > 0; --i) {
    if (word_history_[i - 1].word_start == word_start) {
      return &word_history_[i - 1];
    }
    if (word_history_[i - 1].word_start < word_start) {
      break;
    }
  }
  return nullptr;
}

const InputMethodBuffer::WordRawHistory* InputMethodBuffer::FindWordHistory(
    int32_t word_start) const noexcept {
  for (size_t i = word_history_.size(); i > 0; --i) {
    if (word_history_[i - 1].word_start == word_start) {
      return &word_history_[i - 1];
    }
    if (word_history_[i - 1].word_start < word_start) {
      break;
    }
  }
  return nullptr;
}

InputMethodBuffer::WordRawHistory& InputMethodBuffer::EnsureWordHistory(
    int32_t word_start) {
  WordRawHistory* existing = FindWordHistory(word_start);
  if (existing != nullptr) {
    return *existing;
  }

  WordRawHistory entry;
  entry.raw_keystrokes.reserve(16);
  if (cursor_pos_ > word_start) {
    entry.raw_keystrokes.assign(characters_.begin() + word_start,
                                characters_.begin() + cursor_pos_);
  }
  entry.word_start = word_start;
  word_history_.push_back(std::move(entry));
  return word_history_.back();
}

void InputMethodBuffer::TrimBufferToMaxSize() noexcept {
  if (cursor_pos_ <= max_buffer_size_) {
    return;
  }
  const int32_t overflow = cursor_pos_ - max_buffer_size_;
  characters_.erase(characters_.begin(), characters_.begin() + overflow);
  cursor_pos_ -= overflow;

  size_t keep_from = 0;
  for (size_t i = 0; i < word_history_.size(); ++i) {
    word_history_[i].word_start -= overflow;
    if (word_history_[i].word_start < 0) {
      keep_from = i + 1;
    }
  }
  if (keep_from > 0) {
    word_history_.erase(
        word_history_.begin(),
        word_history_.begin() + static_cast<ptrdiff_t>(keep_from));
  }
}

void InputMethodBuffer::InsertChar(uint16_t c) {
  for (WordRawHistory& entry : word_history_) {
    if (entry.word_start > cursor_pos_) {
      ++entry.word_start;
    }
  }
  characters_.insert(characters_.begin() + cursor_pos_, c);
  ++cursor_pos_;
  TrimBufferToMaxSize();
}

void InputMethodBuffer::MoveCursorLeft() noexcept {
  ClearRawWordHistory();
  if (cursor_pos_ == 0) {
    ClearBuffer(true);
  } else {
    --cursor_pos_;
  }
}

void InputMethodBuffer::MoveCursorRight() noexcept {
  ClearRawWordHistory();
  if (cursor_pos_ == static_cast<int32_t>(characters_.size())) {
    ClearBuffer(true);
  } else {
    ++cursor_pos_;
  }
}

void InputMethodBuffer::BackSpace() noexcept {
  if (cursor_pos_ <= 0) {
    ClearBuffer(true);
    return;
  }

  --cursor_pos_;
  const uint16_t deleted_ch = characters_[static_cast<size_t>(cursor_pos_)];
  characters_.erase(characters_.begin() + cursor_pos_);

  while (!word_history_.empty() &&
         word_history_.back().word_start > cursor_pos_) {
    word_history_.pop_back();
  }

  if (IsWordBoundary(deleted_ch)) {
    return;
  }

  const int32_t word_start = CurrentWordStartIndex();
  const int32_t remaining_len = cursor_pos_ - word_start;
  if (remaining_len <= 0) {
    while (!word_history_.empty() &&
           word_history_.back().word_start >= word_start) {
      word_history_.pop_back();
    }
    return;
  }

  WordRawHistory* history = FindWordHistory(word_start);
  if (history == nullptr || history->raw_keystrokes.empty()) {
    return;
  }
  if (history->is_restored) {
    history->raw_keystrokes.pop_back();
    return;
  }

  const int32_t raw_len = static_cast<int32_t>(history->raw_keystrokes.size());
  const int32_t lcp =
      LongestCommonPrefixLength(characters_.data() + word_start, remaining_len,
                                history->raw_keystrokes.data(), raw_len);
  for (int32_t i = raw_len - 1; i >= lcp; --i) {
    if (history->raw_keystrokes[static_cast<size_t>(i)] == deleted_ch) {
      history->raw_keystrokes.erase(history->raw_keystrokes.begin() + i);
      return;
    }
  }
  if (lcp == remaining_len) {
    history->raw_keystrokes.resize(static_cast<size_t>(lcp));
  } else if (remaining_len == 1 &&
             (characters_[static_cast<size_t>(word_start)] == 0x0111 ||
              characters_[static_cast<size_t>(word_start)] == 0x0110) &&
             history->raw_keystrokes.size() >= 2) {
    history->raw_keystrokes.resize(2);
  } else {
    history->raw_keystrokes.pop_back();
  }
}

void InputMethodBuffer::ForwardDelete() noexcept {
  ClearRawWordHistory();
  if (cursor_pos_ < static_cast<int32_t>(characters_.size())) {
    characters_.erase(characters_.begin() + cursor_pos_);
  } else {
    ClearBuffer(true);
  }
}

int32_t InputMethodBuffer::CurrentWordStartIndex() const noexcept {
  int32_t start = cursor_pos_;
  while (start > 0) {
    if (IsWordBoundary(characters_[static_cast<size_t>(start - 1)])) {
      break;
    }
    --start;
  }
  return start;
}

int32_t InputMethodBuffer::RewriteWord(const uint16_t* word, int32_t word_len,
                                       uint16_t key, uint16_t* out_buf,
                                       int32_t out_capacity) const noexcept {
  return std::visit(
      [word, word_len, key, out_buf,
       out_capacity]<typename Automaton>(const Automaton& automaton) {
        return automaton.RewriteToBuffer(word, word_len, key, out_buf,
                                         out_capacity);
      },
      automaton_);
}

TextEditAction InputMethodBuffer::ApplyWordSuffixReplacement(
    int32_t word_start, const uint16_t* replacement, int32_t replacement_len,
    int32_t lcp) {
  const int32_t old_count = cursor_pos_ - word_start;
  const int32_t backspace_count = old_count - lcp;
  const int32_t emitted_len = replacement_len - lcp;
  const int32_t suffix_start = word_start + lcp;
  const int32_t common_suffix = std::min(backspace_count, emitted_len);

  std::copy_n(replacement + lcp, common_suffix,
              characters_.begin() + suffix_start);
  if (emitted_len < backspace_count) {
    characters_.erase(characters_.begin() + suffix_start + common_suffix,
                      characters_.begin() + cursor_pos_);
  } else if (emitted_len > backspace_count) {
    characters_.insert(characters_.begin() + cursor_pos_,
                       replacement + lcp + common_suffix,
                       replacement + replacement_len);
  }
  const int32_t delta = replacement_len - old_count;
  if (delta != 0) {
    for (WordRawHistory& entry : word_history_) {
      if (entry.word_start > word_start) {
        entry.word_start += delta;
      }
    }
  }
  cursor_pos_ = word_start + replacement_len;

  return TextEditAction{
      .text_to_insert =
          std::u16string(replacement + lcp, replacement + replacement_len),
      .backspace_count = backspace_count,
  };
}

TextEditAction InputMethodBuffer::RestoreCurrentWord() {
  const int32_t word_start = CurrentWordStartIndex();
  const int32_t old_count = cursor_pos_ - word_start;
  WordRawHistory* history = FindWordHistory(word_start);
  if (old_count <= 0 || history == nullptr || history->raw_keystrokes.empty()) {
    return {};
  }

  history->is_restored = true;

  const int32_t raw_count =
      static_cast<int32_t>(history->raw_keystrokes.size());
  const int32_t lcp =
      LongestCommonPrefixLength(characters_.data() + word_start, old_count,
                                history->raw_keystrokes.data(), raw_count);
  if (lcp == old_count && lcp == raw_count) {
    return {};
  }

  TextEditAction action = ApplyWordSuffixReplacement(
      word_start, history->raw_keystrokes.data(), raw_count, lcp);
  TrimBufferToMaxSize();
  return action;
}

TextEditAction InputMethodBuffer::StepAutomaton(uint16_t key) {
  const int32_t word_start = CurrentWordStartIndex();
  const int32_t old_count = cursor_pos_ - word_start;
  const uint16_t* word_ptr =
      old_count > 0 ? characters_.data() + word_start : nullptr;

  uint16_t stack_buf[576];
  std::vector<uint16_t> heap_buf;
  uint16_t* rewritten_ptr = stack_buf;
  int32_t rewritten_len =
      RewriteWord(word_ptr, old_count, key, stack_buf,
                  static_cast<int32_t>(std::size(stack_buf)));
  if (rewritten_len < 0) {
    return {};
  }
  if (rewritten_len > static_cast<int32_t>(std::size(stack_buf))) {
    heap_buf.resize(static_cast<size_t>(rewritten_len));
    rewritten_len =
        RewriteWord(word_ptr, old_count, key, heap_buf.data(), rewritten_len);
    if (rewritten_len < 0) {
      return {};
    }
    rewritten_ptr = heap_buf.data();
  }

  const int32_t lcp = LongestCommonPrefixLength(word_ptr, old_count,
                                                rewritten_ptr, rewritten_len);
  const int32_t backspace_count = old_count - lcp;
  const int32_t emitted_len = rewritten_len - lcp;
  if (backspace_count == 0 &&
      (emitted_len == 0 || (emitted_len == 1 && rewritten_ptr[lcp] == key))) {
    return {};
  }

  return ApplyWordSuffixReplacement(word_start, rewritten_ptr, rewritten_len,
                                    lcp);
}

TextEditAction InputMethodBuffer::MaybeRestoreInvalidWordOnBoundary(
    int32_t word_start, uint16_t boundary_ch) {
  const int32_t old_count = cursor_pos_ - word_start;
  WordRawHistory* history = FindWordHistory(word_start);
  if (old_count <= 0 || history == nullptr || history->raw_keystrokes.empty()) {
    return {};
  }

  const uint16_t* current_word = characters_.data() + word_start;
  const int32_t raw_count =
      static_cast<int32_t>(history->raw_keystrokes.size());
  const int32_t lcp = LongestCommonPrefixLength(
      current_word, old_count, history->raw_keystrokes.data(), raw_count);
  if (lcp == old_count && lcp == raw_count) {
    return {};
  }

  // If the displayed word contains no Vietnamese diacritics and is no longer
  // than the raw keystroke sequence, the user already escaped Telex/VNI
  // modifiers manually (e.g. "tesst" -> "test", "towwn" -> "town", "boook" ->
  // "book"). Keep their escaped ASCII word intact.
  if (!HasVietnameseDiacritic(current_word, old_count) &&
      raw_count >= old_count) {
    return {};
  }

  if (!VietnameseInvAutomaton::Matches(current_word, old_count)) {
    return {};
  }

  history->is_restored = true;
  TextEditAction action = ApplyWordSuffixReplacement(
      word_start, history->raw_keystrokes.data(), raw_count, lcp);
  InsertChar(boundary_ch);
  action.text_to_insert.push_back(static_cast<char16_t>(boundary_ch));
  return action;
}

TextEditAction InputMethodBuffer::HandleInput(const uint16_t* input,
                                              int32_t input_len,
                                              uint32_t modifiers) {
  if ((modifiers & kDisruptingModifiers) != 0) {
    Reset();
    return {};
  }

  should_read_at_caret_ = false;

  if (input == nullptr || input_len <= 0) {
    return {};
  }

  if (input_len == 2 && input[0] == kCR && input[1] == kLF) {
    ClearBuffer(false);
    return {};
  }

  if (input_len > 1) {
    for (int32_t i = 0; i < input_len; ++i) {
      const uint16_t ch = input[i];
      if (!IsWordBoundary(ch)) {
        const int32_t word_start = CurrentWordStartIndex();
        EnsureWordHistory(word_start).raw_keystrokes.push_back(ch);
      }
      InsertChar(ch);
    }
    return {};
  }

  const uint16_t ch = input[0];
  switch (ch) {
    case kLeft:
    case kLeftCtrl:
      if ((modifiers & kModShift) != 0) {
        Reset();
      } else {
        MoveCursorLeft();
      }
      return {};

    case kRight:
    case kRightCtrl:
      if ((modifiers & kModShift) != 0) {
        Reset();
      } else {
        MoveCursorRight();
      }
      return {};

    case kBackspace:
    case kBackspaceCtrl:
      BackSpace();
      return {};

    case kForwardDelete:
      ForwardDelete();
      return {};

    case kCR:
    case kLF:
      ClearBuffer(false);
      return {};

    default: {
      if (IsUnsupportedDirection(ch) || IsNonPrintableOrFunctionKey(ch)) {
        ClearBuffer(true);
        return {};
      }

      const int32_t word_start = CurrentWordStartIndex();
      WordRawHistory* current_history = FindWordHistory(word_start);
      if (current_history == nullptr && cursor_pos_ > word_start) {
        current_history = &EnsureWordHistory(word_start);
      }
      const bool is_restored =
          (current_history != nullptr && current_history->is_restored);

      if (!is_restored) {
        TextEditAction action = StepAutomaton(ch);
        if (!action.IsNoOp()) {
          EnsureWordHistory(word_start).raw_keystrokes.push_back(ch);
          TrimBufferToMaxSize();
          return action;
        }
      }

      if (IsWordBoundary(ch)) {
        if (restore_invalid_words_ && !is_restored &&
            cursor_pos_ > word_start &&
            (cursor_pos_ == static_cast<int32_t>(characters_.size()) ||
             IsWordBoundary(characters_[static_cast<size_t>(cursor_pos_)]))) {
          TextEditAction restore_action =
              MaybeRestoreInvalidWordOnBoundary(word_start, ch);
          if (!restore_action.IsNoOp()) {
            return restore_action;
          }
        }
        InsertChar(ch);
        return {};
      }

      EnsureWordHistory(word_start).raw_keystrokes.push_back(ch);
      InsertChar(ch);
      return {};
    }
  }
}

}  // namespace okeytre
