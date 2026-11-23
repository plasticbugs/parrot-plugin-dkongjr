// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// z80_core.h -- the Z80's instructions (z80.h says how to include it).
// The flags follow the chip, as MAME's z80 does: bits 3 and 5 (X, Y) of F
// from the result or operand the chip takes them from, and WZ (MEMPTR) for
// BIT n,(HL). SCF and CCF take X and Y from A alone.
#include "z80.h"

#define Z_CF 0x01
#define Z_NF 0x02
#define Z_PF 0x04
#define Z_XF 0x08
#define Z_HF 0x10
#define Z_YF 0x20
#define Z_ZF 0x40
#define Z_SF 0x80

static uint8_t z_sz[256], z_szp[256], z_szbit[256];

static void z80_tables(void) {
    for (int i = 0; i < 256; i++) {
        uint8_t f = (uint8_t)(i & (Z_SF | Z_YF | Z_XF));
        if (!i) f |= Z_ZF;
        z_sz[i] = f;
        int p = 0;
        for (int b = 0; b < 8; b++) p ^= i >> b & 1;
        z_szp[i] = (uint8_t)(f | (p ? 0 : Z_PF));
        z_szbit[i] = (uint8_t)((i ? i & Z_SF : Z_ZF | Z_PF) | (i & (Z_YF | Z_XF)));
    }
}

#define ZA  z->af.b.h
#define ZF  z->af.b.l
#define ZB  z->bc.b.h
#define ZC  z->bc.b.l
#define ZD  z->de.b.h
#define ZE  z->de.b.l
#define ZH  z->hl.b.h
#define ZL  z->hl.b.l
#define ZBC z->bc.w
#define ZDE z->de.w
#define ZHL z->hl.w
#define ZSP z->sp.w
#define ZPC z->pc.w
#define ZWZ z->wz.w

static inline uint8_t z_fetch(z80_t *z) { return Z80_RD(ZPC++); }
static inline uint16_t z_fetch16(z80_t *z) {
    uint16_t lo = Z80_RD(ZPC++);
    return (uint16_t)(lo | Z80_RD(ZPC++) << 8);
}
static inline uint16_t z_rd16(uint16_t a) { return (uint16_t)(Z80_RD(a) | Z80_RD((uint16_t)(a + 1)) << 8); }
static inline void z_wr16(uint16_t a, uint16_t v) { Z80_WR(a, (uint8_t)v); Z80_WR((uint16_t)(a + 1), (uint8_t)(v >> 8)); }
static inline void z_push(z80_t *z, uint16_t v) { ZSP -= 2; Z80_WR((uint16_t)(ZSP + 1), (uint8_t)(v >> 8)); Z80_WR(ZSP, (uint8_t)v); }
static inline uint16_t z_pop(z80_t *z) { uint16_t v = z_rd16(ZSP); ZSP += 2; return v; }
static inline void z_rinc(z80_t *z) { z->r = (uint8_t)((z->r + 1) & 0x7F); }

