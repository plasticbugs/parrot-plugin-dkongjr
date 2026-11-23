// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// jrsnd.c -- see jrsnd.h. MAME's discrete modules, one step at a time, for
// just the parts dkongjr_discrete uses. A logic signal carries MAME's "x
// time": its state at the step's end plus the fraction of the step it has
// been in it (0 = the whole step), so the gates can give a level averaged
// over the step instead of aliasing.
#include "jrsnd.h"

#include <math.h>
#include <stdbool.h>

#include "jr.h"

static float dt;

// ---- the modules --------------------------------------------------------------------------
static inline float charge_exp(float rc) { return 1.0f - expf(-dt / rc); }

typedef struct {               // 74LS624 VCO
    float vfs, vrs, expo, vcap, cscale, t_used;
    bool cap;
    int ff;
} ls624_t;

static void ls624_init(ls624_t *v, float c, float r_freq_in, float c_freq_in, float r_rng_in) {
    const float in_r = 90000.0f;
    v->vfs = in_r / (r_freq_in + in_r);
    v->vrs = in_r / (r_rng_in + in_r);
    v->cap = c_freq_in > 0;
    v->expo = v->cap ? charge_exp(r_freq_in * in_r / (r_freq_in + in_r) * c_freq_in) : 0;
    v->vcap = 0;
    v->cscale = 1e-7f / c;
    v->t_used = 0;
    v->ff = 0;
}

// one step: the falling and rising edges in it and the x time after the last
static void ls624_step(ls624_t *v, float vmod, int *count_f, int *count_r, float *x) {
    float f = vmod * v->vfs;
    if (v->cap) { v->vcap += (f - v->vcap) * v->expo; f = v->vcap; }
    const float r = 5.0f * v->vrs;
    const float f2 = f * f, f3 = f2 * f, f4 = f3 * f;
    float freq = 1990.4769024796283f + 1207.0059213983407f * f + 1326.6985579561108f * f2
               - 155.00979825922698f * f3 + 2.8184536266938172f * f4
               + r * (-235.03421582744556f - 338.36786704527788f * f - 135.69136703258670f * f2
                      + 2.9914575453819188f * f3 + 1.6855569086173170f * f4);
    freq *= v->cscale;
    const float t1 = 0.5f / freq;
    float t = v->t_used + dt, xt = 0;
    int cf = 0, cr = 0;
    while (t > t1) {
        t -= t1;
        v->ff ^= 1;
        if (v->ff) cr++; else cf++;
        xt = t;
    }
    v->t_used = t;
    *count_f = cf;
    *count_r = cr;
    *x = xt / dt;
}

static float ls624_logic_x(ls624_t *v, float vmod) {
    int cf, cr;
    float x;
    ls624_step(v, vmod, &cf, &cr, &x);
    return (float)v->ff + x;
}

typedef struct {               // the retriggerable one-shot (an LS123)
    float countdown, width, amp;
    bool state, last, active_low;
    float out;
} oneshot_t;

static void oneshot_init(oneshot_t *o, float r, float c, float amp, bool active_low) {
    o->width = 0.25f * r * c * (1.0f + 700.0f / r);
    o->amp = amp;
    o->active_low = active_low;
    o->countdown = 0;
    o->state = o->last = false;
    o->out = active_low ? amp : 0;
}

static float oneshot_step(oneshot_t *o, bool trig) {
    bool count = o->state;
    if (trig != o->last) {
        o->last = trig;
        if (trig) {             // a rising edge
            if (!o->state) {
                o->state = true;
                o->out = o->active_low ? 0 : o->amp;
                o->countdown = o->width;
            } else {
                o->countdown = o->width;
                count = false;
            }
        }
    }
    if (count) {
        o->countdown -= dt;
        if (o->countdown <= 0) {
            o->out = o->active_low ? o->amp : 0;
            o->countdown = 0;
            o->state = false;
        }
    }
    return o->out;
}

