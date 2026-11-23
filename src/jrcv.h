// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// jrcv.h -- Donkey Kong Jr.'s picture and sound as the frame kernel's lists.
//
// The picture: the play area (the arcade's lines 16-255) at 3/4, 168x180,
// at x 48, y 6, on the TMS9918's 8-pixel grid; the scores, bonus, level and
// lives at the arcade's own size in the margins either side (6 columns on
// the left, 5 on the right). Sent as a Graphics II bitmap: a model of the
// console's video RAM, and each list sends bytes that differ.
//
// The sound: 100 samples a list through the three-tone DAC, at their times
// between the picture's runs. Every list costs the same (JRCV_LIST_T), so
// the console runs them back to back and that is the sample clock.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JRCV_SAMPLES 100
#define JRCV_LIST_MAX 4096

// the console's Z80 cycles from one list's first sample to the next's (the
// lists run back to back), and each sample's first write from the first's,
// as MAME's ColecoVision runs them
extern uint32_t jrcv_list_t;
extern uint32_t jrcv_sample_t[JRCV_SAMPLES];
// a list's length by gbkernel.asm's costs, which the kit's models use (they
// are a little over)
extern uint32_t jrcv_kit_t;

// the board's 256 colours (0xRRGGBB, jr_palette()) to the console's
void jrcv_init(const uint32_t rgb[256]);

// The next picture, in two steps: jrcv_hud() with the arcade's tiles alone
// (224x256 pens, upright) takes the scores, bonus, level and lives and
// clears their places; then, with the sprites drawn over that,
// jrcv_picture() makes the rest.
void jrcv_hud(uint8_t *pens);
void jrcv_picture(const uint8_t *pens);

// how many video RAM bytes the last picture still has to send (0: all sent)
uint32_t jrcv_left(void);

// a list: the samples (DAC levels 0-511) and as much of the picture as fits.
// Returns its length.
size_t jrcv_list(uint8_t *out, const uint16_t level[JRCV_SAMPLES]);

// the model of the console's video RAM (16K), for previews and tests
const uint8_t *jrcv_vram(void);

// for tests: the last picture made whole from pens against the one made a
// part at a time; how many runs differ
int jrcv_check(const uint8_t *pens);

// for tests: the HUD jrcv_hud() took against these pens (the tiles alone,
// drawn whole), how many pixels differ; then the holes cleared, as
// jrcv_hud() does
int jrcv_hud_check(uint8_t *pens);