// ---- the arithmetic -----------------------------------------------------------------
static inline void z_add(z80_t *z, uint8_t v, int c) {
    unsigned r = (unsigned)ZA + v + (unsigned)c;
    ZF = (uint8_t)(z_sz[r & 0xFF] | (r >> 8 & Z_CF) | ((ZA ^ r ^ v) & Z_HF) | (((v ^ ZA ^ 0x80) & (v ^ r) & 0x80) >> 5));
    ZA = (uint8_t)r;
}
static inline uint8_t z_subf(z80_t *z, uint8_t v, int c) {
    unsigned r = (unsigned)ZA - v - (unsigned)c;
    ZF = (uint8_t)(z_sz[r & 0xFF] | (r >> 8 & Z_CF) | Z_NF | ((ZA ^ r ^ v) & Z_HF) | (((v ^ ZA) & (ZA ^ r) & 0x80) >> 5));
    return (uint8_t)r;
}
static inline void z_sub(z80_t *z, uint8_t v, int c) { ZA = z_subf(z, v, c); }
static inline void z_cp(z80_t *z, uint8_t v) {
    z_subf(z, v, 0);
    ZF = (uint8_t)((ZF & ~(Z_YF | Z_XF)) | (v & (Z_YF | Z_XF)));
}
static inline void z_and(z80_t *z, uint8_t v) { ZA &= v; ZF = (uint8_t)(z_szp[ZA] | Z_HF); }
static inline void z_or(z80_t *z, uint8_t v) { ZA |= v; ZF = z_szp[ZA]; }
static inline void z_xor(z80_t *z, uint8_t v) { ZA ^= v; ZF = z_szp[ZA]; }
static inline void z_alu(z80_t *z, int op, uint8_t v) {
    switch (op) {
    case 0: z_add(z, v, 0); break;
    case 1: z_add(z, v, ZF & Z_CF); break;
    case 2: z_sub(z, v, 0); break;
    case 3: z_sub(z, v, ZF & Z_CF); break;
    case 4: z_and(z, v); break;
    case 5: z_xor(z, v); break;
    case 6: z_or(z, v); break;
    default: z_cp(z, v); break;
    }
}
static inline uint8_t z_inc(z80_t *z, uint8_t v) {
    uint8_t r = (uint8_t)(v + 1);
    ZF = (uint8_t)((ZF & Z_CF) | z_sz[r] | (r == 0x80 ? Z_PF : 0) | ((r & 0x0F) ? 0 : Z_HF));
    return r;
}
static inline uint8_t z_dec(z80_t *z, uint8_t v) {
    uint8_t r = (uint8_t)(v - 1);
    ZF = (uint8_t)((ZF & Z_CF) | Z_NF | z_sz[r] | (r == 0x7F ? Z_PF : 0) | ((r & 0x0F) == 0x0F ? Z_HF : 0));
    return r;
}
static inline uint16_t z_add16(z80_t *z, uint16_t a, uint16_t b) {
    uint32_t r = (uint32_t)a + b;
    ZWZ = (uint16_t)(a + 1);
    ZF = (uint8_t)((ZF & (Z_SF | Z_ZF | Z_PF)) | (((a ^ r ^ b) >> 8) & Z_HF) | ((r >> 16) & Z_CF) | ((r >> 8) & (Z_YF | Z_XF)));
    return (uint16_t)r;
}
static inline void z_adc16(z80_t *z, uint16_t v) {
    uint32_t r = (uint32_t)ZHL + v + (ZF & Z_CF);
    ZWZ = (uint16_t)(ZHL + 1);
    ZF = (uint8_t)((((ZHL ^ r ^ v) >> 8) & Z_HF) | ((r >> 16) & Z_CF) | ((r >> 8) & (Z_SF | Z_YF | Z_XF)) |
                   ((r & 0xFFFF) ? 0 : Z_ZF) | (((v ^ ZHL ^ 0x8000) & (v ^ r) & 0x8000) >> 13));
    ZHL = (uint16_t)r;
}
static inline void z_sbc16(z80_t *z, uint16_t v) {
    uint32_t r = (uint32_t)ZHL - v - (ZF & Z_CF);
    ZWZ = (uint16_t)(ZHL + 1);
    ZF = (uint8_t)((((ZHL ^ r ^ v) >> 8) & Z_HF) | Z_NF | ((r >> 16) & Z_CF) | ((r >> 8) & (Z_SF | Z_YF | Z_XF)) |
                   ((r & 0xFFFF) ? 0 : Z_ZF) | (((v ^ ZHL) & (ZHL ^ r) & 0x8000) >> 13));
    ZHL = (uint16_t)r;
}
// the CB rotates and shifts: op 0 RLC 1 RRC 2 RL 3 RR 4 SLA 5 SRA 6 SLL 7 SRL
static inline uint8_t z_rot(z80_t *z, int op, uint8_t v) {
    uint8_t r, c;
    switch (op) {
    case 0: c = v >> 7; r = (uint8_t)(v << 1 | c); break;
    case 1: c = v & 1; r = (uint8_t)(v >> 1 | c << 7); break;
    case 2: c = v >> 7; r = (uint8_t)(v << 1 | (ZF & Z_CF)); break;
    case 3: c = v & 1; r = (uint8_t)(v >> 1 | (ZF & Z_CF) << 7); break;
    case 4: c = v >> 7; r = (uint8_t)(v << 1); break;
    case 5: c = v & 1; r = (uint8_t)(v >> 1 | (v & 0x80)); break;
    case 6: c = v >> 7; r = (uint8_t)(v << 1 | 1); break;
    default: c = v & 1; r = (uint8_t)(v >> 1); break;
    }
    ZF = (uint8_t)(z_szp[r] | c);
    return r;
}
static inline void z_bit(z80_t *z, int b, uint8_t v, uint8_t xy) {
    ZF = (uint8_t)((ZF & Z_CF) | Z_HF | (z_szbit[v & (1 << b)] & ~(Z_YF | Z_XF)) | (xy & (Z_YF | Z_XF)));
}
static inline bool z_cond(z80_t *z, int cc) {
    switch (cc) {
    case 0: return !(ZF & Z_ZF);
    case 1: return ZF & Z_ZF;
    case 2: return !(ZF & Z_CF);
    case 3: return ZF & Z_CF;
    case 4: return !(ZF & Z_PF);
    case 5: return ZF & Z_PF;
    case 6: return !(ZF & Z_SF);
    default: return ZF & Z_SF;
    }
}