// The mean over the step of a two-input gate, from the inputs' x times:
// with x > 0 an input was the other state for the step's first 1 - x.
static float gate_mean(float a, float b, int kind /* 0 xor, 1 nand */) {
    const int sa = (int)a, sb = (int)b;
    const float xa = a - (float)sa, xb = b - (float)sb;
    const float ta = xa > 0 ? 1.0f - xa : 0, tb = xb > 0 ? 1.0f - xb : 0;
    float cut[4] = {0, ta, tb, 1};
    if (cut[1] > cut[2]) { float t = cut[1]; cut[1] = cut[2]; cut[2] = t; }
    float sum = 0;
    for (int i = 0; i < 3; i++) {
        const float w = cut[i + 1] - cut[i];
        if (w <= 0) continue;
        const float mid = (cut[i] + cut[i + 1]) * 0.5f;
        const int va = mid < ta ? !sa : sa, vb = mid < tb ? !sb : sb;
        sum += w * (float)(kind == 0 ? (va ^ vb) : !(va && vb));
    }
    return sum;
}

// a gate's output as a state with an x time (for a gate that feeds another)
static float gate_x(float a, float b, int kind) {
    const int sa = (int)a, sb = (int)b;
    const float xa = a - (float)sa, xb = b - (float)sb;
    const int end = kind == 0 ? (sa ^ sb) : !(sa && sb);
    const int start = kind == 0 ? ((xa > 0 ? !sa : sa) ^ (xb > 0 ? !sb : sb)) : !((xa > 0 ? !sa : sa) && (xb > 0 ? !sb : sb));
    if (start == end) return (float)end;      // (two changes in one step are rare: let them go)
    // one change: when the input that changed did
    float x = xa > 0 && xb > 0 ? (xa < xb ? xa : xb) : xa > 0 ? xa : xb;
    if (x >= 1.0f) x = 0.999f;
    return (float)end + x;
}

static float inverter_x(float in, float lo, float hi) {
    const int s = (int)in;
    const float x = in - (float)s;
    const int out = !s;
    if (x > 0) return lo + (hi - lo) * (out ? x : 1.0f - x);
    return out ? hi : lo;
}

typedef struct {               // MAME's RCDISC_MODULATED
    float exp_low[2], gain[2], exp_high[4], vd_gain[4], vp, v_cap;
} rcdisc_mod_t;

static float divider(float r1, float r2) { return r2 / (r1 + r2); }
static float parallel(float a, float b) { return a * b / (a + b); }
static float discharge_exp(float rc) { return expf(-dt / rc); }

static void rcdisc_mod_init(rcdisc_mod_t *m, float r1, float r2, float r3, float r4, float c, float vp) {
    float rc0 = r1 + r2, rc1 = r2;
    if (rc0 < 1) rc0 = 1;
    if (rc1 < 1) rc1 = 1;
    m->exp_low[0] = discharge_exp(c * rc0);
    m->gain[0] = divider(rc0, r4);
    m->exp_low[1] = discharge_exp(c * rc1);
    m->gain[1] = divider(rc1, r4);
    const float rc2_0 = r4, rc2_1 = parallel(r3, r4);
    m->exp_high[0] = discharge_exp(c * (rc0 + rc2_0));
    m->vd_gain[0] = divider(rc0, rc2_0);
    m->exp_high[1] = discharge_exp(c * (rc1 + rc2_0));
    m->vd_gain[1] = divider(rc1, rc2_0);
    m->exp_high[2] = discharge_exp(c * (rc0 + rc2_1));
    m->vd_gain[2] = divider(rc0, rc2_1);
    m->exp_high[3] = discharge_exp(c * (rc1 + rc2_1));
    m->vd_gain[3] = divider(rc1, rc2_1);
    m->vp = vp;
    m->v_cap = 0;
}

