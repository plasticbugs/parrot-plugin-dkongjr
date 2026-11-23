// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// jr.c -- the Donkey Kong Jr. board (jr.h). Follows MAME 0.288's
// nintendo/dkong.cpp, dkong_a.cpp and dkong_v.cpp (BSD-3-Clause, by
// Couriersud and the MAME team) for what the board does; the code is ours.
#include "jr.h"

#include <string.h>

jr_board_t jr;
const jr_roms_t *jr_roms;
void (*jr_vblank)(void);

// The sprite copy's hold on the Z80. The 8257 needs 3,082 cycles from the
// end of the instruction that starts it (385 bytes, two transfers of 4
// cycles each, and 2 more), but MAME lets the Z80 go again only at the end
// of a scheduler slice: 60 kHz slices from each line's start, moved by the
// 8035's port writes. Measured over 6,000 frames of play, it lets go at
// 3,507 or 3,553 cycles after vertical blank 87% of the time (3,558 the
// rest). The rule below gives those two; jr_dma_hold, if set, decides
// instead (the Mac tests replay MAME's own holds through it).
#define JR_FRAME_CYCLES (JR_LINES * JR_Z80_LINE)
#define DMA_CYCLES 3082
int (*jr_dma_hold)(uint64_t end);

static int dma_stall(uint64_t end) {
    if (jr_dma_hold) return jr_dma_hold(end);
    const uint64_t done = end + DMA_CYCLES;
    const uint32_t pos = (uint32_t)(done % JR_FRAME_CYCLES);
    const uint32_t go = pos <= 3507 ? 3507 : pos <= 3553 ? 3553 : pos;
    return (int)(done - pos + go - end);
}
#define Z80_STALL(z, end) dma_stall(end)

static void dma_go(void);

// ---- the Z80's bus ---------------------------------------------------------------------
static inline uint8_t Z80_RD(uint16_t a) {
    if (a < 0x6000) return jr_roms->main[a];
    if (a < 0x7800) return jr.ram[a - 0x6000];
    switch (a) {
    case 0x7C00: return jr.in0;
    case 0x7C80: return jr.in1;
    case 0x7D00: return jr.in2;     // (and the watchdog's reset)
    case 0x7D80: return jr.dsw0;
    default: break;
    }
    if (a >= 0x7800 && a < 0x7810) {
        if ((a & 15) == 8) { uint8_t s = jr.dma_status; jr.dma_status &= 0xF0; return s; }
        const int ch = (a >> 1) & 3;
        uint16_t v = a & 1 ? jr.dma_count[ch] : jr.dma_addr[ch];
        uint8_t b = (uint8_t)(jr.dma_msb ? v >> 8 : v);
        jr.dma_msb = !jr.dma_msb;
        return b;
    }
    return 0;
}

static inline void Z80_WR(uint16_t a, uint8_t v) {
    if (a < 0x6000) return;
    if (a < 0x7800) { jr.ram[a - 0x6000] = v; return; }
    if (a < 0x7810) {
        if ((a & 15) == 8) { jr.dma_mode = v; jr.dma_msb = false; return; }
        if (a & 8) return;
        const int ch = (a >> 1) & 3;
        uint16_t *r = a & 1 ? &jr.dma_count[ch] : &jr.dma_addr[ch];
        *r = jr.dma_msb ? (uint16_t)((*r & 0x00FF) | v << 8) : (uint16_t)((*r & 0xFF00) | v);
        jr.dma_msb = !jr.dma_msb;
        return;
    }
    // what the 8035 can see waits for the instruction's end (latch_sync)
    if (a == 0x7C00 || (a >= 0x7C81 && a <= 0x7C87) || (a >= 0x7D00 && a <= 0x7D07) || a == 0x7D80 || a == 0x7D81) {
        if (jr.npend < 4) { jr.pend_a[jr.npend] = a; jr.pend_v[jr.npend] = v; jr.npend++; }
        return;
    }
    const uint8_t on = v & 1;
    switch (a) {
    case 0x7C80: jr.gfx_bank = on; return;
    case 0x7D82: jr.flip = on; return;
    case 0x7D83: jr.sprite_bank = on; return;
    case 0x7D84: jr.nmi_mask = on; return;
    case 0x7D85: if (on) dma_go(); return;
    case 0x7D86: case 0x7D87:
        jr.palette_bank = (uint8_t)(on ? jr.palette_bank | 1 << (a - 0x7D86) : jr.palette_bank & ~(1 << (a - 0x7D86)));
        return;
    default: break;
    }
}