// ---- the registers by their 3-bit number (6 is (HL), not here) ----------------------------
static inline uint8_t *z_reg8(z80_t *z, int r) {
    switch (r) {
    case 0: return &ZB;
    case 1: return &ZC;
    case 2: return &ZD;
    case 3: return &ZE;
    case 4: return &ZH;
    case 5: return &ZL;
    default: return &ZA;
    }
}

static void z80_reset(z80_t *z) {
    static bool tables;
    if (!tables) { z80_tables(); tables = true; }
    z->pc.w = 0;
    z->i = z->r = z->r7 = 0;
    z->iff1 = z->iff2 = 0;
    z->im = 0;
    z->halted = z->nmi_pending = z->ei_delay = false;
    z->stall = 0;
    z->af.w = 0xFFFF;                    // as MAME starts them
    z->sp.w = z->bc.w = z->de.w = z->hl.w = 0;
    z->ix.w = z->iy.w = 0xFFFF;
    z->af2.w = z->bc2.w = z->de2.w = z->hl2.w = 0;
    z->wz.w = 0;
    z->cycles = 0;
}

// an NMI's falling edge: taken before the next instruction
static inline void z80_nmi(z80_t *z) { z->nmi_pending = true; }

// ---- ED ----------------------------------------------------------------------------------
static int z_ed(z80_t *z) {
    uint8_t op = z_fetch(z);
    z_rinc(z);
    switch (op) {
    case 0x40: case 0x48: case 0x50: case 0x58: case 0x60: case 0x68: case 0x70: case 0x78: {   // IN r,(C)
        uint8_t v = Z80_IN(ZBC);
        ZWZ = (uint16_t)(ZBC + 1);
        if (op != 0x70) *z_reg8(z, op >> 3 & 7) = v;
        ZF = (uint8_t)((ZF & Z_CF) | z_szp[v]);
        return 12;
    }
    case 0x41: case 0x49: case 0x51: case 0x59: case 0x61: case 0x69: case 0x71: case 0x79:    // OUT (C),r
        Z80_OUT(ZBC, op == 0x71 ? 0 : *z_reg8(z, op >> 3 & 7));
        ZWZ = (uint16_t)(ZBC + 1);
        return 12;
    case 0x42: z_sbc16(z, ZBC); return 15;
    case 0x52: z_sbc16(z, ZDE); return 15;
    case 0x62: z_sbc16(z, ZHL); return 15;
    case 0x72: z_sbc16(z, ZSP); return 15;
    case 0x4A: z_adc16(z, ZBC); return 15;
    case 0x5A: z_adc16(z, ZDE); return 15;
    case 0x6A: z_adc16(z, ZHL); return 15;
    case 0x7A: z_adc16(z, ZSP); return 15;
    case 0x43: case 0x53: case 0x63: case 0x73: {      // LD (nn),rr
        uint16_t a = z_fetch16(z);
        uint16_t v = op == 0x43 ? ZBC : op == 0x53 ? ZDE : op == 0x63 ? ZHL : ZSP;
        z_wr16(a, v);
        ZWZ = (uint16_t)(a + 1);
        return 20;
    }
    case 0x4B: case 0x5B: case 0x6B: case 0x7B: {      // LD rr,(nn)
        uint16_t a = z_fetch16(z), v = z_rd16(a);
        ZWZ = (uint16_t)(a + 1);
        if (op == 0x4B) ZBC = v; else if (op == 0x5B) ZDE = v; else if (op == 0x6B) ZHL = v; else ZSP = v;
        return 20;
    }
    case 0x44: case 0x4C: case 0x54: case 0x5C: case 0x64: case 0x6C: case 0x74: case 0x7C: {   // NEG
        uint8_t v = ZA;
        ZA = 0;
        z_sub(z, v, 0);
        return 8;
    }
    case 0x45: case 0x55: case 0x65: case 0x75: case 0x4D: case 0x5D: case 0x6D: case 0x7D:     // RETN, RETI
        ZPC = z_pop(z);
        ZWZ = ZPC;
        z->iff1 = z->iff2;
        return 14;
    case 0x46: case 0x4E: case 0x66: case 0x6E: z->im = 0; return 8;
    case 0x56: case 0x76: z->im = 1; return 8;
    case 0x5E: case 0x7E: z->im = 2; return 8;
    case 0x47: z->i = ZA; return 9;
    case 0x4F: z->r = ZA & 0x7F; z->r7 = ZA & 0x80; return 9;
    case 0x57:
        ZA = z->i;
        ZF = (uint8_t)((ZF & Z_CF) | z_sz[ZA] | (z->iff2 ? Z_PF : 0));
        return 9;
    case 0x5F:
        ZA = (uint8_t)((z->r & 0x7F) | z->r7);
        ZF = (uint8_t)((ZF & Z_CF) | z_sz[ZA] | (z->iff2 ? Z_PF : 0));
        return 9;
    case 0x67: {                                       // RRD
        uint8_t m = Z80_RD(ZHL);
        ZWZ = (uint16_t)(ZHL + 1);
        Z80_WR(ZHL, (uint8_t)(ZA << 4 | m >> 4));
        ZA = (uint8_t)((ZA & 0xF0) | (m & 0x0F));
        ZF = (uint8_t)((ZF & Z_CF) | z_szp[ZA]);
        return 18;
    }
    case 0x6F: {                                       // RLD
        uint8_t m = Z80_RD(ZHL);
        ZWZ = (uint16_t)(ZHL + 1);
        Z80_WR(ZHL, (uint8_t)(m << 4 | (ZA & 0x0F)));
        ZA = (uint8_t)((ZA & 0xF0) | m >> 4);
        ZF = (uint8_t)((ZF & Z_CF) | z_szp[ZA]);
        return 18;
    }
    case 0xA0: case 0xA8: case 0xB0: case 0xB8: {     // LDI LDD LDIR LDDR
        uint8_t v = Z80_RD(ZHL);
        Z80_WR(ZDE, v);
        const int d = op & 8 ? -1 : 1;
        ZHL = (uint16_t)(ZHL + d);
        ZDE = (uint16_t)(ZDE + d);
        ZBC--;
        uint8_t n = (uint8_t)(v + ZA);
        ZF = (uint8_t)((ZF & (Z_SF | Z_ZF | Z_CF)) | (ZBC ? Z_PF : 0) | (n & Z_XF) | ((n << 4) & Z_YF));
        if (op & 0x10 && ZBC) {
            ZPC -= 2;
            ZWZ = (uint16_t)(ZPC + 1);
            ZF = (uint8_t)((ZF & ~(Z_YF | Z_XF)) | (z->pc.b.h & (Z_YF | Z_XF)));
            return 21;
        }
        return 16;
    }
    case 0xA1: case 0xA9: case 0xB1: case 0xB9: {     // CPI CPD CPIR CPDR
        uint8_t v = Z80_RD(ZHL);
        uint8_t r = (uint8_t)(ZA - v);
        const int d = op & 8 ? -1 : 1;
        ZWZ = (uint16_t)(ZWZ + d);
        ZHL = (uint16_t)(ZHL + d);
        ZBC--;
        ZF = (uint8_t)((ZF & Z_CF) | (z_sz[r] & ~(Z_YF | Z_XF)) | ((ZA ^ v ^ r) & Z_HF) | Z_NF | (ZBC ? Z_PF : 0));
        uint8_t n = (uint8_t)(r - (ZF & Z_HF ? 1 : 0));
        ZF = (uint8_t)(ZF | (n & Z_XF) | ((n << 4) & Z_YF));
        if (op & 0x10 && ZBC && r) {
            ZPC -= 2;
            ZWZ = (uint16_t)(ZPC + 1);
            ZF = (uint8_t)((ZF & ~(Z_YF | Z_XF)) | (z->pc.b.h & (Z_YF | Z_XF)));
            return 21;
        }
        return 16;
    }
    case 0xA2: case 0xAA: case 0xB2: case 0xBA: {     // INI IND INIR INDR
        uint8_t v = Z80_IN(ZBC);
        const int d = op & 8 ? -1 : 1;
        ZWZ = (uint16_t)(ZBC + d);
        ZB--;
        Z80_WR(ZHL, v);
        ZHL = (uint16_t)(ZHL + d);
        unsigned t = (unsigned)(uint8_t)(ZC + d) + v;
        ZF = (uint8_t)(z_sz[ZB] | (v & 0x80 ? Z_NF : 0) | (t & 0x100 ? Z_HF | Z_CF : 0) | (z_szp[(uint8_t)((t & 7) ^ ZB)] & Z_PF));
        if (op & 0x10 && ZB) { ZPC -= 2; return 21; }
        return 16;
    }
    case 0xA3: case 0xAB: case 0xB3: case 0xBB: {     // OUTI OUTD OTIR OTDR
        uint8_t v = Z80_RD(ZHL);
        ZB--;
        ZWZ = (uint16_t)(ZBC + (op & 8 ? -1 : 1));
        Z80_OUT(ZBC, v);
        ZHL = (uint16_t)(ZHL + (op & 8 ? -1 : 1));
        unsigned t = (unsigned)ZL + v;
        ZF = (uint8_t)(z_sz[ZB] | (v & 0x80 ? Z_NF : 0) | (t & 0x100 ? Z_HF | Z_CF : 0) | (z_szp[(uint8_t)((t & 7) ^ ZB)] & Z_PF));
        if (op & 0x10 && ZB) { ZPC -= 2; return 21; }
        return 16;
    }
    default: return 8;                                 // the rest are 8-cycle no-ops
    }
}