static float rcdisc_mod_step(rcdisc_mod_t *m, float in1, float in2) {
    const int mod1 = in1 > 0.5f, mod2 = in2 > 0.6f, st = 2 * mod2 + mod1;
    const float u = mod1 ? 0 : m->vp;
    float diff = u - m->v_cap, out;
    if (diff * m->vd_gain[st] < -0.6f) {
        diff = u + 0.6f - m->v_cap;
        m->v_cap += diff - diff * m->exp_low[mod1];
        out = mod2 ? 0 : -0.6f;
    } else {
        m->v_cap += diff - diff * m->exp_high[st];
        out = mod2 ? 0 : (u - m->v_cap) * m->gain[mod1];
    }
    return out;
}

typedef struct { float expo, v; } rc_t;     // a one-pole filter's state
static float crfilter(rc_t *f, float in, float vref) {   // high pass, towards vref
    const float out = in - f->v;
    f->v += (out - vref) * f->expo;
    return out;
}
static float rcfilter(rc_t *f, float in) {               // low pass
    f->v += (in - f->v) * f->expo;
    return f->v;
}

// ---- the board ----------------------------------------------------------------------------
struct jr_snd {
    // SOUND0, walking
    ls624_t vco113, vco118;
    oneshot_t os110;
    uint16_t count;
    float b101, b102, b103, b104, cnt_x;
    // SOUND1, jumping
    oneshot_t os10;
    rc_t mix13;
    float mix13_r0, mix13_r1, mix13_rt;
    ls624_t vco14;
    rcdisc_mod_t rcm16;
    rc_t cr17, rc242;
    // SOUND2, climbing
    uint32_t lfsr;
    float lfsr_t, lfsr_out;
    oneshot_t os25;
    rcdisc_mod_t rcm26;
    rc_t cr27, rc243;
    // SOUND9, falling
    ls624_t vco91;
    // the DAC
    int dis_state;
    float dis_v, dis_k;
    float a1, a2, b0, b1, b2, x1, x2, y1, y2;
    // the amplifier
    float mix_r[5], mix_rt;
    rc_t mix_cf, mix_amp, cr295;
};

static struct jr_snd S;

void jr_snd_reset(float rate) {
    dt = 1.0f / rate;
    struct jr_snd *s = &S;
    *s = (struct jr_snd){0};
    ls624_init(&s->vco113, 22e-9f, 10000, 3.3e-6f, 1000);
    ls624_init(&s->vco118, 4.7e-9f, 20000, 3.3e-6f, 1000);
    oneshot_init(&s->os110, 47000, 4.7e-6f, 4, false);
    oneshot_init(&s->os10, 47000, 22e-6f, 4, false);
    s->mix13_r0 = 1.0f / 47000;
    s->mix13_r1 = 1.0f / 10000;
    s->mix13_rt = 1.0f / (s->mix13_r0 + s->mix13_r1);
    s->mix13.expo = charge_exp(s->mix13_rt * 47e-6f);
    ls624_init(&s->vco14, 220e-9f, 8245.614f, 0, 1000);
    rcdisc_mod_init(&s->rcm16, 120, 10000, 1.0f, 100000, 10e-6f, 5.0f);
    s->cr17.expo = charge_exp(47000 * 0.47e-6f);
    s->rc242.expo = charge_exp(100 * 56e-9f);
    s->lfsr = 0;
    s->lfsr_out = 1;           // after the reset's feedback pass: F0 = 0, inverted
    oneshot_init(&s->os25, 47000, 22e-6f, 4, true);
    rcdisc_mod_init(&s->rcm26, 120, 4700, 1.0f, 100000, 10e-6f, 5.0f);
    s->cr27.expo = charge_exp(20000 * 0.47e-6f);
    s->rc243.expo = charge_exp(120 * 1e-6f);
    ls624_init(&s->vco91, 0.12e-6f, 30000, 47e-6f, 1000);
    s->dis_k = expf(-dt / (10000 * 10e-6f));
    // the Sallen-Key low pass after the DAC, pre-warped bilinear, as MAME's filter2
    {
        const double r1 = 5600, r2 = 5600, c1 = 22e-9, c2 = 10e-9, fs = rate;
        const double fc = 1.0 / (2 * M_PI * sqrt(c1 * c2 * r1 * r2));
        const double q = sqrt(c1 * c2 * r1 * r2) / (c2 * (r1 + r2)), d = 1.0 / q;
        const double t2 = 2 * fs, wc = fs * 2 * tan(M_PI * fc / fs), wc2 = wc * wc;
        const double den = t2 * t2 + d * wc * t2 + wc2;
        s->a1 = (float)(2 * (wc2 - t2 * t2) / den);
        s->a2 = (float)((t2 * t2 - d * wc * t2 + wc2) / den);
        s->b0 = s->b2 = (float)(wc2 / den);
        s->b1 = 2 * s->b0;
    }
    static const float r[5] = {150000, 100000, 20000, 47000, 47000};
    float g = 0;
    for (int i = 0; i < 5; i++) { s->mix_r[i] = 1.0f / r[i]; g += s->mix_r[i]; }
    s->mix_rt = 1.0f / g;
    s->mix_cf.expo = charge_exp(s->mix_rt * 10e-9f);
    s->mix_amp.expo = charge_exp(100000 * 1e-6f);
    s->cr295.expo = charge_exp(1000 * 4.7e-6f);
}

