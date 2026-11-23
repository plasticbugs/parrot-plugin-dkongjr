// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// jrcv.c -- see jrcv.h
#include "jrcv.h"

#include <string.h>

#include "cvscale.h"
#include "jr.h"
#include "jrdraw.h"
#include "jrdac.h"

// ---- the layout ---------------------------------------------------------------------------
#define PF_X 48                 // the play area on the console: 21 columns from column 6
#define PF_Y 6
#define PF_TOP 16               // the arcade's lines 16-255 (240) make 180
#define PF_LINES 240
#define PF_W (JR_W * 3 / 4)    // 168: 21 runs

typedef struct { uint8_t sx, sy, w, dx, dy; } piece_t;   // 8 lines high, from (sx, sy) to (dx, dy)
static const piece_t hud[] = {
    // the left margin, 6 columns: 1UP and the score, HIGH / SCORE and the high score
    {8, 0, 48, 0, 8},           // 1UP (it blinks)
    {8, 8, 48, 0, 16},          // the score
    {72, 0, 32, 8, 32},         // HIGH
    {112, 0, 40, 4, 40},        // SCORE
    {88, 8, 48, 0, 48},         // the high score
    // the right margin, 5 columns: the bonus, BONUS, the level and L, the lives
    {160, 32, 32, 220, 8},      // the bonus's digits
    {160, 40, 34, 216, 16},     // BONUS (not the level box's edge after it)
    {200, 32, 8, 232, 32},      // the level
    {200, 40, 8, 232, 40},      // L
    {152, 16, 40, 216, 56},     // the lives
};
// the HUD's places in the play area, cleared before the sprites go on
static const struct { uint8_t x, y, w, h; } holes[] = {
    {152, 16, 64, 32},          // the lives, the bonus box and the level box
};

// ---- the colours --------------------------------------------------------------------------
// the TMS9918's colours as the kit draws them (tools/png256.py)
static const uint8_t tms_rgb[16][3] = {
    {0, 0, 0}, {0, 0, 0}, {33, 200, 66}, {94, 220, 120}, {84, 85, 237}, {125, 118, 252}, {212, 82, 77},
    {66, 235, 245}, {252, 85, 84}, {255, 121, 120}, {212, 193, 84}, {230, 206, 128}, {33, 176, 59},
    {201, 91, 186}, {204, 204, 204}, {255, 255, 255}};
static uint8_t pen_tms[256];
static uint16_t tdist[16][16];  // between two console colours, squared, /64

static uint32_t rgb_dist(int r1, int g1, int b1, int r2, int g2, int b2) {
    // a little weighted for the eye (redmean)
    const int rm = (r1 + r2) / 2, dr = r1 - r2, dg = g1 - g2, db = b1 - b2;
    return (uint32_t)(((512 + rm) * dr * dr >> 8) + 4 * dg * dg + ((767 - rm) * db * db >> 8));
}

// ---- the console's video RAM --------------------------------------------------------------
static uint8_t vram[0x4000];    // what the console holds (as far as we know)
static uint8_t tgt[0x4000];     // what it should
static uint16_t dirty[1536 + 104];   // 8-byte groups that differ, in address order
static int ndirty, dcur;
static uint16_t refresh;        // where the next filler run starts (when nothing differs)
static bool first = true;
static uint32_t scr[192][32];   // the picture being made, console colours: each run's 8 (pack8)

// two console colours for an 8x1 run: the pattern byte (1 = fg) and colour byte
static inline void run_code(const uint8_t *c, uint8_t *pat, uint8_t *col) {
    uint8_t a = c[0], b = c[0];
    int many = 0;
    for (int i = 1; i < 8; i++) {
        if (c[i] == a) continue;
        if (b == a) { b = c[i]; continue; }
        if (c[i] != b) { many = 1; break; }
    }
    if (many) {
        // the pair that costs least, each pixel the nearer of the two
        uint8_t seen[8];
        int n = 0;
        for (int i = 0; i < 8; i++) {
            int j = 0;
            while (j < n && seen[j] != c[i]) j++;
            if (j == n) seen[n++] = c[i];
        }
        uint32_t best = UINT32_MAX;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                uint32_t cost = 0;
                for (int k = 0; k < 8; k++) {
                    const uint16_t di = tdist[c[k]][seen[i]], dj = tdist[c[k]][seen[j]];
                    cost += di < dj ? di : dj;
                }
                if (cost < best) { best = cost; a = seen[i]; b = seen[j]; }
            }
    }
    if (a > b) { uint8_t t = a; a = b; b = t; }     // the darker index the background
    uint8_t p = 0;
    if (a != b)
        for (int k = 0; k < 8; k++)
            if (c[k] == b || (c[k] != a && tdist[c[k]][b] < tdist[c[k]][a])) p |= (uint8_t)(0x80 >> k);
    *pat = p;
    *col = (uint8_t)(b << 4 | a);
}

