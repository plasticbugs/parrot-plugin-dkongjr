// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// jrhost.c -- the board (src/jr.c) on the Mac, for comparing with MAME.
//
//   jrhost ROMS.zip OUT FRAMES [options]
//     --script "F:in0=XX F:in2=XX ..."   inputs from vertical blank F on (hex, active high)
//     --snaps F,F,...                    OUT/jr-F.png: the picture of frame F, upright, 224x256
//     --pal FILE                         MAME's palette (index rrggbb lines) for the PNGs
//     --trace A-B                        OUT/z80.trace: every Z80 instruction in frames A to B
//     --m48trace A-B                     OUT/m48.trace: every 8035 instruction
//     --holds FILE                       the sprite copy's holds as MAME had them (mame.sh's holds.txt)
//   OUT/dump.bin: at each vertical blank (line 240, before the NMI), the Z80's
//   $6000-$77FF, then the 8035's 64 bytes of RAM and its A, PSW, PC (2), P1, P2.
//
// The ROMs are read from the zip and never written anywhere.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "jr.h"
#include "jrroms.h"
#include "swzip.h"

static jr_roms_t roms;
static FILE *dumpf, *tracef, *m48f;
static uint32_t pal[256];
static int snaps[256], nsnaps;
static int tr_a = -1, tr_b = -1, mt_a = -1, mt_b = -1;
static const char *out;
static struct { int frame; int port; int val; } ev[4096];
static int *holds, nholds, holdi;

// MAME's own holds, in order: R - (W + 13), from mame.sh's holds.txt
static int replay_hold(uint64_t end) {
    (void)end;
    if (holdi < nholds) return holds[holdi++];
    return 3082;
}
static int nev, evi;

static void png(const char *path, const uint8_t *pens, int w, int h) {
    uint8_t *raw = malloc((size_t)h * (1 + 3 * w));
    for (int y = 0; y < h; y++) {
        uint8_t *r = raw + y * (1 + 3 * w);
        r[0] = 0;
        for (int x = 0; x < w; x++) {
            const uint32_t c = pal[pens[y * w + x]];
            r[1 + 3 * x] = (uint8_t)(c >> 16);
            r[2 + 3 * x] = (uint8_t)(c >> 8);
            r[3 + 3 * x] = (uint8_t)c;
        }
    }
    uLongf zn = compressBound((uLong)(h * (1 + 3 * w)));
    uint8_t *z = malloc(zn);
    compress2(z, &zn, raw, (uLong)(h * (1 + 3 * w)), 9);
    FILE *f = fopen(path, "wb");
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13] = {0, 0, (uint8_t)(w >> 8), (uint8_t)w, 0, 0, (uint8_t)(h >> 8), (uint8_t)h, 8, 2, 0, 0, 0};
    const struct { const char *t; const uint8_t *d; uint32_t n; } ch[3] = {{"IHDR", ihdr, 13}, {"IDAT", z, (uint32_t)zn}, {"IEND", NULL, 0}};
    for (int i = 0; i < 3; i++) {
        uint8_t b[4] = {(uint8_t)(ch[i].n >> 24), (uint8_t)(ch[i].n >> 16), (uint8_t)(ch[i].n >> 8), (uint8_t)ch[i].n};
        fwrite(b, 1, 4, f);
        fwrite(ch[i].t, 1, 4, f);
        if (ch[i].n) fwrite(ch[i].d, 1, ch[i].n, f);
        uLong c = crc32(0, (const Bytef *)ch[i].t, 4);
        if (ch[i].n) c = crc32(c, ch[i].d, ch[i].n);
        uint8_t cb[4] = {(uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c};
        fwrite(cb, 1, 4, f);
    }
    fclose(f);
    free(raw);
    free(z);
}

// the trace hooks (jr.c is built with -DZ80_TRACE=jr_z80_trace -DM48_TRACE=jr_m48_trace)
static FILE *nmif;
void jr_z80_trace(const z80_t *z) {
    if (z->pc.w == 0x0066 && nmif) fprintf(nmif, "N %llu\n", (unsigned long long)z->cycles);
    if (!tracef || (int)jr.frame < tr_a || (int)jr.frame > tr_b) return;
    fprintf(tracef, "C=%llu AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X %04X\n", (unsigned long long)z->cycles, z->af.w & 0xFFD7,
            z->bc.w, z->de.w, z->hl.w, z->ix.w, z->iy.w, z->sp.w, z->pc.w);
}
void jr_m48_trace(const m48_t *m) {
    if (!m48f || (int)jr.frame < mt_a || (int)jr.frame > mt_b) return;
    const uint8_t *r = m->ram + ((m->psw & 0x10) ? 24 : 0);
    fprintf(m48f, "C=%llu A=%02X R0=%02X R1=%02X R2=%02X R3=%02X R4=%02X R5=%02X R6=%02X R7=%02X P2=%02X %03X\n",
            (unsigned long long)m->cycles, m->a, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], m->p2, m->pc);
}