float jr_snd_step(uint8_t bits, float dac) {
    struct jr_snd *s = &S;
    // MAME's inputs: SOUND0-2 and 7 inverted, SOUND9 as it is, and the
    // DAC's discharge the inverse of the 8035's P2 bit 7
    const bool s0_inv = !(bits & JR_S_WALK), s1_inv = !(bits & JR_S_JUMP), s2_inv = !(bits & JR_S_CLIMB);
    const bool s7_inv = !(bits & JR_S_SND7), s9 = bits & JR_S_FALL, discharge = !(bits & JR_S_DECAY);

    // ---- SOUND0, walking (task 1)
    {
        // the counter, clocked by VCO 118's falling edges (from the last step)
        const float n110 = oneshot_step(&s->os110, s0_inv);
        const float n111 = n110 != 0 ? 0.151f : 4.14f;
        const float n113 = ls624_logic_x(&s->vco113, n111);
        const float n105 = s7_inv ? n113 : 0;
        const float n106 = s7_inv ? s->b102 : s->b101;
        const float n107 = s7_inv ? s->b104 : s->b103;
        const float n115 = gate_x(n105, n106, 0);
        const float n116 = inverter_x(n107, 0.135f, 4.15f);
        int cf, cr;
        float x;
        ls624_step(&s->vco118, n116, &cf, &cr, &x);
        // the counter and its bits, for the next step (MAME's node order:
        // the counter reads 118 before 118 runs, so a step late)
        const uint16_t old = s->count;
        s->count = (uint16_t)((s->count + (unsigned)cf) & 0x3FFF);
        const float cx = cf ? x : 0;
        const uint16_t chg = (uint16_t)(old ^ s->count);
        s->b101 = (float)(s->count >> 6 & 1) + ((chg >> 6 & 1) ? cx : 0);
        s->b102 = (float)(s->count >> 3 & 1) + ((chg >> 3 & 1) ? cx : 0);
        s->b103 = (float)(s->count >> 12 & 1) + ((chg >> 12 & 1) ? cx : 0);
        s->b104 = (float)(s->count >> 11 & 1) + ((chg >> 11 & 1) ? cx : 0);
        const float n119 = n110 != 0 ? 1.0f : 0.0f;
        s->cnt_x = 0.2f + 4.7f * gate_mean(n119, n115, 1);     // NODE_241
    }
    const float n241 = s->cnt_x;

    // ---- SOUND1, jumping (task 2)
    float n242;
    {
        const float n10 = oneshot_step(&s->os10, s1_inv);
        const float n11 = n10 != 0 ? 4.14f : 0.151f;
        const float n12 = inverter_x(s->b104, 0.151f, 4.14f);
        const float v = (n11 * s->mix13_r0 + n12 * s->mix13_r1) * s->mix13_rt;
        const float n13 = rcfilter(&s->mix13, v);
        int cf, cr;
        float x;
        ls624_step(&s->vco14, n13, &cf, &cr, &x);
        if (x == 0) x = 1;
        const float n14 = 4.5f * (s->vco14.ff ? x : 1.0f - x);
        const float n15 = n10 == 0 ? 1.0f : 0.0f;
        const float n16 = rcdisc_mod_step(&s->rcm16, n15, n14);
        const float n17 = crfilter(&s->cr17, n16, 2.5f);
        n242 = rcfilter(&s->rc242, n17);
    }

    // ---- SOUND2, climbing (task 1)
    float n243;
    {
        s->lfsr_t += dt;
        const float period = 1.0f / 710;
        while (s->lfsr_t >= period) {
            s->lfsr_t -= period;
            // the new bit is the last feedback, inverted; the feedback is bit 2 ^ bit 15
            const uint32_t f0 = s->lfsr >> 16 & 1;
            uint32_t reg = ((s->lfsr << 1) | (f0 ^ 1)) & 0xFFFF;
            const uint32_t fb = (reg >> 2 ^ reg >> 15) & 1;
            s->lfsr = reg | fb << 16;
        }
        s->lfsr_out = (s->lfsr >> 16 & 1) ? 0.0f : 1.0f;
        const float n25 = oneshot_step(&s->os25, s2_inv);
        const float n26 = rcdisc_mod_step(&s->rcm26, n25, s->lfsr_out);
        const float n27 = crfilter(&s->cr27, n26, 2.5f);
        n243 = rcfilter(&s->rc243, n27);
    }

    // ---- SOUND9, falling (task 1)
    float n249;
    {
        const float n90 = s9 ? 0.134f : 4.16f;
        const float n91 = ls624_logic_x(&s->vco91, n90);
        n249 = 0.2f + 4.7f * gate_mean(s9 ? 1.0f : 0.0f, n91, 1);
    }

    // ---- the DAC (task 1)
    float n250;
    {
        // NODE_170: the discharge envelope, 1 down to 0 while discharging
        float n170;
        if (s->dis_state == 0) {
            if (discharge) { s->dis_state = 1; s->dis_v = 1; }
            n170 = 0;
            if (s->dis_state == 1) { n170 = s->dis_v; s->dis_v *= s->dis_k; }
        } else if (discharge) {
            n170 = s->dis_v;
            s->dis_v *= s->dis_k;
        } else {
            s->dis_state = 0;
            n170 = s->dis_v;          // (MAME leaves it this step)
        }
        const float n171 = dac * 0.01953125f * ((discharge ? 0.0f : 1.0f) + n170);
        const float y = -s->a1 * s->y1 - s->a2 * s->y2 + s->b0 * n171 + s->b1 * s->x1 + s->b2 * s->x2;
        s->x2 = s->x1; s->x1 = n171;
        s->y2 = s->y1; s->y1 = y;
        n250 = y;
    }

    // ---- the amplifier (task 3)
    const float v = (n249 * s->mix_r[0] + n241 * s->mix_r[1] + n243 * s->mix_r[2] + n242 * s->mix_r[3] + n250 * s->mix_r[4]) * s->mix_rt;
    const float lp = rcfilter(&s->mix_cf, v);
    s->mix_amp.v += (lp - s->mix_amp.v) * s->mix_amp.expo;
    const float n288 = lp - s->mix_amp.v;
    const float n295 = crfilter(&s->cr295, n288, 0);
    return n295 * 27768.64f * 0.5f;
}
