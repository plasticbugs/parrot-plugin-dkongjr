// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// jrroms.h -- MAME's dkongjr set: its 13 files, known by size and CRC-32
// (any names), and how they make the board's ROMs (MAME 0.288's
// ROM_START(dkongjr)).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "jr.h"

#define JR_NROMS 13

typedef struct {
    const char *name;           // MAME's name, to say which one is missing
    uint32_t crc;
    uint16_t size;
} jr_rom_t;

extern const jr_rom_t jr_rom_list[JR_NROMS];

// the list's index for a file of this size and CRC, or -1
int jr_rom_find(uint32_t size, uint32_t crc);

// CRC-32 (zip's)
uint32_t jr_crc32(const uint8_t *p, uint32_t n, uint32_t crc);

// Lay out and decode the ROMs from the 13 files (file[i] is jr_rom_list[i])
void jr_roms_build(jr_roms_t *out, const uint8_t *const file[JR_NROMS]);
