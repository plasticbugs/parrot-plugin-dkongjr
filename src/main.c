// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// Donkey Kong Jr.: Nintendo's 1982 arcade game on the ColecoVision, from
// your own MAME set (dkongjr.zip, or its files, in /DKONGJR on the card).
//
// The board (src/jr.c: its Z80, its 8035 and their memory, as MAME 0.288's
// dkong.cpp has them) runs here at its own speed; src/jrsnd.c is the sound
// board after the 8035 (MAME's discrete model); src/jrplay.c makes each of
// the console's lists from them: 16.77 ms of the game, 100 samples of its
// sound and as much of its picture as fits. The lists run back to back, so
// the console's time is the game's.
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "cvdiag.h"
#include "cvframes.h"
#include "cvinput.h"
#include "cvpsram.h"
#include "cvsd.h"
#include "cvusb.h"
#include "font8.h"
#include "gbkernel.h"
#include "handoff.h"
#include "swzip.h"

#include "jr.h"
#include "jrcv.h"
#include "jrplay.h"
#include "jrroms.h"

#define VERSION "0.1.0"
#define HOME "/DKONGJR"

static uint8_t list[JRCV_LIST_MAX];
static size_t list_len;

// ---- a screen of text, for the loading and what's missing -----------------------------------
// Graphics II with the font in tiles 32-95 of each third, white on black,
// and a name for each place. The first lists send the font; each sends the
// names (they may have changed) and the sound chip, silent.
#define ROWS 24
static char text[ROWS][29];
static int text_step;

static void say(int row, const char *s) {
    int i = 0;
    for (; i < 28 && s[i]; i++) text[row][i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];
    text[row][i] = 0;
}

static uint8_t *run(uint8_t *o, uint16_t a, const uint8_t *d, int n) {
    o[0] = 1;
    o[1] = (uint8_t)a;
    o[2] = (uint8_t)(a >> 8 | 0x40);
    o[3] = (uint8_t)n;
    memcpy(o + 4, d, (size_t)n);
    return o + 4 + n;
}

static size_t text_list(void) {
    static const uint8_t silence[10] = {0x80, 0x00, 0x9F, 0xA0, 0x00, 0xBF, 0xC0, 0x00, 0xDF, 0xFF};
    static uint8_t buf[256];
    uint8_t *o = list;
    *o++ = 0;                   // nothing in the blank part
    *o++ = 2;
    *o++ = sizeof silence;
    memcpy(o, silence, sizeof silence);
    o += sizeof silence;
    if (text_step < 6) {
        // the font in one third (a list each for its patterns and its colours)
        const uint16_t third = (uint16_t)(text_step / 2 * 0x800);
        for (int half = 0; half < 2; half++) {
            if (text_step & 1) memset(buf, 0xF1, sizeof buf);       // white on black
            else {
                for (int c = 0; c < 32; c++) memcpy(buf + c * 8, font8[half * 32 + c], 8);
                if (half) {     // the font has no _ (its last place is a second ?), which ROM names have
                    memset(buf + 31 * 8, 0, 8);
                    buf[31 * 8 + 6] = 0x7E;
                }
            }
            const uint16_t a = (uint16_t)((text_step & 1 ? 0x2000 : 0) + third + 256 + half * 256);
            o = run(o, a, buf, 255);
            o = run(o, (uint16_t)(a + 255), buf + 255, 1);
        }
        text_step++;
    }
    // the names: two places each side for the TV's edges
    for (int part = 0; part < 3; part++) {
        for (int r = 0; r < 8; r++) {
            const int row = part * 8 + r;
            const int n = (int)strlen(text[row]);
            for (int col = 0; col < 32; col++) {
                const int i = col - 2;
                const char ch = i >= 0 && i < n ? text[row][i] : ' ';
                buf[r * 32 + col] = (uint8_t)(ch >= 32 && ch < 96 ? ch : '?');
            }
        }
        o = run(o, (uint16_t)(0x1800 + part * 256), buf, 255);
        o = run(o, (uint16_t)(0x1800 + part * 256 + 255), buf + 255, 1);
    }
    buf[0] = 0xD0;              // no sprites
    o = run(o, 0x1B00, buf, 1);
    *o++ = 0;
    return (size_t)(o - list);
}

// Show the text for some of the console's frames, or (forever) until RESET
// takes it back to the menu (inside cvf_task)
static void show_text(int lists, bool forever) {
    cvf_task(time_us_32());     // (after a long step, such as the card's start: before asking the console anything)
    uint32_t seen = cvf_frames();
    int sent = 0;
    for (;;) {
        cvf_task(time_us_32());
        cvusb_task();
        if (cvf_frames() == seen) continue;
        seen = cvf_frames();
        const size_t n = text_list();
        while (!cvf_publish(list, n)) cvf_task(time_us_32());
        if (!forever && ++sent >= lists) return;
    }
}

