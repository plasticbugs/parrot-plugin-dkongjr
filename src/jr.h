// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// jr.h -- the Donkey Kong Jr. arcade board (Nintendo TKG-04), as MAME
// 0.288's dkong driver has it: a Z80 at 3.072 MHz with the 8257 DMA that
// copies the sprites, the tile and sprite video, and the sound board's
// 8035 at 6 MHz with its latches. Portable C: the cart builds it, and so
// does the Mac, to test it against MAME frame by frame.
//
// Time goes by scanlines: 264 a frame at 60.606 Hz, 192 Z80 cycles and 25
// of the 8035's machine cycles each. Vertical blank starts at line 240,
// where the board raises the Z80's NMI (if the game has it on) and calls
// jr_vblank(), the moment MAME draws its frame.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "mcs48.h"
#include "z80.h"

#define JR_LINES        264
#define JR_VBLANK_LINE  240
#define JR_Z80_LINE     192     // Z80 cycles a line
#define JR_M48_LINE     25      // 8035 machine cycles a line (7.68 Z80 cycles each)
#define JR_LINE_US      62.5    // 384 pixels at 6.144 MHz

// The ROMs, decoded where it helps (jrroms.c fills them from the set)
typedef struct {
    uint8_t main[0x6000];       // 5b 5c 5e
    uint8_t snd[0x1000];        // 3h
    uint8_t tiles[512][8][8];   // 3n 3p: 512 tiles of 8x8, pens 0-3, [tile][row][x] as MAME draws them
    uint8_t sprites[128][16][16]; // 7c-7f: 128 sprites of 16x16, pens 0-3
    uint8_t prom[0x300];        // 2e 2f (palette), 2n (tile colours)
} jr_roms_t;

// One scanline of what the sound circuits are given
typedef struct {
    uint16_t dac;               // the 8035's DAC over the line, its average x 256
    uint8_t bits;               // JR_S_*: the discrete inputs at the line's end
} jr_sline_t;
#define JR_S_WALK   0x01        // 6H bit 0: SOUND0
#define JR_S_JUMP   0x02        // 6H bit 1: SOUND1
#define JR_S_CLIMB  0x04        // 6H bit 2: SOUND2
#define JR_S_SND7   0x08        // 6H bit 7: SOUND7 (switches SOUND0's flavour)
#define JR_S_FALL   0x10        // 5H bit 1: SOUND9
#define JR_S_DECAY  0x20        // the 8035's P2 bit 7: the DAC's decay (DISCHARGE_INV)

// The arcade's inputs, active high as the board reads them
#define JR_IN_RIGHT 0x01        // IN0
#define JR_IN_LEFT  0x02
#define JR_IN_UP    0x04
#define JR_IN_DOWN  0x08
#define JR_IN_JUMP  0x10
#define JR_IN_START1 0x04       // IN2
#define JR_IN_START2 0x08
#define JR_IN_COIN  0x80

typedef struct {
    z80_t cpu;
    m48_t snd;
    uint8_t ram[0x1800];        // $6000-$77FF: work RAM, sprite RAM ($7000), video RAM ($7400)
    // the latches
    uint8_t tune;               // LS174 3D at $7C00: the 8035's MOVX
    uint8_t l4h, l6h, l5h;      // LS259 4H ($7C80-87), 6H ($7D00-07), 5H ($7D80-87)
    uint8_t vp2;                // the 8035's port 2 as written
    bool snd_irq;
    uint16_t pend_a[4];         // the Z80's writes to them, for the instruction's end
    uint8_t pend_v[4], npend;
    // the video's registers
    uint8_t gfx_bank, palette_bank, flip, sprite_bank, nmi_mask;
    // the 8257
    uint16_t dma_addr[4], dma_count[4];
    uint8_t dma_mode, dma_status;
    bool dma_msb;
    // inputs
    uint8_t in0, in1, in2, dsw0;
    // time
    int line;                   // the next line to run
    bool started;               // (the first line, at the start of a blank, isn't its end)
    uint32_t frame;             // vertical blanks so far
    int64_t z80_target;
    // the DAC over the line being run
    uint8_t dac;
    uint32_t dac_sum;
    uint64_t dac_since, line_m48;
} jr_board_t;

extern jr_board_t jr;
extern const jr_roms_t *jr_roms;

void jr_reset(const jr_roms_t *roms);
// Run one scanline; its sound goes in *s
void jr_line(jr_sline_t *s);
// Called at the start of vertical blank (line 240), before the NMI: the
// board's picture is final for the frame. The plugin and the tests set it.
extern void (*jr_vblank)(void);
// How long the sprite copy holds the Z80, from the end of the instruction
// that starts it (in Z80 cycles since the reset); NULL = as MAME mostly does
extern int (*jr_dma_hold)(uint64_t end);

// The picture, as MAME draws it, turned upright: 224 wide, 256 high, one
// palette index (0-255) a pixel. A slow, exact renderer for tests and
// previews; the cart's encoder draws its own way (jrcv.c).
#define JR_W 224
#define JR_H 256
void jr_render(uint8_t *pens /* JR_H rows of JR_W */);

// The colours: the palette PROMs through the board's resistor networks,
// as 0xRRGGBB (MAME's dkong2b_palette)
void jr_palette(uint32_t rgb[256]);
