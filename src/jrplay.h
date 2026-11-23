// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// jrplay.h -- the game, a list at a time: the board runs for as long as the
// console takes over a list (jrcv_list_t of its cycles, 16.77 ms), its
// sound goes into the list's 100 samples, and its picture into the list's
// runs whenever the last picture has all gone. The cart's loop and the Mac's
// tests both use it.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jr.h"

void jrp_init(const jr_roms_t *roms);

// the controls, as the arcade's inputs (active high), from the board's next
// frame on: IN0's stick and jump (JR_IN_*), IN2's starts and coin
void jrp_controls(uint8_t in0, uint8_t in2);

// for tests: called at each of the board's vertical blanks, after the
// controls go in (jr.frame counts them), to set jr.in0 and jr.in2 itself
extern void (*jrp_frame_hook)(void);

// for tests: called with each picture the board makes, drawn a part at a time
extern void (*jrp_check_pens)(const uint8_t *pens);
// and with each console picture made from them
extern void (*jrp_check_picture)(const uint8_t *pens);
// for tests: called with each sample before the knee (1.0 = full scale)
extern void (*jrp_sample_hook)(float x);

// the next list; its length
size_t jrp_list(uint8_t *out);

typedef struct {
    uint32_t lists, pictures;   // made so far
    uint32_t frames;            // the board's
    float peak;                 // the loudest sample lately, of full scale (1.0: the knee's top)
    uint32_t samples, kneed;    // samples made, and how many of them went into the knee
} jrp_stats_t;
extern jrp_stats_t jrp_stats;

// the sound's gain into the DAC (default JRP_GAIN). Samples above
// JRP_KNEE of full scale bend towards full scale (a soft knee).
#define JRP_GAIN 9.0f
#define JRP_KNEE 0.75f
extern float jrp_gain;
