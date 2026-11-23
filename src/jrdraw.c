// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// jrdraw.c -- see jrdraw.h. The rules are jr.c's jr_render_tiles() and
// jr_render_sprites(), which test/jr/jrlists checks this against.
#include "jrdraw.h"

#include <string.h>

uint8_t jrd_dirty[JRD_BH][JRD_BW];

// the tiles turned upright: [flipped][code][row], 2 bits a pixel, the first lowest
static uint16_t rot[2][512][8];
static uint32_t unpack[256];    // 4 pixels of 2 bits to 4 bytes
static uint16_t cell[32][32];   // what each tile cell shows: code | colour << 9
static uint8_t was_flip;
static bool valid;
static uint8_t spr_was[JRD_BH][JRD_BW], spr_now[JRD_BH][JRD_BW];   // blocks sprites drew in
static uint8_t keep[JRD_BH][JRD_BW];       // blocks hud() clears: drawn every frame

void jrd_keep(int x, int y, int w, int h) {
    for (int by = y / 8; by < (y + h + 7) / 8 && by < JRD_BH; by++)
        for (int bx = x / 8; bx < (x + w + 7) / 8 && bx < JRD_BW; bx++) keep[by][bx] = 1;
}

void jrd_init(void) {
    const jr_roms_t *r = jr_roms;
    for (int code = 0; code < 512; code++)
        for (int j = 0; j < 8; j++) {
            uint16_t a = 0, b = 0;
            for (int i = 0; i < 8; i++) {
                a |= (uint16_t)(r->tiles[code][i][7 - j] << 2 * i);
                b |= (uint16_t)(r->tiles[code][7 - i][j] << 2 * i);
            }
            rot[0][code][j] = a;
            rot[1][code][j] = b;
        }
    for (int v = 0; v < 256; v++) {
        uint8_t px[4];
        for (int i = 0; i < 4; i++) px[i] = (uint8_t)(v >> 2 * i & 3);
        memcpy(&unpack[v], px, 4);
    }
    valid = false;
    memset(spr_was, 0, sizeof spr_was);
}

static void tiles(uint8_t *pens) {
    const jr_roms_t *r = jr_roms;
    const int flip = jr.flip ? 1 : 0;
    const bool all = !valid || flip != was_flip;
    for (int row = 2; row < 30; row++)
        for (int col = 0; col < 32; col++) {
            const int code = jr.ram[0x1400 + row * 32 + col] + 256 * jr.gfx_bank;
            const int color = (r->prom[0x200 + col + 32 * (row / 4)] & 0x0F) + 16 * jr.palette_bank;
            const uint16_t key = (uint16_t)(code | color << 9);
            const int x0 = flip ? 232 - row * 8 : row * 8 - 16, y0 = flip ? col * 8 : 248 - col * 8;
            uint8_t *d = &jrd_dirty[y0 >> 3][x0 >> 3];
            if (!all && key == cell[row][col] && !*d) {
                if (!keep[y0 >> 3][x0 >> 3]) continue;
            } else {
                cell[row][col] = key;
                *d = 1;
            }
            const uint32_t c4 = (uint32_t)(color * 4) * 0x01010101u;
            const uint16_t *t = rot[flip][code];
            uint8_t *p = pens + y0 * JR_W + x0;
            for (int j = 0; j < 8; j++, p += JR_W) {
                const uint32_t a = unpack[t[j] & 0xFF] | c4, b = unpack[t[j] >> 8] | c4;
                memcpy(p, &a, 4);
                memcpy(p + 4, &b, 4);
            }
        }
    was_flip = (uint8_t)flip;
    valid = true;
}

static inline void put(uint8_t *pens, int rx, int line, uint8_t pen) {
    const int x = line - 16, y = 255 - rx;
    pens[y * JR_W + x] = pen;
    spr_now[y >> 3][x >> 3] = 1;
}

