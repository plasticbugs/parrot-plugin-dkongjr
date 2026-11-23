// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// z80.h -- a Zilog Z80, for the arcade board's main CPU.
//
// The instructions themselves are in z80_core.h, which the board's file
// includes after defining how the CPU reaches its memory, so that every
// read and write is inlined:
//
//   static inline uint8_t Z80_RD(uint16_t a);      a memory read (opcodes too)
//   static inline void    Z80_WR(uint16_t a, uint8_t v);
//   static inline uint8_t Z80_IN(uint16_t port);
//   static inline void    Z80_OUT(uint16_t port, uint8_t v);
//
// z80_core.h then defines z80_reset(), z80_nmi() and z80_run(), which runs
// whole instructions until the cycle budget is used up, and returns the
// cycles it ran (a little past the budget: an instruction is never split).
//
// Every instruction is here, the undocumented ones too, with the flags'
// bits 3 and 5 and the hidden WZ register as the chip has them, so a trace
// can be compared with MAME's line for line.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef union {
    uint16_t w;
    struct { uint8_t l, h; } b;          // (both the cart and the Mac are little-endian)
} z80_pair_t;

typedef struct {
    z80_pair_t af, bc, de, hl, ix, iy, sp, pc, wz;
    z80_pair_t af2, bc2, de2, hl2;
    uint8_t i, r, r7;                    // r7: bit 7 of R, which only LD R,A sets
    uint8_t iff1, iff2, im;
    bool halted;
    bool nmi_pending;                    // an NMI's edge, not taken yet
    bool irq_line;                       // the INT line (IM 0/1/2), held
    uint8_t irq_vector;                  // what the bus gives in an interrupt acknowledge
    bool ei_delay;                       // just after EI: no interrupt until the next instruction
    int stall;                           // cycles the bus was taken (a DMA) during the last instruction
    uint64_t cycles;                     // T-states run since reset
} z80_t;
