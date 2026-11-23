// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// jrplay.c -- see jrplay.h
#include "jrplay.h"

#include <math.h>
#include <string.h>

#include "jrcv.h"
#include "jrdraw.h"
#include "jrsnd.h"

// Time in units of 1 / (32,000 x 3,579,545) s, where both clocks are whole:
// a console cycle is 32,000 of them and a sound step (the board's line,
// 62.5 us) 7,159,090. A list (60,039 cycles) is 1.92e9: it fits.
#define U_T 32000u
#define U_STEP 7159090u

jrp_stats_t jrp_stats;
void (*jrp_frame_hook)(void);
void (*jrp_check_pens)(const uint8_t *pens);
void (*jrp_check_picture)(const uint8_t *pens);
void (*jrp_sample_hook)(float x);
float jrp_gain = JRP_GAIN;

static uint8_t pens[JR_W * JR_H];
static bool want = true, have;
static uint32_t list_u, slot_end[JRCV_SAMPLES];  // the list's length; each sample's share ends here
static uint32_t t_u;            // the next sound step's time from this list's start
static jr_sline_t lines[300];   // the board's lines in a list (268 or 269)
static float knee_tab[1025];    // the soft knee, |x| 0-4 in 1/256ths, as levels from the middle
static uint8_t in0, in2;

static void vblank(void) {
    jrp_stats.frames++;
    jr.in0 = in0;
    jr.in2 = (uint8_t)(0x40 | in2);
    if (jrp_frame_hook) jrp_frame_hook();
    if (want) {
        // the picture as it stands at the start of vertical blank
        jrd_frame(pens, jrcv_hud);
        if (jrp_check_pens) jrp_check_pens(pens);
        want = false;
        have = true;
    }
}

void jrp_init(const jr_roms_t *roms) {
    jr_reset(roms);
    jrd_init();
    static uint32_t rgb[256];       // (static: the stack is 2K)
    jr_palette(rgb);
    jrcv_init(rgb);
    jr_snd_reset(1e6f / JR_LINE_US);  // a step a line, 16 kHz
    jr_vblank = vblank;
    list_u = jrcv_list_t * U_T;
    for (int k = 0; k < JRCV_SAMPLES; k++)
        slot_end[k] = k + 1 < JRCV_SAMPLES ? jrcv_sample_t[k + 1] * U_T : list_u;
    // the knee: below k a sample stays as it is; above, it bends towards full
    // scale (references/porting.md, "How loud")
    for (int i = 0; i <= 1024; i++) {
        const float x = i / 256.0f, k = JRP_KNEE;
        const float y = x <= k ? x : k + (1 - k) * (1 - expf(-(x - k) / (1 - k)));
        knee_tab[i] = 255.5f * y;
    }
    t_u = 0;
    want = true;
    have = false;
    jrp_stats = (jrp_stats_t){0};
}

void jrp_controls(uint8_t i0, uint8_t i2) {
    in0 = i0;
    in2 = i2 & (JR_IN_START1 | JR_IN_START2 | JR_IN_COIN);
}

// a sample level (0-511) from the board's mean over its share of the list
static inline uint16_t level_of(float v, float *peak) {
    float x = v * jrp_gain * (256.0f / 32768);
    const float ax = fabsf(x);
    if (ax > *peak) *peak = ax;
    jrp_stats.samples++;
    if (ax > 256 * JRP_KNEE) jrp_stats.kneed++;
    if (jrp_sample_hook) jrp_sample_hook(x * (1.0f / 256));
    const int i = ax < 1024 ? (int)ax : 1023;
    const float f = ax < 1024 ? ax - (float)i : 1.0f;
    float y = knee_tab[i] + (knee_tab[i + 1] - knee_tab[i]) * f;
    const int lv = (int)lrintf(255.5f + (x < 0 ? -y : y));
    return (uint16_t)(lv < 0 ? 0 : lv > 511 ? 511 : lv);
}

size_t jrp_list(uint8_t *out) {
    // the board, for the list's time, a sound step a line; each sample is
    // the mean of the steps in its share of the list
    uint16_t level[JRCV_SAMPLES];
    float peak = 0, acc = 0, last = 0;
    int k = 0, n = 0;
    uint32_t end = slot_end[0];
    const int nl = (int)((list_u - t_u + U_STEP - 1) / U_STEP);
    jr_lines(nl, lines);
    for (int i = 0; i < nl; i++) {
        const float v = jr_snd_step(lines[i].bits, lines[i].dac * (1.0f / 256));
        while (t_u >= end) {
            if (n) last = acc / (float)n;
            level[k++] = level_of(last, &peak);
            acc = 0;
            n = 0;
            end = slot_end[k];
        }
        acc += v;
        n++;
        t_u += U_STEP;
    }
    while (k < JRCV_SAMPLES) {
        if (n) last = acc / (float)n;
        level[k++] = level_of(last, &peak);
        n = 0;
    }
    t_u -= list_u;
    jrp_stats.peak = peak / 256;
    // the picture: a new one when the last has all gone
    if (have && jrcv_left() == 0) {
        jrcv_picture(pens);
        if (jrp_check_picture) jrp_check_picture(pens);
        have = false;
        want = true;
        jrp_stats.pictures++;
    } else if (!have && jrcv_left() == 0)
        want = true;
    jrp_stats.lists++;
    return jrcv_list(out, level);
}