// as jr_render_sprites, a sprite at a time: each line takes the first 16
// sprites on it in RAM order, a later one over an earlier one
static void sprites(uint8_t *pens) {
    const jr_roms_t *r = jr_roms;
    const uint8_t *sr = &jr.ram[0x1000 + (jr.sprite_bank << 9)];
    const int add_y = jr.flip ? 0xF7 : 0xF9, add_x = 0xF7;
    uint8_t cnt[256];
    memset(cnt, 0, sizeof cnt);
    for (int offs = 0; offs < 0x200; offs += 4) {
        const int y = sr[offs];
        const int code = sr[offs + 1] & 0x7F;
        const int color = (sr[offs + 2] & 0x0F) + 16 * jr.palette_bank;
        int flipx = sr[offs + 2] & 0x80 ? 1 : 0;
        const int flipy = sr[offs + 1] & 0x80 ? 1 : 0;
        int x = (sr[offs + 3] + add_x + 1) & 0xFF;
        if (jr.flip) { x = (x ^ 0xFF) - 15; flipx = !flipx; }
        // the line of row 0: ((y + add_y + 1 + vf) & 0xFF) == 0xF0 + row, vf = (line - 1) & 0xFF (flipped, ^ 0xFF)
        const int line0 = jr.flip ? (y + add_y + 1 - 0xF0) & 0xFF : (0xF0 - y - add_y) & 0xFF;
        const int dl = jr.flip ? -1 : 1;
        // its 16 lines run on from lo (mod 256); none on the screen (16-239)
        // when all are in the 32 from 240 round to 15
        const int lo = jr.flip ? (line0 - 15) & 0xFF : line0;
        if (((lo - 240) & 0xFF) <= 16) continue;
        const bool inside = x >= 0 && x <= 240;    // no part off the side to wrap round
        for (int row = 0; row < 16; row++) {
            const int line = (line0 + row * dl) & 0xFF;
            if (line < 16 || line >= 240 || cnt[line] >= 16) continue;
            cnt[line]++;
            const int gy = flipy ? 15 - row : row;
            const uint8_t *g = r->sprites[code][gy];
            if (inside) {
                // the row is a column of the upright picture, from y = 255 - x upwards
                uint8_t *p = pens + (255 - x) * JR_W + (line - 16);
                const uint8_t c4 = (uint8_t)(color * 4);
                bool any = false;
                for (int tx = 0; tx < 16; tx++, p -= JR_W) {
                    const uint8_t v = g[flipx ? 15 - tx : tx];
                    if (v) { *p = (uint8_t)(c4 + v); any = true; }
                }
                if (any) {
                    const int bx = (line - 16) >> 3;
                    spr_now[(240 - x) >> 3][bx] = spr_now[(248 - x) >> 3][bx] = spr_now[(255 - x) >> 3][bx] = 1;
                }
                continue;
            }
            for (int tx = 0; tx < 16; tx++) {
                const uint8_t p = g[flipx ? 15 - tx : tx];
                if (!p) continue;
                const int px = x + tx;
                const uint8_t pen = (uint8_t)(color * 4 + p);
                if (px >= 0 && px < 256) put(pens, px, line, pen);
                if (px - 256 >= 0) put(pens, px - 256, line, pen);
                if (jr.flip && px + 256 < 256) put(pens, px + 256, line, pen);
            }
        }
    }
}

void jrd_frame(uint8_t *pens, void (*hud)(uint8_t *pens)) {
    // where sprites were: their tiles go back
    for (int i = 0; i < JRD_BH; i++)
        for (int j = 0; j < JRD_BW; j++) jrd_dirty[i][j] |= spr_was[i][j];
    tiles(pens);
    if (hud) hud(pens);
    memset(spr_now, 0, sizeof spr_now);
    sprites(pens);
    for (int i = 0; i < JRD_BH; i++)
        for (int j = 0; j < JRD_BW; j++) jrd_dirty[i][j] |= spr_now[i][j];
    memcpy(spr_was, spr_now, sizeof spr_was);
}