// The latches the 8035 reads. MAME synchronizes each write: the Z80 stops
// at the instruction's end, the 8035 catches up to that moment and only
// then is the latch written. So the 8035's instructions that start before
// 8035 cycle N (that moment, rounded down) see the old value and the rest
// the new. In attoseconds a Z80 cycle is a hair short, so a moment on an
// exact 8035 cycle (every 192 Z80 cycles) rounds down to the one before.
static inline uint64_t m48_cycle_at(uint64_t z80) {
    return z80 * 25 / 192 - (z80 % 192 == 0 && z80 ? 1 : 0);
}
static void m48_upto(uint64_t n);
static void latch_sync(uint64_t end) {
    m48_upto(m48_cycle_at(end));
    for (int i = 0; i < jr.npend; i++) {
        const uint16_t a = jr.pend_a[i];
        const uint8_t v = jr.pend_v[i], bit = (uint8_t)(1 << (a & 7)), on = v & 1;
        if (a == 0x7C00) jr.tune = v;
        else if (a == 0x7D80) { jr.snd_irq = v != 0; jr.snd.irq_line = jr.snd_irq; }
        else if (a >= 0x7C81 && a <= 0x7C87) jr.l4h = (uint8_t)(on ? jr.l4h | bit : jr.l4h & ~bit);
        else if (a >= 0x7D00 && a <= 0x7D07) jr.l6h = (uint8_t)(on ? jr.l6h | bit : jr.l6h & ~bit);
        else if (a == 0x7D81) jr.l5h = (uint8_t)(on ? jr.l5h | bit : jr.l5h & ~bit);
    }
    jr.npend = 0;
}
#define Z80_AFTER(z, end) do { if (jr.npend) latch_sync(end); } while (0)

static inline uint8_t Z80_IN(uint16_t port) { (void)port; return 0; }
static inline void Z80_OUT(uint16_t port, uint8_t v) { (void)port; (void)v; }

#include "z80_core.h"

// The sprite copy: channel 0 reads the work RAM at $6900, channel 1 writes
// the sprite RAM at $7000, a byte for each count + 1, while the Z80 waits.
static void dma_go(void) {
    if ((jr.dma_mode & 3) != 3) return;
    const unsigned n = (jr.dma_count[0] & 0x3FFF) + 1u;
    uint16_t src = jr.dma_addr[0], dst = jr.dma_addr[1];
    for (unsigned i = 0; i < n; i++) Z80_WR((uint16_t)(dst + i), Z80_RD((uint16_t)(src + i)));
    jr.dma_addr[0] = (uint16_t)(src + n);
    jr.dma_addr[1] = (uint16_t)(dst + n);
    jr.dma_count[0] = (uint16_t)(jr.dma_count[0] & 0xC000) | 0x3FFF;
    jr.dma_count[1] = (uint16_t)(jr.dma_count[1] & 0xC000) | 0x3FFF;
    jr.dma_status |= 3;
    if (jr.dma_mode & 0x40) jr.dma_mode &= (uint8_t)~3;     // TC STOP
    jr.cpu.stall = -1;          // dma_stall() works it out at the instruction's end
}

