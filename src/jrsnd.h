// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// jrsnd.h -- Donkey Kong Jr.'s sound board after the 8035: the walk, jump,
// climb and fall circuits, and the DAC's envelope and filter, mixed as MAME
// 0.288's dkongjr_discrete has them (nintendo/dkong_a.cpp and the discrete
// sound modules, BSD-3-Clause, by Couriersud, Derrick Renaud and the MAME
// team). The node numbers in jrsnd.c are MAME's.
#pragma once
#include <stdint.h>

typedef struct jr_snd jr_snd_t;

// rate: steps a second (MAME runs it at 48,000; the cart at two a line)
void jr_snd_reset(float rate);
// One step. bits are jr.h's JR_S_*, dac the 8035's DAC (0-255, a fraction
// allowed). The result is MAME's speaker sample: 16-bit full scale is 32767.
float jr_snd_step(uint8_t bits, float dac);