// ---- CB ----------------------------------------------------------------------------------
static int z_cb(z80_t *z) {
    uint8_t op = z_fetch(z);
    z_rinc(z);
    const int r = op & 7, b = op >> 3 & 7;
    if (r == 6) {
        uint8_t v = Z80_RD(ZHL);
        switch (op >> 6) {
        case 0: Z80_WR(ZHL, z_rot(z, b, v)); return 15;
        case 1: z_bit(z, b, v, z->wz.b.h); return 12;
        case 2: Z80_WR(ZHL, (uint8_t)(v & ~(1 << b))); return 15;
        default: Z80_WR(ZHL, (uint8_t)(v | 1 << b)); return 15;
        }
    }
    uint8_t *p = z_reg8(z, r);
    switch (op >> 6) {
    case 0: *p = z_rot(z, b, *p); break;
    case 1: z_bit(z, b, *p, *p); break;
    case 2: *p = (uint8_t)(*p & ~(1 << b)); break;
    default: *p = (uint8_t)(*p | 1 << b); break;
    }
    return 8;
}

// DD CB d op / FD CB d op
static int z_xycb(z80_t *z, uint16_t xy) {
    int8_t d = (int8_t)z_fetch(z);
    uint8_t op = z_fetch(z);
    uint16_t a = (uint16_t)(xy + d);
    ZWZ = a;
    uint8_t v = Z80_RD(a);
    const int r = op & 7, b = op >> 3 & 7;
    uint8_t res;
    switch (op >> 6) {
    case 0: res = z_rot(z, b, v); break;
    case 1: z_bit(z, b, v, (uint8_t)(a >> 8)); return 20;
    case 2: res = (uint8_t)(v & ~(1 << b)); break;
    default: res = (uint8_t)(v | 1 << b); break;
    }
    Z80_WR(a, res);
    if (r != 6) *z_reg8(z, r) = res;                   // the undocumented copy to a register
    return 23;
}