// ---- the 8035's pins --------------------------------------------------------------------
static inline uint8_t M48_ROM(uint16_t a) { return jr_roms->snd[a & 0xFFF]; }
static inline uint8_t M48_PORT_R(int p) {
    if (p == 1) return 0xFF;
    // virtual P2: bit 6 from 4H bit 1, 5 from 6H bit 3, 4 from 6H bit 6, the rest as written; XOR $70
    uint8_t v = (uint8_t)(jr.vp2 & 0x8F);
    if (jr.l4h & 0x02) v |= 0x40;
    if (jr.l6h & 0x08) v |= 0x20;
    if (jr.l6h & 0x40) v |= 0x10;
    return v ^ 0x70;
}
static inline void dac_change(uint8_t v) {
    const uint64_t now = jr.snd.cycles;
    jr.dac_sum += (uint32_t)(jr.dac * (now - jr.dac_since));
    jr.dac_since = now;
    jr.dac = v;
}
static inline void M48_PORT_W(int p, uint8_t v) {
    if (p == 1) dac_change(v);
    else jr.vp2 = v;
}
static inline uint8_t M48_EXT_R(uint8_t a) { (void)a; return jr.tune & 0x1F; }
static inline void M48_EXT_W(uint8_t a, uint8_t v) { (void)a; (void)v; }
static inline uint8_t M48_BUS_R(void) { return 0xFF; }
static inline void M48_BUS_W(uint8_t v) { (void)v; }
static inline int M48_T0(void) { return !(jr.l6h & 0x20); }
static inline int M48_T1(void) { return !(jr.l6h & 0x10); }

#include "mcs48_core.h"

// run the 8035 until it has started every instruction before cycle n
static void m48_upto(uint64_t n) {
    if (jr.snd.cycles < n) m48_run(&jr.snd, (int)(n - jr.snd.cycles));
}

// ---- time ----------------------------------------------------------------------------------
void jr_reset(const jr_roms_t *roms) {
    jr_roms = roms;
    memset(&jr, 0, sizeof jr);
    z80_reset(&jr.cpu);
    m48_reset(&jr.snd);
    jr.vp2 = 0xFF;              // the 8035's reset writes its ports
    jr.in2 = 0x40;              // the MCU line, not connected on Donkey Kong Jr.: high
    jr.dsw0 = 0x80;             // upright, 3 lives, bonus at 10000, 1 coin 1 credit
    jr.dma_status = 0;
    // MAME's screen starts where vertical blank begins, so the first
    // frame's end is a whole frame (50,688 cycles) after the reset
    jr.line = JR_VBLANK_LINE;
    jr.started = false;
}

void jr_line(jr_sline_t *s) {
    if (jr.line == JR_VBLANK_LINE && jr.started) {
        jr.frame++;
        if (jr_vblank) jr_vblank();
        if (jr.nmi_mask) z80_nmi(&jr.cpu);
    }
    jr.z80_target += JR_Z80_LINE;
    // MAME's vertical blank comes a hair (attoseconds) before the Z80's
    // cycle 50,688 x n, so a Z80 at exactly one cycle before it doesn't
    // start another instruction and takes the NMI first
    const int64_t z = jr.z80_target - (jr.line == JR_VBLANK_LINE - 1) - (int64_t)jr.cpu.cycles;
    if (z > 0) z80_run(&jr.cpu, (int)z);
    // the 8035 to the line's end (where MAME's line timer stops it), with
    // the DAC's level added up over the line
    m48_upto(m48_cycle_at((uint64_t)jr.z80_target));
    {
        const uint64_t now = jr.snd.cycles, start = jr.line_m48;
        jr.dac_sum += (uint32_t)(jr.dac * (now - jr.dac_since));
        const uint32_t ran = (uint32_t)(now - start);
        if (s) s->dac = (uint16_t)(ran ? (jr.dac_sum * 256u + ran / 2) / ran : jr.dac * 256u);
        jr.dac_sum = 0;
        jr.dac_since = jr.line_m48 = now;
    }
    if (s) {
        uint8_t b = 0;
        if (jr.l6h & 0x01) b |= JR_S_WALK;
        if (jr.l6h & 0x02) b |= JR_S_JUMP;
        if (jr.l6h & 0x04) b |= JR_S_CLIMB;
        if (jr.l6h & 0x80) b |= JR_S_SND7;
        if (jr.l5h & 0x02) b |= JR_S_FALL;
        if (jr.vp2 & 0x80) b |= JR_S_DECAY;
        s->bits = b;
    }
    jr.started = true;
    if (++jr.line == JR_LINES) jr.line = 0;
}

// ---- the picture ---------------------------------------------------------------------------
// MAME draws the board's picture as 256 pixels along each of lines 16-239
// and turns it upright (ROT270): upright x = line - 16, y = 255 - pixel.
static inline void put(uint8_t *pens, int rx, int line, uint8_t pen) {
    const int x = line - 16, y = 255 - rx;
    if (x >= 0 && x < JR_W && y >= 0 && y < JR_H) pens[y * JR_W + x] = pen;
}

