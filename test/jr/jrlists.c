// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// jrlists.c -- the plugin's game loop (src/jrplay.c) on the Mac: lists as
// the cart would send them, for tools/mame-replay.sh, and what the console
// would show.
//
//   jrlists ROMS.zip OUT LISTS [--script S] [--snaps L,L,...] [--gain G]
//     OUT/lists.bin      the lists, as test/host/host.c records them ('F')
//     OUT/cv-L.png       the console's picture after list L (the model of its video RAM)
//     OUT/stats.txt      a line a list: its bytes, T, pictures so far
//   --script: as jrhost's, by the board's frames (in2=80 is a coin)
//   --gain G: the sound's gain (jrp_gain)
//   --roms-out FILE: the built ROMs (jr_roms_t), for test/jr/armcount.py
//   --parts M,E: the music's and the effects' parts in the mix (1,1; 1,0 is the music alone)
//   --samples-out FILE: each sample before the knee (float32, 1.0 = full scale), for test/jr/knee.py
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "jrcv.h"
#include "jrplay.h"
#include "jrroms.h"
#include "jrsnd.h"
#include "swzip.h"

static const uint8_t tms[16][3] = {
    {0, 0, 0}, {0, 0, 0}, {33, 200, 66}, {94, 220, 120}, {84, 85, 237}, {125, 118, 252}, {212, 82, 77},
    {66, 235, 245}, {252, 85, 84}, {255, 121, 120}, {212, 193, 84}, {230, 206, 128}, {33, 176, 59},
    {201, 91, 186}, {204, 204, 204}, {255, 255, 255}};

