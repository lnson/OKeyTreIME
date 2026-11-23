// Copyright (C) 2026 lnson
// SPDX-License-Identifier: GPL-3.0-only

// Pure C Bridging Header exposing the portable C++ IME engine to Swift.

#ifndef OKEYTRE_ENGINE_H_
#define OKEYTRE_ENGINE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum OKTInputMethodStyle {
  OKT_STYLE_TELEX = 0,
  OKT_STYLE_VNI = 1,
  OKT_STYLE_VIQR = 2,
};

enum OKTModifierFlags {
  OKT_MOD_NONE = 0,
  OKT_MOD_SHIFT = 1 << 0,
  OKT_MOD_CONTROL = 1 << 1,
  OKT_MOD_OPTION = 1 << 2,
  OKT_MOD_COMMAND = 1 << 3,
};

extern const int32_t OKT_BUFFER_DEFAULT_MAX_SIZE;
extern const int32_t OKT_BUFFER_MIN_SIZE;
extern const int32_t OKT_BUFFER_MAX_SIZE;

typedef struct OKTInputMethodBuffer OKTInputMethodBuffer;

bool okeytre_is_word_boundary(uint16_t c);

OKTInputMethodBuffer* okeytre_buffer_create(enum OKTInputMethodStyle style,
                                            bool modern_tone, bool uo_horn,
                                            bool ng_to_ngh, bool ngh_to_ng,
                                            int32_t max_buffer_size);

void okeytre_buffer_destroy(OKTInputMethodBuffer* buffer);

void okeytre_buffer_reset(OKTInputMethodBuffer* buffer);

enum OKTInputMethodStyle okeytre_buffer_get_style(
    const OKTInputMethodBuffer* buffer);
void okeytre_buffer_set_style(OKTInputMethodBuffer* buffer,
                              enum OKTInputMethodStyle style);

bool okeytre_buffer_get_modern_tone(const OKTInputMethodBuffer* buffer);
void okeytre_buffer_set_modern_tone(OKTInputMethodBuffer* buffer, bool enabled);

bool okeytre_buffer_get_uo_horn(const OKTInputMethodBuffer* buffer);
void okeytre_buffer_set_uo_horn(OKTInputMethodBuffer* buffer, bool enabled);

bool okeytre_buffer_get_ng_to_ngh(const OKTInputMethodBuffer* buffer);
void okeytre_buffer_set_ng_to_ngh(OKTInputMethodBuffer* buffer, bool enabled);

bool okeytre_buffer_get_ngh_to_ng(const OKTInputMethodBuffer* buffer);
void okeytre_buffer_set_ngh_to_ng(OKTInputMethodBuffer* buffer, bool enabled);

bool okeytre_buffer_get_restore_invalid_words(
    const OKTInputMethodBuffer* buffer);
void okeytre_buffer_set_restore_invalid_words(OKTInputMethodBuffer* buffer,
                                              bool enabled);

int32_t okeytre_buffer_get_max_size(const OKTInputMethodBuffer* buffer);
void okeytre_buffer_set_max_size(OKTInputMethodBuffer* buffer,
                                 int32_t max_buffer_size);

bool okeytre_buffer_consume_caret_read_request(OKTInputMethodBuffer* buffer);

void okeytre_buffer_seed_with_surrounding_text(OKTInputMethodBuffer* buffer,
                                               const uint16_t* word,
                                               int32_t word_len,
                                               int32_t caret_pos);

bool okeytre_buffer_handle_input(OKTInputMethodBuffer* buffer,
                                 const uint16_t* input, int32_t input_len,
                                 uint32_t modifiers,
                                 int32_t* out_backspace_count,
                                 uint16_t* out_text, int32_t out_text_capacity,
                                 int32_t* out_text_len);

bool okeytre_buffer_restore_current_word(OKTInputMethodBuffer* buffer,
                                         int32_t* out_backspace_count,
                                         uint16_t* out_text,
                                         int32_t out_text_capacity,
                                         int32_t* out_text_len);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // OKEYTRE_ENGINE_H_