// the groups (8 bytes) of patterns and colours that a run changed
static uint8_t pat_changed[768], col_changed[768];

// 8 bytes alike (memcmp is a call, and slow, on the M33)
static inline bool same8(const uint8_t *a, const uint8_t *b) {
    uint32_t a0, a1, b0, b1;
    memcpy(&a0, a, 4); memcpy(&a1, a + 4, 4);
    memcpy(&b0, b, 4); memcpy(&b1, b + 4, 4);
    return a0 == b0 && a1 == b1;
}

// a run's 8 pixels (0-15) in a word: pixel i in byte i & 3, the low half
// for 0-3, the high for 4-7
static inline uint32_t pack8(const uint8_t *px) {
    uint32_t a, b;
    memcpy(&a, px, 4);
    memcpy(&b, px + 4, 4);
    return a | b << 4;
}
static inline void unpack8(uint32_t v, uint8_t *px) {
    const uint32_t a = v & 0x0F0F0F0Fu, b = v >> 4 & 0x0F0F0F0Fu;
    memcpy(px, &a, 4);
    memcpy(px + 4, &b, 4);
}

// run (y, cx), whose pixels are px, coded again
static inline void code_run(int y, int cx, const uint8_t *px) {
    const uint16_t a = (uint16_t)((y >> 6) * 0x800 + ((y & 63) >> 3) * 256 + cx * 8 + (y & 7));
    run_code(px, &tgt[a], &tgt[0x2000 + a]);
    pat_changed[a >> 3] = col_changed[a >> 3] = 1;
}

// 8 new pixels for run (y, cx): coded again if they differ
static inline void set_run(int y, int cx, const uint8_t *px) {
    const uint32_t v = pack8(px);
    if (scr[y][cx] == v) return;
    scr[y][cx] = v;
    code_run(y, cx, px);
}

static uint8_t hud_px[sizeof hud / sizeof hud[0]][8][48];  // the HUD's pieces, console colours

// ---- the list's plan ----------------------------------------------------------------------
uint32_t jrcv_list_t;
uint32_t jrcv_sample_t[JRCV_SAMPLES];
static uint8_t run_n[JRCV_SAMPLES];      // the video RAM run before sample k (k >= 1)
static uint8_t extra_n[JRCV_SAMPLES];    // the chip's other registers, after sample k's three
static const uint8_t extra[JRCV_SAMPLES][2] = {
    [1] = {0x81, 0x00}, [2] = {0xA1, 0x00}, [3] = {0xC1, 0x00}, [4] = {0xFF, 0},
};

// The costs in gbkernel.asm's header, which the kit's models check a list by
#define K_KERNEL 480
#define K_BLANK_END 54
#define K_END 52
#define K_RUN(n) (106 + 31 * (n))
#define K_PSG(n) (75 + 52 * (n))
// and as MAME 0.288's ColecoVision runs them, measured with its taps on the
// PSG's port (test/jr/probe.lua): a byte of sound 51 cycles, not 52; from
// the last sample's first write to the next list's first, back to back,
// 688; the NMI (it counts frames, once in every list) 105. The header's
// costs are a little over, so a list they make 60,036 is under a frame on
// MAME, and the console waits for the next frame instead.
#define R_RUN(n) (106 + 31 * (n))
#define R_PSG(n) (75 + 51 * (n))
#define R_BOUND 688
#define R_NMI 105
#define LIST_AIM 60036          // a frame (59,736) and 300, so the lists run back to back
uint32_t jrcv_kit_t;