// ---- the ROMs -------------------------------------------------------------------------------
// Read into the PSRAM (a zip whole, or each loose file), found by size and
// CRC (sets name them differently), then built into jr_roms_t in SRAM.
static fs_t fs;
static fs_dir_t dir;
static uint8_t sector[512];
static jr_roms_t roms;
#define PS_ZIP   (CVPSRAM_CACHED + 0x000000)    // a zip, read whole (up to 4 MB)
#define PS_FILES (CVPSRAM_CACHED + 0x400000)    // the files found, 64K each
#define PS_LOOSE (CVPSRAM_CACHED + 0x600000)    // a loose file being looked at
static const uint8_t *found[JR_NROMS];

static bool read_file(const fs_file_t *e, uint8_t *dst) {
    if (fs_open(&fs, e) != FS_OK) return false;
    for (uint32_t off = 0; off < e->size; off += 512) {
        const uint32_t lba = fs_lba(&fs, off / 512);
        if (!lba || !cvsd_read(lba, sector)) return false;
        const uint32_t take = e->size - off < 512 ? (uint32_t)(e->size - off) : 512;
        memcpy(dst + off, sector, take);
        if ((off & 0x3FFF) == 0) cvf_task(time_us_32());
    }
    return true;
}

static bool ends_with(const char *s, const char *tail) {
    const size_t n = strlen(s), m = strlen(tail);
    if (n < m) return false;
    for (size_t i = 0; i < m; i++) {
        char c = s[n - m + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != tail[i]) return false;
    }
    return true;
}

// The card, started (0.1 to 1.5 s without cvf_task(): the one long step,
// at the very start) and mounted. What went wrong, or NULL.
static const char *start_card(void) {
    if (!cvsd_card_in()) return "THERE'S NO CARD IN THE SLOT";
    if (!cvsd_init()) return "THE CARD DIDN'T START";
    if (fs_mount(&fs, cvsd_read) != FS_OK) return "THE CARD HAS NO FAT OR EXFAT";
    return NULL;
}

// what went wrong, or NULL with every ROM found
#define MISSING "SOME OF THE SET IS MISSING:"
static const char *load_roms(void) {
    if (fs_dir_open_path(&fs, &dir, HOME) != FS_OK) return "THERE'S NO " HOME " FOLDER";
    fs_file_t e;
    while (fs_dir_next(&fs, &dir, &e) == 1) {
        if (e.is_dir || dir.lname[0] == '.') continue;
        if (ends_with(dir.lname, ".zip")) {
            if (e.size > 0x400000 || !read_file(&e, PS_ZIP)) continue;
            swzip_entry_t z;
            for (int i = 0; swzip_entry(PS_ZIP, (uint32_t)e.size, i, &z); i++) {
                const int k = jr_rom_find(z.usize, z.crc);
                if (k < 0 || found[k]) continue;
                uint8_t *slot = PS_FILES + k * 0x10000;
                if (swzip_read(&z, slot) && jr_crc32(slot, z.usize, 0) == z.crc) found[k] = slot;
                cvf_task(time_us_32());
            }
        } else if (e.size <= 0x2000) {
            if (!read_file(&e, PS_LOOSE)) continue;
            const int k = jr_rom_find((uint32_t)e.size, jr_crc32(PS_LOOSE, (uint32_t)e.size, 0));
            if (k < 0 || found[k]) continue;
            memcpy(PS_FILES + k * 0x10000, PS_LOOSE, (size_t)e.size);
            found[k] = PS_FILES + k * 0x10000;
        }
        cvf_task(time_us_32());
    }
    for (int k = 0; k < JR_NROMS; k++)
        if (!found[k]) return MISSING;
    return NULL;
}

// ---- the controls ---------------------------------------------------------------------------
// The keys every arcade plugin has (references/porting.md): the stick (four
// ways, as the arcade's: on a diagonal the way that came on last wins),
// either fire to jump, keypad * or # (pad -) a coin, keypad 1 (pad +) one
// player, 2 two.
static uint8_t ways_held, way;
static int coin_left, coin_wait;