static int z_exec(z80_t *z, uint8_t op);

// DD or FD: the instruction after the prefix, with IX or IY for HL
static int z_xy(z80_t *z, z80_pair_t *xy) {
    uint8_t op = z_fetch(z);
    z_rinc(z);
    switch (op) {
    case 0x09: xy->w = z_add16(z, xy->w, ZBC); return 15;
    case 0x19: xy->w = z_add16(z, xy->w, ZDE); return 15;
    case 0x29: xy->w = z_add16(z, xy->w, xy->w); return 15;
    case 0x39: xy->w = z_add16(z, xy->w, ZSP); return 15;
    case 0x21: xy->w = z_fetch16(z); return 14;
    case 0x22: { uint16_t a = z_fetch16(z); z_wr16(a, xy->w); ZWZ = (uint16_t)(a + 1); return 20; }
    case 0x2A: { uint16_t a = z_fetch16(z); xy->w = z_rd16(a); ZWZ = (uint16_t)(a + 1); return 20; }
    case 0x23: xy->w++; return 10;
    case 0x2B: xy->w--; return 10;
    case 0x24: xy->b.h = z_inc(z, xy->b.h); return 8;
    case 0x25: xy->b.h = z_dec(z, xy->b.h); return 8;
    case 0x26: xy->b.h = z_fetch(z); return 11;
    case 0x2C: xy->b.l = z_inc(z, xy->b.l); return 8;
    case 0x2D: xy->b.l = z_dec(z, xy->b.l); return 8;
    case 0x2E: xy->b.l = z_fetch(z); return 11;
    case 0x34: { uint16_t a = (uint16_t)(xy->w + (int8_t)z_fetch(z)); ZWZ = a; Z80_WR(a, z_inc(z, Z80_RD(a))); return 23; }
    case 0x35: { uint16_t a = (uint16_t)(xy->w + (int8_t)z_fetch(z)); ZWZ = a; Z80_WR(a, z_dec(z, Z80_RD(a))); return 23; }
    case 0x36: { uint16_t a = (uint16_t)(xy->w + (int8_t)z_fetch(z)); ZWZ = a; Z80_WR(a, z_fetch(z)); return 19; }
    case 0xCB: return z_xycb(z, xy->w);
    case 0xE1: xy->w = z_pop(z); return 14;
    case 0xE5: z_push(z, xy->w); return 15;
    case 0xE3: { uint16_t v = z_rd16(ZSP); z_wr16(ZSP, xy->w); xy->w = v; ZWZ = v; return 23; }
    case 0xE9: ZPC = xy->w; return 8;
    case 0xF9: ZSP = xy->w; return 10;
    case 0xDD: case 0xFD: case 0xED: ZPC--; return 4;    // a prefix again: this one was a 4-cycle no-op
    default: break;
    }
    if (op >= 0x40 && op < 0xC0 && op != 0x76) {
        const int dst = op >> 3 & 7, src = op & 7;
        if (op < 0x80) {                               // LD r,r'
            if (src == 6) {                            // LD r,(XY+d): r is the real H or L
                uint16_t a = (uint16_t)(xy->w + (int8_t)z_fetch(z));
                ZWZ = a;
                *z_reg8(z, dst) = Z80_RD(a);
                return 19;
            }
            if (dst == 6) {
                uint16_t a = (uint16_t)(xy->w + (int8_t)z_fetch(z));
                ZWZ = a;
                Z80_WR(a, *z_reg8(z, src));
                return 19;
            }
            uint8_t v = src == 4 ? xy->b.h : src == 5 ? xy->b.l : *z_reg8(z, src);
            if (dst == 4) xy->b.h = v; else if (dst == 5) xy->b.l = v; else *z_reg8(z, dst) = v;
            return 8;
        }
        if (src == 6) {                                // ALU A,(XY+d)
            uint16_t a = (uint16_t)(xy->w + (int8_t)z_fetch(z));
            ZWZ = a;
            z_alu(z, dst, Z80_RD(a));
            return 19;
        }
        z_alu(z, dst, src == 4 ? xy->b.h : src == 5 ? xy->b.l : *z_reg8(z, src));
        return 8;
    }
    // the prefix does nothing to the rest: the plain instruction, 4 cycles later
    return 4 + z_exec(z, op);
}

