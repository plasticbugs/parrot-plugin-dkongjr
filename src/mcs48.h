// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// mcs48.h -- an Intel 8035 (here Fujitsu's MB8884), the sound board's CPU.
//
// Like z80.h, the instructions are in mcs48_core.h, which the board's file
// includes after defining the CPU's connections:
//
//   static inline uint8_t M48_ROM(uint16_t a);           program memory (4K)
//   static inline uint8_t M48_PORT_R(int p);             port 1 or 2's pins
//   static inline void    M48_PORT_W(int p, uint8_t v);  port 1 or 2 latched
//   static inline uint8_t M48_EXT_R(uint8_t a);          MOVX A,@Rr
//   static inline void    M48_EXT_W(uint8_t a, uint8_t v);
//   static inline uint8_t M48_BUS_R(void);               INS A,BUS
//   static inline void    M48_BUS_W(uint8_t v);          OUTL BUS,A
//   static inline int     M48_T0(void), M48_T1(void);    the test inputs
//
// m48_step() runs one instruction and returns its machine cycles (15
// clocks each); m->cycles counts them as it goes, so a port write sees the
// time of its instruction.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t pc;                 // 12 bits
    uint8_t a, psw;              // PSW: CY AC F0 BS 1 SP2-0
    uint8_t f1, a11;             // F1; A11 for the next JMP/CALL (SEL MB)
    uint8_t p1, p2;              // the ports' latches
    uint8_t timer, prescaler;
    bool timer_on, counter_on;   // STRT T / STRT CNT
    bool timer_flag;             // TF (JTF)
    bool tirq_enabled, xirq_enabled;
    bool timer_irq;              // a timer interrupt waiting
    bool irq_in_progress;
    bool irq_line;               // the INT pin, asserted
    uint8_t t1_last;             // for the counter, T1's last level
    uint8_t ram[64];
    uint64_t cycles;
} m48_t;