static void vblank(void) {
    const int f = (int)jr.frame;       // 1 at the first
    // the state MAME's frame_done sees
    fwrite(jr.ram, 1, sizeof jr.ram, dumpf);
    uint8_t s[70];
    memcpy(s, jr.snd.ram, 64);
    s[64] = jr.snd.a;
    s[65] = jr.snd.psw;
    s[66] = (uint8_t)jr.snd.pc;
    s[67] = (uint8_t)(jr.snd.pc >> 8);
    s[68] = jr.snd.p1;
    s[69] = jr.snd.p2;
    fwrite(s, 1, sizeof s, dumpf);
    for (int i = 0; i < nsnaps; i++)
        if (snaps[i] == f) {
            static uint8_t pens[JR_W * JR_H];
            jr_render(pens);
            char p[512];
            snprintf(p, sizeof p, "%s/jr-%d.png", out, f);
            png(p, pens, JR_W, JR_H);
        }
    while (evi < nev && ev[evi].frame <= f) {
        if (ev[evi].port == 0) jr.in0 = (uint8_t)ev[evi].val;
        else jr.in2 = (uint8_t)(0x40 | ev[evi].val);
        evi++;
    }
}

static uint8_t *slurp(const char *path, uint32_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc((size_t)len);
    if (fread(p, 1, (size_t)len, f) != (size_t)len) exit(1);
    fclose(f);
    *n = (uint32_t)len;
    return p;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: jrhost ROMS.zip OUT FRAMES [--script S] [--snaps F,F] [--pal FILE] [--trace A-B]\n"); return 2; }
    uint32_t zn;
    uint8_t *zip = slurp(argv[1], &zn);
    out = argv[2];
    const int frames = atoi(argv[3]);
    const char *script = "", *palfile = NULL;
    for (int i = 4; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--script")) script = argv[i + 1];
        else if (!strcmp(argv[i], "--snaps")) {
            for (char *p = argv[i + 1]; *p && nsnaps < 256;) { snaps[nsnaps++] = (int)strtol(p, &p, 10); if (*p == ',') p++; }
        } else if (!strcmp(argv[i], "--pal")) palfile = argv[i + 1];
        else if (!strcmp(argv[i], "--trace")) sscanf(argv[i + 1], "%d-%d", &tr_a, &tr_b);
        else if (!strcmp(argv[i], "--m48trace")) sscanf(argv[i + 1], "%d-%d", &mt_a, &mt_b);
        else if (!strcmp(argv[i], "--holds")) {
            FILE *f = fopen(argv[i + 1], "r");
            char k;
            long long v, w = -1;
            holds = malloc(sizeof(int) * 200000);
            while (f && fscanf(f, " %c %lld", &k, &v) == 2 && nholds < 200000) {
                if (k == 'W') w = v;
                else if (w >= 0) { holds[nholds++] = (int)(v - (w + 13)); w = -1; }
            }
            if (f) fclose(f);
            jr_dma_hold = replay_hold;
        }
    }
    // the set, from the zip, by CRC
    static uint8_t files[JR_NROMS][0x2000];
    const uint8_t *fp[JR_NROMS] = {0};
    swzip_entry_t e;
    for (int i = 0; swzip_entry(zip, zn, i, &e); i++) {
        int k = jr_rom_find(e.usize, e.crc);
        if (k >= 0 && swzip_read(&e, files[k])) fp[k] = files[k];
    }
    for (int k = 0; k < JR_NROMS; k++)
        if (!fp[k]) { fprintf(stderr, "missing %s\n", jr_rom_list[k].name); return 1; }
    jr_roms_build(&roms, fp);
    // inputs
    for (const char *p = script; *p && nev < 4096;) {
        while (*p == ' ') p++;
        if (!*p) break;
        int f, v;
        char port[8];
        if (sscanf(p, "%d:%3[^=]=%x", &f, port, &v) != 3) { fprintf(stderr, "bad script at %s\n", p); return 2; }
        ev[nev].frame = f;
        ev[nev].port = !strcmp(port, "in0") ? 0 : 2;
        ev[nev].val = v;
        nev++;
        while (*p && *p != ' ') p++;
    }
    jr_reset(&roms);
    jr_palette(pal);
    if (palfile) {
        FILE *f = fopen(palfile, "r");
        int i;
        unsigned c;
        while (f && fscanf(f, "%d %x", &i, &c) == 2) if (i < 256) pal[i] = c;
        if (f) fclose(f);
    }
    char path[512];
    snprintf(path, sizeof path, "%s/dump.bin", out);
    dumpf = fopen(path, "wb");
    if (tr_a >= 0) { snprintf(path, sizeof path, "%s/z80.trace", out); tracef = fopen(path, "w"); }
    snprintf(path, sizeof path, "%s/nmi.txt", out);
    nmif = fopen(path, "w");
    if (mt_a >= 0) { snprintf(path, sizeof path, "%s/m48.trace", out); m48f = fopen(path, "w"); }
    jr_vblank = vblank;
    jr_sline_t sl;
    for (;;) {
        jr_line(&sl);
        if ((int)jr.frame >= frames && jr.line == JR_VBLANK_LINE) break;
    }
    fclose(dumpf);
    if (tracef) fclose(tracef);
    if (m48f) fclose(m48f);
    if (nmif) fclose(nmif);
    return 0;
}