// ---- one instruction (after its opcode fetch) -----------------------------------------------
static int z_exec(z80_t *z, uint8_t op) {
    switch (op) {
    case 0x00: return 4;
    case 0x01: ZBC = z_fetch16(z); return 10;
    case 0x11: ZDE = z_fetch16(z); return 10;
    case 0x21: ZHL = z_fetch16(z); return 10;
    case 0x31: ZSP = z_fetch16(z); return 10;
    case 0x02: Z80_WR(ZBC, ZA); z->wz.b.l = (uint8_t)(ZBC + 1); z->wz.b.h = ZA; return 7;
    case 0x12: Z80_WR(ZDE, ZA); z->wz.b.l = (uint8_t)(ZDE + 1); z->wz.b.h = ZA; return 7;
    case 0x0A: ZA = Z80_RD(ZBC); ZWZ = (uint16_t)(ZBC + 1); return 7;
    case 0x1A: ZA = Z80_RD(ZDE); ZWZ = (uint16_t)(ZDE + 1); return 7;
    case 0x22: { uint16_t a = z_fetch16(z); z_wr16(a, ZHL); ZWZ = (uint16_t)(a + 1); return 16; }
    case 0x2A: { uint16_t a = z_fetch16(z); ZHL = z_rd16(a); ZWZ = (uint16_t)(a + 1); return 16; }
    case 0x32: { uint16_t a = z_fetch16(z); Z80_WR(a, ZA); z->wz.b.l = (uint8_t)(a + 1); z->wz.b.h = ZA; return 13; }
    case 0x3A: { uint16_t a = z_fetch16(z); ZA = Z80_RD(a); ZWZ = (uint16_t)(a + 1); return 13; }
    case 0x03: ZBC++; return 6;
    case 0x13: ZDE++; return 6;
    case 0x23: ZHL++; return 6;
    case 0x33: ZSP++; return 6;
    case 0x0B: ZBC--; return 6;
    case 0x1B: ZDE--; return 6;
    case 0x2B: ZHL--; return 6;
    case 0x3B: ZSP--; return 6;
    case 0x09: ZHL = z_add16(z, ZHL, ZBC); return 11;
    case 0x19: ZHL = z_add16(z, ZHL, ZDE); return 11;
    case 0x29: ZHL = z_add16(z, ZHL, ZHL); return 11;
    case 0x39: ZHL = z_add16(z, ZHL, ZSP); return 11;
    case 0x04: ZB = z_inc(z, ZB); return 4;
    case 0x0C: ZC = z_inc(z, ZC); return 4;
    case 0x14: ZD = z_inc(z, ZD); return 4;
    case 0x1C: ZE = z_inc(z, ZE); return 4;
    case 0x24: ZH = z_inc(z, ZH); return 4;
    case 0x2C: ZL = z_inc(z, ZL); return 4;
    case 0x3C: ZA = z_inc(z, ZA); return 4;
    case 0x34: Z80_WR(ZHL, z_inc(z, Z80_RD(ZHL))); return 11;
    case 0x05: ZB = z_dec(z, ZB); return 4;
    case 0x0D: ZC = z_dec(z, ZC); return 4;
    case 0x15: ZD = z_dec(z, ZD); return 4;
    case 0x1D: ZE = z_dec(z, ZE); return 4;
    case 0x25: ZH = z_dec(z, ZH); return 4;
    case 0x2D: ZL = z_dec(z, ZL); return 4;
    case 0x3D: ZA = z_dec(z, ZA); return 4;
    case 0x35: Z80_WR(ZHL, z_dec(z, Z80_RD(ZHL))); return 11;
    case 0x06: ZB = z_fetch(z); return 7;
    case 0x0E: ZC = z_fetch(z); return 7;
    case 0x16: ZD = z_fetch(z); return 7;
    case 0x1E: ZE = z_fetch(z); return 7;
    case 0x26: ZH = z_fetch(z); return 7;
    case 0x2E: ZL = z_fetch(z); return 7;
    case 0x3E: ZA = z_fetch(z); return 7;
    case 0x36: Z80_WR(ZHL, z_fetch(z)); return 10;
    case 0x07:
        ZA = (uint8_t)(ZA << 1 | ZA >> 7);
        ZF = (uint8_t)((ZF & (Z_SF | Z_ZF | Z_PF)) | (ZA & (Z_YF | Z_XF | Z_CF)));
        return 4;
    case 0x0F:
        ZF = (uint8_t)((ZF & (Z_SF | Z_ZF | Z_PF)) | (ZA & Z_CF));
        ZA = (uint8_t)(ZA >> 1 | ZA << 7);
        ZF |= ZA & (Z_YF | Z_XF);
        return 4;
    case 0x17: {
        uint8_t r = (uint8_t)(ZA << 1 | (ZF & Z_CF));
        ZF = (uint8_t)((ZF & (Z_SF | Z_ZF | Z_PF)) | (ZA >> 7) | (r & (Z_YF | Z_XF)));
        ZA = r;
        return 4;
    }
    case 0x1F: {
        uint8_t r = (uint8_t)(ZA >> 1 | (ZF & Z_CF) << 7);
        ZF = (uint8_t)((ZF & (Z_SF | Z_ZF | Z_PF)) | (ZA & Z_CF) | (r & (Z_YF | Z_XF)));
        ZA = r;
        return 4;
    }
    case 0x08: { uint16_t t = z->af.w; z->af.w = z->af2.w; z->af2.w = t; return 4; }
    case 0x10:
        ZB--;
        if (ZB) { int8_t d = (int8_t)z_fetch(z); ZPC = (uint16_t)(ZPC + d); ZWZ = ZPC; return 13; }
        ZPC++;
        return 8;
    case 0x18: { int8_t d = (int8_t)z_fetch(z); ZPC = (uint16_t)(ZPC + d); ZWZ = ZPC; return 12; }
    case 0x20: case 0x28: case 0x30: case 0x38:
        if (z_cond(z, (op >> 3) & 3)) { int8_t d = (int8_t)z_fetch(z); ZPC = (uint16_t)(ZPC + d); ZWZ = ZPC; return 12; }
        ZPC++;
        return 7;
    case 0x27: {
        uint8_t a = ZA;
        if (ZF & Z_NF) {
            if ((ZF & Z_HF) || (ZA & 0x0F) > 9) a -= 6;
            if ((ZF & Z_CF) || ZA > 0x99) a -= 0x60;
        } else {
            if ((ZF & Z_HF) || (ZA & 0x0F) > 9) a += 6;
            if ((ZF & Z_CF) || ZA > 0x99) a += 0x60;
        }
        ZF = (uint8_t)((ZF & (Z_CF | Z_NF)) | (ZA > 0x99 ? Z_CF : 0) | ((ZA ^ a) & Z_HF) | z_szp[a]);
        ZA = a;
        return 4;
    }
    case 0x2F:
        ZA ^= 0xFF;
        ZF = (uint8_t)((ZF & (Z_SF | Z_ZF | Z_PF | Z_CF)) | Z_HF | Z_NF | (ZA & (Z_YF | Z_XF)));
        return 4;
    case 0x37:
        ZF = (uint8_t)((ZF & (Z_SF | Z_ZF | Z_PF)) | Z_CF | (ZA & (Z_YF | Z_XF)));
        return 4;
    case 0x3F:
        ZF = (uint8_t)(((ZF & (Z_SF | Z_ZF | Z_PF | Z_CF)) | ((ZF & Z_CF) << 4) | (ZA & (Z_YF | Z_XF))) ^ Z_CF);
        return 4;
    case 0x76:                                         // HALT: NOPs until an interrupt
        z->halted = true;
        ZPC--;
        return 4;
    case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8:
        if (z_cond(z, op >> 3 & 7)) { ZPC = z_pop(z); ZWZ = ZPC; return 11; }
        return 5;
    case 0xC9: ZPC = z_pop(z); ZWZ = ZPC; return 10;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA: {
        uint16_t a = z_fetch16(z);
        ZWZ = a;
        if (z_cond(z, op >> 3 & 7)) ZPC = a;
        return 10;
    }
    case 0xC3: ZPC = z_fetch16(z); ZWZ = ZPC; return 10;
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC: {
        uint16_t a = z_fetch16(z);
        ZWZ = a;
        if (z_cond(z, op >> 3 & 7)) { z_push(z, ZPC); ZPC = a; return 17; }
        return 10;
    }
    case 0xCD: { uint16_t a = z_fetch16(z); ZWZ = a; z_push(z, ZPC); ZPC = a; return 17; }
    case 0xC1: ZBC = z_pop(z); return 10;
    case 0xD1: ZDE = z_pop(z); return 10;
    case 0xE1: ZHL = z_pop(z); return 10;
    case 0xF1: z->af.w = z_pop(z); return 10;
    case 0xC5: z_push(z, ZBC); return 11;
    case 0xD5: z_push(z, ZDE); return 11;
    case 0xE5: z_push(z, ZHL); return 11;
    case 0xF5: z_push(z, z->af.w); return 11;
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        z_alu(z, op >> 3 & 7, z_fetch(z));
        return 7;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        z_push(z, ZPC);
        ZPC = op & 0x38;
        ZWZ = ZPC;
        return 11;
    case 0xCB: return z_cb(z);
    case 0xED: return z_ed(z);
    case 0xDD: return z_xy(z, &z->ix);
    case 0xFD: return z_xy(z, &z->iy);
    case 0xD3: {
        uint8_t n = z_fetch(z);
        Z80_OUT((uint16_t)(n | ZA << 8), ZA);
        z->wz.b.l = (uint8_t)(n + 1);
        z->wz.b.h = ZA;
        return 11;
    }
    case 0xDB: {
        uint8_t n = z_fetch(z);
        uint16_t port = (uint16_t)(n | ZA << 8);
        ZA = Z80_IN(port);
        ZWZ = (uint16_t)(port + 1);
        return 11;
    }
    case 0xD9: {
        uint16_t t;
        t = ZBC; ZBC = z->bc2.w; z->bc2.w = t;
        t = ZDE; ZDE = z->de2.w; z->de2.w = t;
        t = ZHL; ZHL = z->hl2.w; z->hl2.w = t;
        return 4;
    }
    case 0xE3: { uint16_t v = z_rd16(ZSP); z_wr16(ZSP, ZHL); ZHL = v; ZWZ = v; return 19; }
    case 0xE9: ZPC = ZHL; return 4;
    case 0xEB: { uint16_t t = ZDE; ZDE = ZHL; ZHL = t; return 4; }
    case 0xF3: z->iff1 = z->iff2 = 0; return 4;
    case 0xFB: z->iff1 = z->iff2 = 1; z->ei_delay = true; return 4;
    case 0xF9: ZSP = ZHL; return 6;
    default: break;
    }
    // 0x40-0xBF: LD r,r' and the ALU on registers
    const int dst = op >> 3 & 7, src = op & 7;
    uint8_t v = src == 6 ? Z80_RD(ZHL) : *z_reg8(z, src);
    if (op < 0x80) {
        if (dst == 6) { Z80_WR(ZHL, v); return 7; }
        *z_reg8(z, dst) = v;
        return src == 6 ? 7 : 4;
    }
    z_alu(z, dst, v);
    return src == 6 ? 7 : 4;
}