static void controls(void) {
    uint8_t joy, fire, lines;
    cvf_input(&joy, &fire, &lines);                 // low = pressed
    const int key = cvin_decode_key(lines);
    uint32_t dirs = 0, buttons = 0;
    gamepad_t p;
    if (cvusb_pad(0, &p)) { dirs = cvusb_pad_dirs(&p); buttons = p.buttons; }
    uint8_t held = 0;
    if (!(joy & 1) || (dirs & GP_UP)) held |= JR_IN_UP;
    if (!(joy & 2) || (dirs & GP_RIGHT)) held |= JR_IN_RIGHT;
    if (!(joy & 4) || (dirs & GP_DOWN)) held |= JR_IN_DOWN;
    if (!(joy & 8) || (dirs & GP_LEFT)) held |= JR_IN_LEFT;
    const uint8_t fresh = held & (uint8_t)~ways_held;
    ways_held = held;
    if (!(held & (held - 1))) way = held;                       // none, or one way
    else if (fresh && !(fresh & (fresh - 1))) way = fresh;      // a diagonal: the way that just came on
    else if (!(way & held)) way = held & (uint8_t)-held;        // (both at once: the lowest bit's)
    const bool jump = (fire & 3) != 3 || (buttons & (GP_SOUTH | GP_EAST | GP_WEST | GP_NORTH));
    const bool coin = key == CVIN_STAR || key == CVIN_HASH || (buttons & GP_MINUS);
    const bool start1 = key == 1 || (buttons & GP_PLUS), start2 = key == 2;
    // a coin: the switch shut for 6 lists from the press, and the key up 3 before the next
    if (coin && !coin_wait) coin_left = 6;
    if (coin) coin_wait = 3;
    else if (coin_wait) coin_wait--;
    const bool coin_switch = coin_left > 0;
    if (coin_left) coin_left--;
    jrp_controls((uint8_t)(way | (jump ? JR_IN_JUMP : 0)),
                 (uint8_t)((start1 ? JR_IN_START1 : 0) | (start2 ? JR_IN_START2 : 0) | (coin_switch ? JR_IN_COIN : 0)));
}

// ---- the note --------------------------------------------------------------------------------
// After RESET, the menu's bottom line: pictures a second, lists the console
// ran twice (late), the slowest list's making, core 1's bus counts.
static uint32_t late, slowest_us;

static void note(uint32_t pictures) {
    uint32_t unsel, early, unhdr;
    cvf_bus_counts(&unsel, &early, &unhdr);
    static char s[48];
    snprintf(s, sizeof s, "DKJR %lu/s L%lu %lu.%lums %lu/%lu/%lu", (unsigned long)pictures, (unsigned long)late,
             (unsigned long)(slowest_us / 1000), (unsigned long)(slowest_us / 100 % 10), (unsigned long)unsel,
             (unsigned long)early, (unsigned long)unhdr);
    s[32] = 0;
    cvf_note(s);
}

int main(void) {
    static const cvf_config_t cfg = {gbkernel_bin, GBKERNEL_BIN_LEN, handoff_bin, HANDOFF_BIN_LEN};
    cvf_start(&cfg);
    cvd_start();
    cvf_note("DONKEY KONG JR. " VERSION);
    cvusb_start();
    const char *err = start_card();

    say(2, "DONKEY KONG JR.");
    say(4, "LOADING " HOME);
    show_text(8, false);

    if (!err) err = !cvpsram_init() ? "THE PSRAM DIDN'T ANSWER" : load_roms();
    if (err) {
        say(4, err);
        if (!strcmp(err, MISSING))
            for (int k = 0, row = 6; k < JR_NROMS && row < 20; k++)
                if (!found[k]) say(row++, jr_rom_list[k].name);
        say(20, "PUT DKONGJR.ZIP, MAME'S SET,");
        say(21, "OR ITS FILES IN " HOME);
        say(23, "RESET: BACK TO THE MENU");
        cvf_note("DKJR: the set isn't all there");
        show_text(0, true);
    }
    jr_roms_build(&roms, found);
    jrp_init(&roms);

    bool pending = false;
    uint32_t made = 0, published = 0, repeats0 = 0, pics0 = 0, sec_start = time_us_32();
    cvf_task(sec_start);
    const uint32_t frames0 = cvf_frames();
    for (;;) {
        const uint32_t t = time_us_32();
        cvf_task(t);
        cvusb_task();
        // never drop a list (jrcv's copy of the console's video RAM counts on it)
        if (pending) {
            if (!cvf_publish(list, list_len)) continue;
            published++;
        }
        pending = false;
        // lists the console ran again, none new being there in time (counted from the 30th)
        const uint32_t repeats = (cvf_frames() - frames0) - (published - (cvf_waiting() ? 1 : 0));
        if (made == 30) repeats0 = repeats;
        if (made > 30 && repeats - repeats0 > late) late = repeats - repeats0;
        // the next, one ahead: it waits for the console to take the last
        controls();
        const uint32_t t0 = time_us_32();
        list_len = jrp_list(list);
        const uint32_t took = time_us_32() - t0;
        if (made > 30 && took > slowest_us) slowest_us = took;
        pending = true;
        made++;
        if (t - sec_start >= 1000000) {
            note(jrp_stats.pictures - pics0);
            pics0 = jrp_stats.pictures;
            sec_start = t;
        }
    }
}
