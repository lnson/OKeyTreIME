// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// C-ABI wrapper around the stateful C++ IME buffer and AOT automata.

#include "okeytre-engine.h"

#include <algorithm>
#include <cstring>

#include "input-method-buffer.h"
#include "unicode-utils.h"

struct OKTInputMethodBuffer {
  okeytre::InputMethodBuffer impl;
};

namespace {

okeytre::InputMethodStyle ToCppStyle(OKTInputMethodStyle style) noexcept {
  switch (style) {
    case OKT_STYLE_TELEX:
      return okeytre::InputMethodStyle::kTelex;
    case OKT_STYLE_VNI:
      return okeytre::InputMethodStyle::kVNI;
    case OKT_STYLE_VIQR:
      return okeytre::InputMethodStyle::kVIQR;
  }
  return okeytre::InputMethodStyle::kTelex;
}

OKTInputMethodStyle ToCStyle(okeytre::InputMethodStyle style) noexcept {
  switch (style) {
    case okeytre::InputMethodStyle::kTelex:
      return OKT_STYLE_TELEX;
    case okeytre::InputMethodStyle::kVNI:
      return OKT_STYLE_VNI;
    case okeytre::InputMethodStyle::kVIQR:
      return OKT_STYLE_VIQR;
  }
  return OKT_STYLE_TELEX;
}

bool WriteTextEditAction(const okeytre::TextEditAction& action,
                         int32_t* out_backspace_count, uint16_t* out_text,
                         int32_t out_text_capacity, int32_t* out_text_len) {
  if (action.IsNoOp()) {
    return false;
  }
  if (out_backspace_count != nullptr) {
    *out_backspace_count = action.backspace_count;
  }
  const int32_t len = static_cast<int32_t>(action.text_to_insert.size());
  if (out_text_len != nullptr) {
    *out_text_len = len;
  }
  if (out_text != nullptr && out_text_capacity > 0 && len > 0) {
    const int32_t to_copy = std::min(len, out_text_capacity);
    std::memcpy(out_text, action.text_to_insert.data(),
                static_cast<size_t>(to_copy) * sizeof(uint16_t));
  }
  return true;
}

}  // namespace

