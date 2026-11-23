// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// mcs48_core.h -- the 8035's instructions (mcs48.h says how to include it).
// Following the 8048 family's datasheet, with MAME's mcs48 for the details
// it leaves open: a port read is the pins ANDed with the port's latch, the
// program counter's low 11 bits count and bit 11 changes only by a jump, a
// call or a return, and a jump inside an interrupt keeps A11 at 0.
#include "mcs48.h"

#define M48_CY 0x80
#define M48_AC 0x40
#define M48_F0 0x20
#define M48_BS 0x10

static inline uint8_t *m48_r(m48_t *m, int r) { return &m->ram[(m->psw & M48_BS ? 24 : 0) + r]; }

static void m48_reset(m48_t *m) {
    m->pc = 0;
    m->psw = 0x08;
    m->a = 0;
    m->f1 = 0;
    m->a11 = 0;
    m->p1 = m->p2 = 0xFF;
    m->timer = m->prescaler = 0;
    m->timer_on = m->counter_on = false;
    m->timer_flag = m->timer_irq = false;
    m->tirq_enabled = m->xirq_enabled = false;
    m->irq_in_progress = false;
    m->t1_last = 0;
    m->cycles = 0;
}

static inline uint8_t m48_fetch(m48_t *m) {
    uint8_t v = M48_ROM(m->pc);
    m->pc = (uint16_t)(((m->pc + 1) & 0x7FF) | (m->pc & 0x800));
    return v;
}

static inline void m48_push(m48_t *m) {
    int sp = m->psw & 7;
    m->ram[8 + 2 * sp] = (uint8_t)m->pc;
    m->ram[9 + 2 * sp] = (uint8_t)(((m->pc >> 8) & 0x0F) | (m->psw & 0xF0));
    m->psw = (uint8_t)((m->psw & 0xF8) | ((sp + 1) & 7));
}

static inline void m48_pull(m48_t *m, bool psw) {
    int sp = (m->psw - 1) & 7;
    m->psw = (uint8_t)((m->psw & 0xF8) | sp);
    uint8_t lo = m->ram[8 + 2 * sp], hi = m->ram[9 + 2 * sp];
    m->pc = (uint16_t)(lo | (hi & 0x0F) << 8);
    if (psw) m->psw = (uint8_t)((m->psw & 0x0F) | (hi & 0xF0));
}

static inline void m48_jmp(m48_t *m, uint16_t addr) {
    m->pc = (uint16_t)(addr | (m->irq_in_progress ? 0 : m->a11 << 11));
}

static inline void m48_jcc(m48_t *m, bool cond) {
    uint16_t page = m->pc & 0xF00;
    uint8_t off = m48_fetch(m);
    if (cond) m->pc = (uint16_t)(page | off);
}

static inline void m48_add(m48_t *m, uint8_t v, int c) {
    unsigned t = (unsigned)m->a + v + (unsigned)c;
    unsigned t4 = (unsigned)(m->a & 15) + (v & 15) + (unsigned)c;
    m->psw = (uint8_t)((m->psw & ~(M48_CY | M48_AC)) | ((t4 << 2) & M48_AC) | ((t >> 1) & M48_CY));
    m->a = (uint8_t)t;
}

// the timer and the counter, after `n` machine cycles
static inline void m48_tick(m48_t *m, int n) {
    if (m->timer_on) {
        unsigned p = m->prescaler + (unsigned)n;
        unsigned inc = p >> 5;
        m->prescaler = (uint8_t)(p & 31);
        if (inc) {
            unsigned t = m->timer + inc;
            if (t > 0xFF) { m->timer_flag = true; if (m->tirq_enabled) m->timer_irq = true; }
            m->timer = (uint8_t)t;
        }
    } else if (m->counter_on) {
        uint8_t t1 = (uint8_t)(M48_T1() ? 1 : 0);
        if (m->t1_last && !t1) {
            if (++m->timer == 0) { m->timer_flag = true; if (m->tirq_enabled) m->timer_irq = true; }
        }
        m->t1_last = t1;
    }
}

