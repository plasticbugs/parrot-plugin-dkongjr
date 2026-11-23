// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// jrdraw.h -- the board's picture into the same 224x256 upright pens as
// jr_render(), redrawing only what changed since the last one: the tiles
// that changed, and the tiles and sprites where sprites were and are. It
// says which 8x8 blocks it touched, so the scaling and the console's
// picture can redo only those.
#pragma once
#include <stdint.h>

#include "jr.h"

#define JRD_BW (JR_W / 8)       // 28 blocks across
#define JRD_BH (JR_H / 8)       // 32 down
extern uint8_t jrd_dirty[JRD_BH][JRD_BW];   // blocks that may have changed (the caller clears them)

// after jr_reset(): the next frame redraws everything
void jrd_init(void);

// Blocks x/8..(x+w)/8, y/8..(y+h)/8 that hud() clears: their tiles are drawn
// every frame, so hud() reads them as they are, and marked as changed only
// when they changed. (Kept across jrd_init().)
void jrd_keep(int x, int y, int w, int h);

// The tiles (only what changed, and under last frame's sprites), then
// hud(pens) (which may read the tiles and clear places), then the sprites.
// The same pens as jr_render_tiles(), hud(), jr_render_sprites().
void jrd_frame(uint8_t *pens, void (*hud)(uint8_t *pens));