extern "C" {

const int32_t OKT_BUFFER_DEFAULT_MAX_SIZE =
    okeytre::InputMethodBuffer::kDefaultMaxBufferSize;
const int32_t OKT_BUFFER_MIN_SIZE = okeytre::InputMethodBuffer::kMinBufferSize;
const int32_t OKT_BUFFER_MAX_SIZE =
    okeytre::InputMethodBuffer::kMaxAllowedBufferSize;

bool okeytre_is_word_boundary(uint16_t c) { return okeytre::IsWordBoundary(c); }

OKTInputMethodBuffer* okeytre_buffer_create(OKTInputMethodStyle style,
                                            bool modern_tone, bool uo_horn,
                                            bool ng_to_ngh, bool ngh_to_ng,
                                            int32_t max_buffer_size) {
  return new OKTInputMethodBuffer{
      okeytre::InputMethodBuffer(ToCppStyle(style), modern_tone, uo_horn,
                                 ng_to_ngh, ngh_to_ng, max_buffer_size)};
}

void okeytre_buffer_destroy(OKTInputMethodBuffer* buffer) { delete buffer; }

void okeytre_buffer_reset(OKTInputMethodBuffer* buffer) {
  if (buffer != nullptr) {
    buffer->impl.Reset();
  }
}

OKTInputMethodStyle okeytre_buffer_get_style(
    const OKTInputMethodBuffer* buffer) {
  return buffer != nullptr ? ToCStyle(buffer->impl.style()) : OKT_STYLE_TELEX;
}

void okeytre_buffer_set_style(OKTInputMethodBuffer* buffer,
                              OKTInputMethodStyle style) {
  if (buffer != nullptr) {
    buffer->impl.SetStyle(ToCppStyle(style));
  }
}

bool okeytre_buffer_get_modern_tone(const OKTInputMethodBuffer* buffer) {
  return buffer != nullptr && buffer->impl.modern_tone();
}

void okeytre_buffer_set_modern_tone(OKTInputMethodBuffer* buffer,
                                    bool enabled) {
  if (buffer != nullptr) {
    buffer->impl.SetModernTone(enabled);
  }
}

bool okeytre_buffer_get_uo_horn(const OKTInputMethodBuffer* buffer) {
  return buffer != nullptr && buffer->impl.uo_horn();
}

void okeytre_buffer_set_uo_horn(OKTInputMethodBuffer* buffer, bool enabled) {
  if (buffer != nullptr) {
    buffer->impl.SetUoHorn(enabled);
  }
}

bool okeytre_buffer_get_ng_to_ngh(const OKTInputMethodBuffer* buffer) {
  return buffer != nullptr && buffer->impl.ng_to_ngh();
}

void okeytre_buffer_set_ng_to_ngh(OKTInputMethodBuffer* buffer, bool enabled) {
  if (buffer != nullptr) {
    buffer->impl.SetNgToNgh(enabled);
  }
}

bool okeytre_buffer_get_ngh_to_ng(const OKTInputMethodBuffer* buffer) {
  return buffer != nullptr && buffer->impl.ngh_to_ng();
}

void okeytre_buffer_set_ngh_to_ng(OKTInputMethodBuffer* buffer, bool enabled) {
  if (buffer != nullptr) {
    buffer->impl.SetNghToNg(enabled);
  }
}

bool okeytre_buffer_get_restore_invalid_words(
    const OKTInputMethodBuffer* buffer) {
  return buffer != nullptr && buffer->impl.restore_invalid_words();
}

void okeytre_buffer_set_restore_invalid_words(OKTInputMethodBuffer* buffer,
                                              bool enabled) {
  if (buffer != nullptr) {
    buffer->impl.SetRestoreInvalidWords(enabled);
  }
}

int32_t okeytre_buffer_get_max_size(const OKTInputMethodBuffer* buffer) {
  return buffer != nullptr ? buffer->impl.max_buffer_size()
                           : okeytre::InputMethodBuffer::kDefaultMaxBufferSize;
}

void okeytre_buffer_set_max_size(OKTInputMethodBuffer* buffer,
                                 int32_t max_buffer_size) {
  if (buffer != nullptr) {
    buffer->impl.SetMaxBufferSize(max_buffer_size);
  }
}

bool okeytre_buffer_consume_caret_read_request(OKTInputMethodBuffer* buffer) {
  return buffer != nullptr && buffer->impl.ConsumeCaretReadRequest();
}

void okeytre_buffer_seed_with_surrounding_text(OKTInputMethodBuffer* buffer,
                                               const uint16_t* word,
                                               int32_t word_len,
                                               int32_t caret_pos) {
  if (buffer != nullptr) {
    buffer->impl.SeedWithSurroundingText(word, word_len, caret_pos);
  }
}

bool okeytre_buffer_handle_input(OKTInputMethodBuffer* buffer,
                                 const uint16_t* input, int32_t input_len,
                                 uint32_t modifiers,
                                 int32_t* out_backspace_count,
                                 uint16_t* out_text, int32_t out_text_capacity,
                                 int32_t* out_text_len) {
  if (buffer == nullptr) {
    return false;
  }
  okeytre::TextEditAction action =
      buffer->impl.HandleInput(input, input_len, modifiers);
  return WriteTextEditAction(action, out_backspace_count, out_text,
                             out_text_capacity, out_text_len);
}

bool okeytre_buffer_restore_current_word(OKTInputMethodBuffer* buffer,
                                         int32_t* out_backspace_count,
                                         uint16_t* out_text,
                                         int32_t out_text_capacity,
                                         int32_t* out_text_len) {
  if (buffer == nullptr) {
    return false;
  }
  okeytre::TextEditAction action = buffer->impl.RestoreCurrentWord();
  return WriteTextEditAction(action, out_backspace_count, out_text,
                             out_text_capacity, out_text_len);
}

}  // extern "C"