void jr_render(uint8_t *pens) {
    const jr_roms_t *r = jr_roms;
    // the tiles: 32 x 32 of them, 8x8, the row's colour from the 2N PROM
    for (int row = 2; row < 30; row++)
        for (int col = 0; col < 32; col++) {
            const int idx = row * 32 + col;
            const int code = jr.ram[0x1400 + idx] + 256 * jr.gfx_bank;
            const int color = (r->prom[0x200 + col + 32 * (row / 4)] & 0x0F) + 16 * jr.palette_bank;
            for (int ty = 0; ty < 8; ty++)
                for (int tx = 0; tx < 8; tx++) {
                    // flipped, the whole 256x256 map turns half a circle
                    const int rx = jr.flip ? 255 - (col * 8 + tx) : col * 8 + tx;
                    const int ry = jr.flip ? 255 - (row * 8 + ty) : row * 8 + ty;
                    put(pens, rx, ry, (uint8_t)(color * 4 + r->tiles[code][ty][tx]));
                }
        }
    // the sprites, line by line as the board finds them: at most 16 a line,
    // in RAM order, a later one over an earlier one
    const uint8_t *sr = &jr.ram[0x1000 + (jr.sprite_bank << 9)];
    const int add_y = jr.flip ? 0xF7 : 0xF9, add_x = 0xF7;
    for (int line = 16; line < 240; line++) {
        const int scan = line & 0xFF;
        int vf = (line - 1) & 0xFF;
        if (jr.flip) vf ^= 0xFF;
        int n = 0;
        for (int offs = 0; offs < 0x200 && n < 16; offs += 4) {
            const int y = sr[offs];
            if (((y + add_y + 1 + vf) & 0xF0) != 0xF0) continue;
            n++;
            const int code = sr[offs + 1] & 0x7F;     // (+ $80 for bit 6 of the colour byte: there are only 128)
            const int color = (sr[offs + 2] & 0x0F) + 16 * jr.palette_bank;
            int flipx = sr[offs + 2] & 0x80 ? 1 : 0;
            const int flipy = sr[offs + 1] & 0x80 ? 1 : 0;
            int x = (sr[offs + 3] + add_x + 1) & 0xFF;
            if (jr.flip) { x = (x ^ 0xFF) - 15; flipx = !flipx; }
            const int row = (y + add_y + 1 + vf) & 0x0F;  // the sprite's row on this line
            const int gy = flipy ? 15 - row : row;
            for (int tx = 0; tx < 16; tx++) {
                const uint8_t p = r->sprites[code][gy][flipx ? 15 - tx : tx];
                if (!p) continue;
                const int px = x + tx;
                const uint8_t pen = (uint8_t)(color * 4 + p);
                if (px >= 0 && px < 256) put(pens, px, scan, pen);
                if (px - 256 >= 0) put(pens, px - 256, scan, pen);
                if (jr.flip && px + 256 < 256) put(pens, px + 256, scan, pen);
            }
        }
    }
}

// The palette: each PROM bit drives a resistor and the monitor inverts, so
// a 0 is bright. Levels measured from MAME's dkong2b_palette (they vary by a
// few counts there with the other channels; this is near the middle).
void jr_palette(uint32_t rgb[256]) {
    static const uint8_t R[8] = {245, 210, 177, 151, 113, 85, 56, 11};
    static const uint8_t G[8] = {245, 218, 185, 156, 116, 84, 45, 6};
    static const uint8_t B[4] = {255, 240, 150, 17};
    const uint8_t *p = jr_roms->prom;
    for (int i = 0; i < 256; i++) {
        if ((i & 3) == 0) { rgb[i] = 0; continue; }       // the PROMs off: black
        const uint8_t hi = p[0x100 + i], lo = p[i];
        const int r = hi >> 1 & 7, g = (hi << 2 & 4) | (lo >> 2 & 3), b = lo & 3;
        rgb[i] = (uint32_t)R[r] << 16 | (uint32_t)G[g] << 8 | B[b];
    }
}