static void png_rgb(const char *path, const uint8_t *rgb, int w, int h) {
    size_t rn = (size_t)h * (1 + 3 * w);
    uint8_t *raw = malloc(rn);
    for (int y = 0; y < h; y++) { raw[y * (1 + 3 * w)] = 0; memcpy(raw + y * (1 + 3 * w) + 1, rgb + y * 3 * w, 3 * w); }
    uLongf zn = compressBound(rn);
    uint8_t *z = malloc(zn);
    compress2(z, &zn, raw, rn, 9);
    FILE *f = fopen(path, "wb");
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
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

// the console's picture from its video RAM (Graphics II, no sprites)
static void cv_png(const char *path) {
    static uint8_t rgb[192 * 256 * 3];
    const uint8_t *v = jrcv_vram();
    for (int y = 0; y < 192; y++)
        for (int x = 0; x < 256; x++) {
            const int slot = v[0x1800 + (y >> 3) * 32 + (x >> 3)];
            const int a = (y >> 6) * 0x800 + slot * 8 + (y & 7);
            const int on = v[a] >> (7 - (x & 7)) & 1, col = v[0x2000 + a];
            int c = on ? col >> 4 : col & 15;
            memcpy(&rgb[(y * 256 + x) * 3], tms[c], 3);
        }
    png_rgb(path, rgb, 256, 192);
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

// the script's inputs, at the board's vertical blank as test/jr/jrhost.c has them
static const char *sp;
static void frame_hook(void) {
    static uint8_t in0, in2 = 0x40;
    for (;;) {
        while (*sp == ' ') sp++;
        int f, v, used;
        char port[8];
        if (!*sp || sscanf(sp, "%d:%3[^=]=%x%n", &f, port, &v, &used) != 3 || f > (int)jr.frame) break;
        if (!strcmp(port, "in0")) in0 = (uint8_t)v;
        else in2 = (uint8_t)(0x40 | v);
        sp += used;
    }
    jr.in0 = in0;
    jr.in2 = in2;
}

// each picture drawn a part at a time (src/jrdraw.c) against one drawn whole
// (jr.c's jr_render_tiles, the HUD's holes, jr_render_sprites)
static uint8_t whole[JR_W * JR_H];
static int pens_bad, pens_checked;
static int hud_bad;
static void check_pens(const uint8_t *pens) {
    jr_render_tiles(whole);
    const int h = jrcv_hud_check(whole);   // (not jrcv_hud(), which would take the HUD from here)
    if (h && !hud_bad++) fprintf(stderr, "picture %d (board frame %u): %d HUD pixels unlike the tiles\n", pens_checked + 1, jr.frame, h);
    jr_render_sprites(whole);
    pens_checked++;
    if (memcmp(whole, pens, sizeof whole)) {
        if (!pens_bad++) {
            int n = 0;
            for (int i = 0; i < JR_W * JR_H; i++) n += whole[i] != pens[i];
            fprintf(stderr, "picture %d (board frame %u): %d pens differ from a whole drawing\n", pens_checked, jr.frame, n);
        }
    }
}

static int pic_bad;
static void check_picture(const uint8_t *pens) {
    const int n = jrcv_check(pens);
    if (n && !pic_bad++) fprintf(stderr, "picture %u: %d runs unlike a whole conversion\n", jrp_stats.pictures + 1, n);
}

static FILE *samples_f;
static void sample_hook(float x) { fwrite(&x, sizeof x, 1, samples_f); }

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: jrlists ROMS.zip OUT LISTS [--script S] [--snaps L,L] [--gain G]\n"); return 2; }
    uint32_t zn;
    uint8_t *zip = slurp(argv[1], &zn);
    const char *out = argv[2];
    const int nlists = atoi(argv[3]);
    const char *script = "", *roms_out = NULL, *samples_out = NULL;
    static int snaps[512];
    int nsnaps = 0;
    for (int i = 4; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--script")) script = argv[i + 1];
        else if (!strcmp(argv[i], "--snaps"))
            for (char *p = argv[i + 1]; *p && nsnaps < 512;) { snaps[nsnaps++] = (int)strtol(p, &p, 10); if (*p == ',') p++; }
        else if (!strcmp(argv[i], "--gain")) jrp_gain = (float)atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--roms-out")) roms_out = argv[i + 1];
        else if (!strcmp(argv[i], "--parts")) sscanf(argv[i + 1], "%f,%f", &jr_snd_part[0], &jr_snd_part[1]);
        else if (!strcmp(argv[i], "--samples-out")) samples_out = argv[i + 1];
    }
    if (samples_out) {
        samples_f = fopen(samples_out, "wb");
        jrp_sample_hook = sample_hook;
    }
    static uint8_t files[JR_NROMS][0x2000];
    const uint8_t *fp[JR_NROMS] = {0};
    swzip_entry_t e;
    for (int i = 0; swzip_entry(zip, zn, i, &e); i++) {
        int k = jr_rom_find(e.usize, e.crc);
        if (k >= 0 && swzip_read(&e, files[k])) fp[k] = files[k];
    }
    for (int k = 0; k < JR_NROMS; k++)
        if (!fp[k]) { fprintf(stderr, "missing %s\n", jr_rom_list[k].name); return 1; }
    static jr_roms_t roms;
    jr_roms_build(&roms, fp);
    if (roms_out) {
        // the built ROMs, for test/jr/armcount.py to put into the M33's memory
        FILE *rf = fopen(roms_out, "wb");
        fwrite(&roms, 1, sizeof roms, rf);
        fclose(rf);
    }
    jrp_init(&roms);
    char path[512];
    snprintf(path, sizeof path, "%s/lists.bin", out);
    FILE *lf = fopen(path, "wb");
    if (!lf) { fprintf(stderr, "jrlists: can't write %s (is %s a folder?)\n", path, out); return 1; }
    snprintf(path, sizeof path, "%s/stats.txt", out);
    FILE *sf = fopen(path, "w");
    fprintf(sf, "# list bytes T pictures frames left peak\n");
    snprintf(path, sizeof path, "%s/plan.txt", out);
    FILE *pf = fopen(path, "w");
    for (int k = 0; k < JRCV_SAMPLES; k++) fprintf(pf, "%d %u\n", k, jrcv_sample_t[k]);
    fprintf(pf, "list %u\n", jrcv_list_t);
    fclose(pf);
    static uint8_t list[JRCV_LIST_MAX];
    sp = script;
    jrp_frame_hook = frame_hook;
    jrp_check_pens = check_pens;
    jrp_check_picture = check_picture;
    for (int l = 1; l <= nlists; l++) {
        const size_t n = jrp_list(list);
        if (n > JRCV_LIST_MAX) { fprintf(stderr, "list %d too long: %zu\n", l, n); return 1; }
        const uint8_t h[9] = {'F', 1, (uint8_t)l, (uint8_t)n, (uint8_t)(n >> 8), 0, 0, 0, 0};
        fwrite(h, 1, 9, lf);
        fwrite(list, 1, n, lf);
        fprintf(sf, "%d %zu %u %u %u %u %.3f\n", l, n, jrcv_list_t, jrp_stats.pictures, jrp_stats.frames, jrcv_left(), jrp_stats.peak);
        for (int i = 0; i < nsnaps; i++)
            if (snaps[i] == l) { snprintf(path, sizeof path, "%s/cv-%d.png", out, l); cv_png(path); }
    }
    fclose(lf);
    fclose(sf);
    printf("pictures drawn a part at a time: %d, %d unlike a whole drawing, %d with the HUD unlike, %d converted unlike a whole conversion\n",
           pens_checked, pens_bad, hud_bad, pic_bad);
    printf("list: %u T on MAME (%u by the kernel's costs), %d samples; %u pictures in %d lists\n", jrcv_list_t, jrcv_kit_t, JRCV_SAMPLES, jrp_stats.pictures, nlists);
    printf("sound: gain %.2f, %u of %u samples (%.3f%%) into the knee (above %.2f of full scale)\n", jrp_gain, jrp_stats.kneed,
           jrp_stats.samples, 100.0 * jrp_stats.kneed / (jrp_stats.samples ? jrp_stats.samples : 1), JRP_KNEE);
    if (samples_f) fclose(samples_f);
    return 0;
}