// machine cycles per opcode (from the cases below)
static const uint8_t m48_cycles[256] = {
    1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 2, 2, 2, 2,
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 1, 2, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2, 2,
    1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 1, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,
    1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 1, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 2, 1, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

static int m48_step(m48_t *m) {
    if (!m->irq_in_progress) {
        uint16_t vec = 0;
        if (m->irq_line && m->xirq_enabled) vec = 3;
        else if (m->timer_irq && m->tirq_enabled) { vec = 7; m->timer_irq = false; }
        if (vec) {
            m->irq_in_progress = true;
            m48_push(m);
            m->pc = vec;
            m48_tick(m, 2);
            m->cycles += 2;
            return 2;
        }
    }
#ifdef M48_TRACE
    M48_TRACE(m);
#endif
    uint8_t op = m48_fetch(m);
    // As MAME does it: the instruction's cycles (and the timer) go by
    // before it acts, so JTF and MOV A,T see them, and a port write is
    // timed at the instruction's end
    int c = m48_cycles[op];
    m48_tick(m, c);
    m->cycles += (uint64_t)c;
    switch (op) {
    case 0x00: break;
    case 0x02: M48_BUS_W(m->a); c = 2; break;
    case 0x03: m48_add(m, m48_fetch(m), 0); c = 2; break;
    case 0x04: case 0x24: case 0x44: case 0x64: case 0x84: case 0xA4: case 0xC4: case 0xE4: {
        uint8_t lo = m48_fetch(m);
        m48_jmp(m, (uint16_t)((op & 0xE0) << 3 | lo));
        c = 2;
        break;
    }
    case 0x05: m->xirq_enabled = true; break;
    case 0x07: m->a--; break;
    case 0x08: m->a = M48_BUS_R(); c = 2; break;
    case 0x09: m->a = (uint8_t)(M48_PORT_R(1) & m->p1); c = 2; break;
    case 0x0A: m->a = (uint8_t)(M48_PORT_R(2) & m->p2); c = 2; break;
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: m->a = 0; c = 2; break;   // MOVD A,Pp: no 8243 here
    case 0x10: case 0x11: (*&m->ram[*m48_r(m, op & 1) & 63])++; break;
    case 0x12: case 0x32: case 0x52: case 0x72: case 0x92: case 0xB2: case 0xD2: case 0xF2:
        m48_jcc(m, m->a >> (op >> 5) & 1);
        c = 2;
        break;
    case 0x13: m48_add(m, m48_fetch(m), m->psw >> 7); c = 2; break;
    case 0x14: case 0x34: case 0x54: case 0x74: case 0x94: case 0xB4: case 0xD4: case 0xF4: {
        uint8_t lo = m48_fetch(m);
        m48_push(m);
        m48_jmp(m, (uint16_t)((op & 0xE0) << 3 | lo));
        c = 2;
        break;
    }
    case 0x15: m->xirq_enabled = false; break;
    case 0x16: { bool tf = m->timer_flag; m->timer_flag = false; m48_jcc(m, tf); c = 2; break; }
    case 0x17: m->a++; break;
    case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F: (*m48_r(m, op & 7))++; break;
    case 0x20: case 0x21: { uint8_t *p = &m->ram[*m48_r(m, op & 1) & 63]; uint8_t t = *p; *p = m->a; m->a = t; break; }
    case 0x23: m->a = m48_fetch(m); c = 2; break;
    case 0x25: m->tirq_enabled = true; break;
    case 0x26: m48_jcc(m, !M48_T0()); c = 2; break;
    case 0x27: m->a = 0; break;
    case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F: {
        uint8_t *p = m48_r(m, op & 7), t = *p;
        *p = m->a;
        m->a = t;
        break;
    }
    case 0x30: case 0x31: {
        uint8_t *p = &m->ram[*m48_r(m, op & 1) & 63], t = *p;
        *p = (uint8_t)((t & 0xF0) | (m->a & 0x0F));
        m->a = (uint8_t)((m->a & 0xF0) | (t & 0x0F));
        break;
    }
    case 0x35: m->tirq_enabled = false; m->timer_irq = false; break;
    case 0x36: m48_jcc(m, M48_T0()); c = 2; break;
    case 0x37: m->a ^= 0xFF; break;
    case 0x39: m->p1 = m->a; M48_PORT_W(1, m->p1); c = 2; break;
    case 0x3A: m->p2 = m->a; M48_PORT_W(2, m->p2); c = 2; break;
    case 0x3C: case 0x3D: case 0x3E: case 0x3F: c = 2; break;           // MOVD Pp,A
    case 0x40: case 0x41: m->a |= m->ram[*m48_r(m, op & 1) & 63]; break;
    case 0x42: m->a = m->timer; break;
    case 0x43: m->a |= m48_fetch(m); c = 2; break;
    case 0x45: m->counter_on = true; m->timer_on = false; m->t1_last = (uint8_t)(M48_T1() ? 1 : 0); break;
    case 0x46: m48_jcc(m, !M48_T1()); c = 2; break;
    case 0x47: m->a = (uint8_t)(m->a << 4 | m->a >> 4); break;
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: m->a |= *m48_r(m, op & 7); break;
    case 0x50: case 0x51: m->a &= m->ram[*m48_r(m, op & 1) & 63]; break;
    case 0x53: m->a &= m48_fetch(m); c = 2; break;
    case 0x55: m->timer_on = true; m->counter_on = false; m->prescaler = 0; break;
    case 0x56: m48_jcc(m, M48_T1()); c = 2; break;
    case 0x57:
        if ((m->a & 0x0F) > 0x09 || (m->psw & M48_AC)) {
            if (m->a > 0xF9) m->psw |= M48_CY;
            m->a += 0x06;
        }
        if ((m->a & 0xF0) > 0x90 || (m->psw & M48_CY)) {
            m->a += 0x60;
            m->psw |= M48_CY;
        }
        break;
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: m->a &= *m48_r(m, op & 7); break;
    case 0x60: case 0x61: m48_add(m, m->ram[*m48_r(m, op & 1) & 63], 0); break;
    case 0x62: m->timer = m->a; break;
    case 0x65: m->timer_on = m->counter_on = false; break;
    case 0x67: { uint8_t cy = m->a & 1; m->a = (uint8_t)(m->a >> 1 | (m->psw & M48_CY)); m->psw = (uint8_t)((m->psw & ~M48_CY) | cy << 7); break; }
    case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E: case 0x6F: m48_add(m, *m48_r(m, op & 7), 0); break;
    case 0x70: case 0x71: m48_add(m, m->ram[*m48_r(m, op & 1) & 63], m->psw >> 7); break;
    case 0x75: break;                                                   // ENT0 CLK
    case 0x76: m48_jcc(m, m->f1); c = 2; break;
    case 0x77: m->a = (uint8_t)(m->a >> 1 | m->a << 7); break;
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: m48_add(m, *m48_r(m, op & 7), m->psw >> 7); break;
    case 0x80: case 0x81: m->a = M48_EXT_R(*m48_r(m, op & 1)); c = 2; break;
    case 0x83: m48_pull(m, false); c = 2; break;
    case 0x85: m->psw &= (uint8_t)~M48_F0; break;
    case 0x86: m48_jcc(m, m->irq_line); c = 2; break;
    case 0x88: M48_BUS_W((uint8_t)(M48_BUS_R() | m48_fetch(m))); c = 2; break;
    case 0x89: m->p1 |= m48_fetch(m); M48_PORT_W(1, m->p1); c = 2; break;
    case 0x8A: m->p2 |= m48_fetch(m); M48_PORT_W(2, m->p2); c = 2; break;
    case 0x8C: case 0x8D: case 0x8E: case 0x8F: c = 2; break;           // ORLD Pp,A
    case 0x90: case 0x91: M48_EXT_W(*m48_r(m, op & 1), m->a); c = 2; break;
    case 0x93: m48_pull(m, true); m->irq_in_progress = false; c = 2; break;
    case 0x95: m->psw ^= M48_F0; break;
    case 0x96: m48_jcc(m, m->a != 0); c = 2; break;
    case 0x97: m->psw &= (uint8_t)~M48_CY; break;
    case 0x98: M48_BUS_W((uint8_t)(M48_BUS_R() & m48_fetch(m))); c = 2; break;
    case 0x99: m->p1 &= m48_fetch(m); M48_PORT_W(1, m->p1); c = 2; break;
    case 0x9A: m->p2 &= m48_fetch(m); M48_PORT_W(2, m->p2); c = 2; break;
    case 0x9C: case 0x9D: case 0x9E: case 0x9F: c = 2; break;           // ANLD Pp,A
    case 0xA0: case 0xA1: m->ram[*m48_r(m, op & 1) & 63] = m->a; break;
    case 0xA3: m->a = M48_ROM((uint16_t)((m->pc & 0xF00) | m->a)); c = 2; break;
    case 0xA5: m->f1 = 0; break;
    case 0xA7: m->psw ^= M48_CY; break;
    case 0xA8: case 0xA9: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF: *m48_r(m, op & 7) = m->a; break;
    case 0xB0: case 0xB1: { uint8_t a = *m48_r(m, op & 1); m->ram[a & 63] = m48_fetch(m); c = 2; break; }
    case 0xB3: m->pc = (uint16_t)((m->pc & 0xF00) | M48_ROM((uint16_t)((m->pc & 0xF00) | m->a))); c = 2; break;
    case 0xB5: m->f1 ^= 1; break;
    case 0xB6: m48_jcc(m, m->psw & M48_F0); c = 2; break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF: *m48_r(m, op & 7) = m48_fetch(m); c = 2; break;
    case 0xC5: m->psw &= (uint8_t)~M48_BS; break;
    case 0xC6: m48_jcc(m, m->a == 0); c = 2; break;
    case 0xC7: m->a = m->psw; break;
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF: (*m48_r(m, op & 7))--; break;
    case 0xD0: case 0xD1: m->a ^= m->ram[*m48_r(m, op & 1) & 63]; break;
    case 0xD3: m->a ^= m48_fetch(m); c = 2; break;
    case 0xD5: m->psw |= M48_BS; break;
    case 0xD7: m->psw = (uint8_t)(m->a | 0x08); break;
    case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF: m->a ^= *m48_r(m, op & 7); break;
    case 0xE3: m->a = M48_ROM((uint16_t)(0x300 | m->a)); c = 2; break;
    case 0xE5: m->a11 = 0; break;
    case 0xE6: m48_jcc(m, !(m->psw & M48_CY)); c = 2; break;
    case 0xE7: m->a = (uint8_t)(m->a << 1 | m->a >> 7); break;
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0xEC: case 0xED: case 0xEE: case 0xEF: {
        uint8_t *p = m48_r(m, op & 7);
        (*p)--;
        m48_jcc(m, *p != 0);
        c = 2;
        break;
    }
    case 0xF0: case 0xF1: m->a = m->ram[*m48_r(m, op & 1) & 63]; break;
    case 0xF5: m->a11 = 1; break;
    case 0xF6: m48_jcc(m, m->psw & M48_CY); c = 2; break;
    case 0xF7: { uint8_t cy = m->a >> 7; m->a = (uint8_t)(m->a << 1 | (m->psw >> 7)); m->psw = (uint8_t)((m->psw & ~M48_CY) | cy << 7); break; }
    case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xFE: case 0xFF: m->a = *m48_r(m, op & 7); break;
    default: break;                                                     // the gaps: one-cycle no-ops
    }
    return m48_cycles[op];
}

// (cycles counts as it goes, so a port write sees the time of its instruction)
static int m48_run(m48_t *m, int budget) {
    int done = 0;
    while (done < budget) {
        done += m48_step(m);
    }
    return done;
}