// Run until `budget` more cycles have gone: whole instructions, so a
// little past it. Returns the cycles run.
static int z80_run(z80_t *z, int budget) {
    int done = 0;
    const uint64_t start = z->cycles;
    while (done < budget) {
        int c;
        if (z->nmi_pending) {
            z->nmi_pending = false;
            if (z->halted) { z->halted = false; ZPC++; }
            z->iff2 = z->iff1;
            z->iff1 = 0;
            z_rinc(z);
            z_push(z, ZPC);
            ZPC = 0x66;
            ZWZ = ZPC;
            c = 11;
        } else if (z->irq_line && z->iff1 && !z->ei_delay) {
            if (z->halted) { z->halted = false; ZPC++; }
            z->iff1 = z->iff2 = 0;
            z_rinc(z);
            z_push(z, ZPC);
            if (z->im == 2) { ZPC = z_rd16((uint16_t)(z->i << 8 | z->irq_vector)); c = 19; }
            else { ZPC = 0x38; c = 13; }
            ZWZ = ZPC;
        } else {
            z->ei_delay = false;
#ifdef Z80_TRACE
            Z80_TRACE(z);
#endif
            uint8_t op = z_fetch(z);
            z_rinc(z);
            c = z_exec(z, op);
#ifdef Z80_AFTER
            Z80_AFTER(z, start + (uint64_t)(done + c));    // the instruction's end, in cycles
#endif
            if (z->stall) {
#ifdef Z80_STALL
                // the board works out a hold that depends on when it starts
                if (z->stall < 0) z->stall = Z80_STALL(z, start + (uint64_t)(done + c));
#endif
                c += z->stall;
                z->stall = 0;
            }
        }
        done += c;
#ifdef Z80_TRACE
        z->cycles = start + (uint64_t)done;    // as it goes, for the trace
#endif
    }
    z->cycles = start + (uint64_t)done;
    return done;
}