void jrcv_init(const uint32_t rgb[256]) {
    for (unsigned i = 0; i < sizeof holes / sizeof holes[0]; i++) jrd_keep(holes[i].x, holes[i].y, holes[i].w, holes[i].h);
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            tdist[i][j] = (uint16_t)(rgb_dist(tms_rgb[i][0], tms_rgb[i][1], tms_rgb[i][2], tms_rgb[j][0], tms_rgb[j][1], tms_rgb[j][2]) >> 6);
    for (int p = 0; p < 256; p++) {
        const int r = rgb[p] >> 16 & 255, g = rgb[p] >> 8 & 255, b = rgb[p] & 255;
        uint32_t best = UINT32_MAX;
        for (int c = 1; c < 16; c++) {
            const uint32_t d = rgb_dist(r, g, b, tms_rgb[c][0], tms_rgb[c][1], tms_rgb[c][2]);
            if (d < best) { best = d; pen_tms[p] = (uint8_t)c; }
        }
        // the vines' and platforms' pale pink highlight: nearest is white,
        // which turns the orange vines white at 3/4; light yellow keeps them warm
        if (rgb[p] == 0xF9DFF2) pen_tms[p] = 11;
    }
    // the plan: samples evenly spaced, the runs between them as near as
    // whole bytes allow; the gap from the last to the next list's first is
    // the kernel's (R_BOUND), with the NMI somewhere in the list
    extra_n[1] = extra_n[2] = extra_n[3] = 2;
    extra_n[4] = 1;
    const double gap = (double)(LIST_AIM - R_BOUND - R_NMI) / (JRCV_SAMPLES - 1);
    uint32_t t = 0, kit = K_KERNEL + K_BLANK_END;
    jrcv_sample_t[0] = 0;
    for (int k = 1; k < JRCV_SAMPLES; k++) {
        t += R_PSG(3 + extra_n[k - 1]);
        kit += K_PSG(3 + extra_n[k - 1]);
        int n = (int)((gap * k - t - 106) / 31 + 0.5);
        if (n < 1) n = 1;
        run_n[k] = (uint8_t)n;
        t += R_RUN(n);
        kit += K_RUN(n);
        jrcv_sample_t[k] = t;
    }
    jrcv_list_t = t + R_BOUND + R_NMI;
    jrcv_kit_t = kit + K_PSG(3 + extra_n[JRCV_SAMPLES - 1]) + K_END;
    memset(vram, 0, sizeof vram);
    memset(tgt, 0, sizeof tgt);
    // the name tables: each third its 256 slots in order; no sprites
    for (int i = 0; i < 768; i++) tgt[0x1800 + i] = (uint8_t)i;
    tgt[0x1B00] = 0xD0;
    // the picture black to begin with, every run coded
    static const uint8_t black[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    for (int y = 0; y < 192; y++)
        for (int cx = 0; cx < 32; cx++) {
            scr[y][cx] = 0x11111111u;
            code_run(y, cx, black);
        }
    memset(hud_px, 1, sizeof hud_px);
    ndirty = dcur = 0;
    refresh = 0;
    first = true;
}

// ---- the picture --------------------------------------------------------------------------
static void clear_holes(uint8_t *pens) {
    for (unsigned i = 0; i < sizeof holes / sizeof holes[0]; i++)
        for (int y = 0; y < holes[i].h; y++)
            memset(&pens[(holes[i].y + y) * JR_W + holes[i].x], 0, holes[i].w);
}

void jrcv_hud(uint8_t *pens) {
    // (into hud_px: the picture before is still on its way to the console)
    for (unsigned i = 0; i < sizeof hud / sizeof hud[0]; i++) {
        const piece_t *h = &hud[i];
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < h->w; x++) hud_px[i][y][x] = pen_tms[pens[(h->sy + y) * JR_W + h->sx + x]];
    }
    clear_holes(pens);
}

int jrcv_hud_check(uint8_t *pens) {
    int bad = 0;
    for (unsigned i = 0; i < sizeof hud / sizeof hud[0]; i++) {
        const piece_t *h = &hud[i];
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < h->w; x++) bad += hud_px[i][y][x] != pen_tms[pens[(h->sy + y) * JR_W + h->sx + x]];
    }
    clear_holes(pens);
    return bad;
}

