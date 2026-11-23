// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// jrroms.c -- see jrroms.h
#include "jrroms.h"

#include <string.h>

const jr_rom_t jr_rom_list[JR_NROMS] = {
    {"djr1-c_5b_f-2.5b", 0xDEA28158, 0x2000},   // 0  main
    {"djr1-c_5c_f-2.5c", 0x6FB5FAF6, 0x2000},   // 1
    {"djr1-c_5e_f-2.5e", 0xD042B6A8, 0x2000},   // 2
    {"djr1-c_3h.3h", 0x715DA5F8, 0x1000},       // 3  the sound CPU's program
    {"djr1-v.3n", 0x8D51ACA9, 0x1000},          // 4  tiles
    {"djr1-v.3p", 0x4EF64BA5, 0x1000},          // 5
    {"djr1-v_7c.7c", 0xDC7F4164, 0x0800},       // 6  sprites
    {"djr1-v_7d.7d", 0x0CE7DCF6, 0x0800},       // 7
    {"djr1-v_7e.7e", 0x24D1FF17, 0x0800},       // 8
    {"djr1-v_7f.7f", 0x0F8C083F, 0x0800},       // 9
    {"djr1-c-2e.2e", 0x463DC7AD, 0x0100},       // 10 palette, low bits
    {"djr1-c-2f.2f", 0x47BA0042, 0x0100},       // 11 palette, high bits
    {"djr1-v-2n.2n", 0xDBF185BF, 0x0100},       // 12 the tiles' colour codes
};

int jr_rom_find(uint32_t size, uint32_t crc) {
    for (int i = 0; i < JR_NROMS; i++)
        if (jr_rom_list[i].size == size && jr_rom_list[i].crc == crc) return i;
    return -1;
}

uint32_t jr_crc32(const uint8_t *p, uint32_t n, uint32_t crc) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = crc >> 1 ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

void jr_roms_build(jr_roms_t *o, const uint8_t *const f[JR_NROMS]) {
    // the program, in the pieces MAME's ROM_CONTINUEs put where the board sees them
    static const struct { uint8_t file; uint16_t from, to, len; } piece[] = {
        {0, 0x0000, 0x0000, 0x1000}, {0, 0x1000, 0x3000, 0x1000},
        {1, 0x0000, 0x2000, 0x0800}, {1, 0x0800, 0x4800, 0x0800}, {1, 0x1000, 0x1000, 0x0800}, {1, 0x1800, 0x5800, 0x0800},
        {2, 0x0000, 0x4000, 0x0800}, {2, 0x0800, 0x2800, 0x0800}, {2, 0x1000, 0x5000, 0x0800}, {2, 0x1800, 0x1800, 0x0800},
    };
    for (unsigned i = 0; i < sizeof piece / sizeof piece[0]; i++)
        memcpy(o->main + piece[i].to, f[piece[i].file] + piece[i].from, piece[i].len);
    memcpy(o->snd, f[3], 0x1000);
    // tiles: two planes, 3P the high bit and 3N the low, a byte a row, bit 7 at the left
    for (int t = 0; t < 512; t++)
        for (int y = 0; y < 8; y++) {
            const uint8_t lo = f[4][t * 8 + y], hi = f[5][t * 8 + y];
            for (int x = 0; x < 8; x++)
                o->tiles[t][y][x] = (uint8_t)((hi >> (7 - x) & 1) << 1 | (lo >> (7 - x) & 1));
        }
    // sprites: 7E/7F the high bit, 7C/7D the low; 7C and 7E the left 8 pixels, 7D and 7F the right
    for (int s = 0; s < 128; s++)
        for (int y = 0; y < 16; y++)
            for (int half = 0; half < 2; half++) {
                const uint8_t lo = f[6 + half][s * 16 + y], hi = f[8 + half][s * 16 + y];
                for (int x = 0; x < 8; x++)
                    o->sprites[s][y][half * 8 + x] = (uint8_t)((hi >> (7 - x) & 1) << 1 | (lo >> (7 - x) & 1));
            }
    memcpy(o->prom, f[10], 0x100);
    memcpy(o->prom + 0x100, f[11], 0x100);
    memcpy(o->prom + 0x200, f[12], 0x100);
}
