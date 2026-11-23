// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// bench.c -- the plugin's work for a list (src/jrplay.c), built for the
// cart's Cortex-M33 for test/jr/armcount.py to run in Unicorn and count.
// The ROMs and the inputs are put into memory from outside.
#include <stdint.h>
#include <string.h>

#include "jrplay.h"

jr_roms_t bench_roms;                    // armcount.py writes jr_roms_build()'s result here
struct { uint32_t frame; uint8_t port, val; } bench_ev[4096];   // and the script here
uint32_t bench_nev;
uint8_t bench_list[4096];
volatile uint32_t bench_len;
static uint32_t evi;

static void hook(void) {
    static uint8_t in0, in2 = 0x40;
    while (evi < bench_nev && bench_ev[evi].frame <= jr.frame) {
        if (bench_ev[evi].port == 0) in0 = bench_ev[evi].val;
        else in2 = (uint8_t)(0x40 | bench_ev[evi].val);
        evi++;
    }
    jr.in0 = in0;
    jr.in2 = in2;
}

void __attribute__((noinline)) bench_mark(void) { __asm volatile("" ::: "memory"); }

int main(void) {
    jrp_init(&bench_roms);
    jrp_frame_hook = hook;
    for (;;) {
        bench_len = (uint32_t)jrp_list(bench_list);
        bench_mark();
    }
}

extern uint32_t __stack_top;
void __attribute__((naked, noreturn)) _start(void) {
    __asm volatile("ldr sp, =__stack_top\n bl main\n b .");
}
// what newlib asks of a system without one
void *_sbrk(int n) { (void)n; return (void *)-1; }