void jrcv_picture(const uint8_t *pens) {
    // the play area: each band of 8 lines with blocks that changed, scaled
    // again (with 4 pixels more either side, which the scaler looks at,
    // and out to whole runs), to the console's colours, and the runs that
    // changed coded again
    for (int by = PF_TOP / 8; by < JRD_BH; by++) {
        int lo = -1, hi = -1;
        for (int bx = 0; bx < JRD_BW; bx++)
            if (jrd_dirty[by][bx]) { if (lo < 0) lo = bx; hi = bx; }
        if (lo < 0) continue;
        memset(jrd_dirty[by], 0, JRD_BW);
        const int x0 = lo * 8 - 4 < 0 ? 0 : lo * 8 - 4, x1 = hi * 8 + 12 > JR_W ? JR_W : hi * 8 + 12;
        // the runs those come out in, and the columns of 4 that make them
        const int o0 = x0 / 4 * 3 / 8 * 8, o1 = (x1 / 4 * 3 + 7) / 8 * 8 > PF_W ? PF_W : (x1 / 4 * 3 + 7) / 8 * 8;
        const int s0 = o0 / 3 * 4, s1 = (o1 + 2) / 3 * 4 > JR_W ? JR_W : (o1 + 2) / 3 * 4;
        static uint8_t band[6][JR_W * 3 / 4];     // (static: the stack is 2K)
        cvscale34(pens + by * 8 * JR_W, JR_W, JR_W, 8, s0, s1, &band[0][0], JR_W * 3 / 4);
        const int oy0 = (by * 8 - PF_TOP) / 4 * 3;
        for (int r = 0; r < 6; r++)
            for (int ox = o0; ox < o1; ox += 8) {
                uint8_t px[8];
                for (int i = 0; i < 8; i++) px[i] = pen_tms[band[r][ox + i]];
                set_run(PF_Y + oy0 + r, (PF_X + ox) / 8, px);
            }
    }
    memset(jrd_dirty, 0, PF_TOP / 8 * JRD_BW);
    // the HUD's pieces
    for (unsigned pi = 0; pi < sizeof hud / sizeof hud[0]; pi++) {
        const piece_t *h = &hud[pi];
        for (int y = 0; y < 8; y++) {
            const int cy = h->dy + y;
            for (int cx = h->dx / 8; cx <= (h->dx + h->w - 1) / 8; cx++) {
                uint8_t px[8];
                unpack8(scr[cy][cx], px);
                for (int i = 0; i < 8; i++) {
                    const int x = cx * 8 + i - h->dx;
                    if (x >= 0 && x < h->w) px[i] = hud_px[pi][y][x];
                }
                set_run(cy, cx, px);
            }
        }
    }
    if (first) {
        // nothing is known on the console yet: everything differs
        for (int a = 0; a < 0x1B01; a++) vram[a] = (uint8_t)~tgt[a];
        for (int a = 0x2000; a < 0x3800; a++) vram[a] = (uint8_t)~tgt[a];
        memset(pat_changed, 1, sizeof pat_changed);
        memset(col_changed, 1, sizeof col_changed);
        first = false;
    }
    // the 8-byte groups that differ, in address order: names, patterns, colours
    ndirty = dcur = 0;
    for (int a = 0x1800; a < 0x1B08; a += 8)
        if (memcmp(&vram[a], &tgt[a], a == 0x1B00 ? 1 : 8)) dirty[ndirty++] = (uint16_t)a;
    for (int g = 0; g < 768; g++) {
        const int a = g * 8;
        if (pat_changed[g] && !same8(&vram[a], &tgt[a])) dirty[ndirty++] = (uint16_t)a;
        if (col_changed[g] && !same8(&vram[0x2000 + a], &tgt[0x2000 + a])) dirty[ndirty++] = (uint16_t)(0x2000 + a);
    }
    memset(pat_changed, 0, sizeof pat_changed);
    memset(col_changed, 0, sizeof col_changed);
}

// a group still differs (a run for another may have covered it)
static inline bool group_dirty(uint16_t a) {
    return a == 0x1B00 ? vram[a] != tgt[a] : !same8(&vram[a], &tgt[a]);
}

uint32_t jrcv_left(void) {
    while (dcur < ndirty && !group_dirty(dirty[dcur])) dcur++;
    return (uint32_t)(ndirty - dcur) * 8;
}

const uint8_t *jrcv_vram(void) { return vram; }


// the filler goes round the names, the patterns and the colours
static const uint16_t fill_lo[3] = {0x1800, 0x0000, 0x2000}, fill_hi[3] = {0x1B00, 0x1800, 0x3800};
static int fill_r;
static uint16_t filler(int n) {
    if (refresh < fill_lo[fill_r] || refresh + n > fill_hi[fill_r]) {
        fill_r = (fill_r + 1) % 3;
        refresh = fill_lo[fill_r];
    }
    const uint16_t a = refresh;
    refresh = (uint16_t)(refresh + n);
    return a;
}

size_t jrcv_list(uint8_t *out, const uint16_t level[JRCV_SAMPLES]) {
    uint8_t *o = out;
    *o++ = 0;                   // the blank part: nothing (the lists don't keep in step with it)
    for (int k = 0; k < JRCV_SAMPLES; k++) {
        if (k) {
            const int n = run_n[k];
            uint16_t a;
            while (dcur < ndirty && !group_dirty(dirty[dcur])) dcur++;
            if (dcur < ndirty) {
                a = dirty[dcur];
                while (vram[a] == tgt[a]) a++;      // from its first byte that differs
            } else
                a = filler(n);      // nothing differs: send again what the console should have
            *o++ = 1;
            *o++ = (uint8_t)a;
            *o++ = (uint8_t)(a >> 8 | 0x40);
            *o++ = (uint8_t)n;
            for (int i = 0; i < n; i++) {
                const uint16_t b = (uint16_t)((a + i) & 0x3FFF);
                vram[b] = tgt[b];
                *o++ = tgt[b];
            }
        }
        const uint16_t t = jr_dac3[level[k] > 511 ? 511 : level[k]];
        *o++ = 2;
        *o++ = (uint8_t)(3 + extra_n[k]);
        *o++ = (uint8_t)(0x90 | t >> 8);
        *o++ = (uint8_t)(0xB0 | (t >> 4 & 15));
        *o++ = (uint8_t)(0xD0 | (t & 15));
        for (int i = 0; i < extra_n[k]; i++) *o++ = extra[k][i];
    }
    *o++ = 0;
    return (size_t)(o - out);
}

// For tests: the picture made whole from these pens (the play area scaled,
// the HUD's pieces, every run coded) against what jrcv_picture() made a part
// at a time. Returns how many runs differ.
int jrcv_check(const uint8_t *pens) {
    static uint8_t sc[PF_LINES * 3 / 4][JR_W * 3 / 4], full[192][256];
    cvscale34(pens + PF_TOP * JR_W, JR_W, JR_W, PF_LINES, 0, JR_W, &sc[0][0], JR_W * 3 / 4);
    for (int y = 0; y < 192; y++)
        for (int cx = 0; cx < 32; cx++) unpack8(scr[y][cx], &full[y][cx * 8]);
    for (int y = 0; y < PF_LINES * 3 / 4; y++)
        for (int x = 0; x < JR_W * 3 / 4; x++) full[PF_Y + y][PF_X + x] = pen_tms[sc[y][x]];
    for (unsigned pi = 0; pi < sizeof hud / sizeof hud[0]; pi++)
        for (int y = 0; y < 8; y++) memcpy(&full[hud[pi].dy + y][hud[pi].dx], hud_px[pi][y], hud[pi].w);
    int bad = 0;
    for (int y = 0; y < 192; y++)
        for (int cx = 0; cx < 32; cx++) {
            const uint16_t a = (uint16_t)((y >> 6) * 0x800 + ((y & 63) >> 3) * 256 + cx * 8 + (y & 7));
            uint8_t p, c;
            run_code(&full[y][cx * 8], &p, &c);
            if (pack8(&full[y][cx * 8]) != scr[y][cx] || p != tgt[a] || c != tgt[0x2000 + a]) bad++;
        }
    return bad;
}
