/* SPDX-License-Identifier: MIT
 * Small deterministic renderer: no LVGL children, animation, fonts, or heap.
 * This exact file is compiled for firmware AND the native preview tests.
 */
#include "sofle_status_render.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct glyph { char character; uint8_t rows[7]; };
/* Original 5x7 row masks, most-significant of five bits is the left pixel. */
static const struct glyph glyphs[] = {
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,2,18,12}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
    {'%',{24,25,2,4,8,19,3}}, {'-',{0,0,0,31,0,0,0}},
    {'+',{0,4,4,31,4,4,0}}, {'?',{14,17,1,2,4,0,4}},
    {' ',{0,0,0,0,0,0,0}}, {'_',{0,0,0,0,0,0,31}},
    {'.',{0,0,0,0,0,12,12}}, {'/',{1,1,2,4,8,16,16}},
};

static const uint8_t *glyph_rows(char c) {
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    for (size_t i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++) {
        if (glyphs[i].character == c) return glyphs[i].rows;
    }
    return glyph_rows('?');
}

static void pixel(uint8_t *frame, int x, int y) {
    if (x < 0 || x >= 32 || y < 0 || y >= 128) return;
    /* One clockwise rotation, explicit bounds: no oversized virtual canvas. */
    int px = 127 - y;
    int py = x;
    frame[8 + py * 16 + px / 8] |= (uint8_t)(0x80u >> (px % 8));
}

static void centered_text(uint8_t *frame, int y, const char *text) {
    size_t count = 0;
    while (count < 5 && text[count]) count++;
    int x = (32 - ((int)count * 6 - 1)) / 2;
    for (size_t i = 0; i < count; i++) {
        const uint8_t *rows = glyph_rows(text[i]);
        for (int dy = 0; dy < 7; dy++) {
            for (int dx = 0; dx < 5; dx++) {
                if (rows[dy] & (1u << (4 - dx))) pixel(frame, x + dx, y + dy);
            }
        }
        x += 6;
    }
}

void sofle_status_render(uint8_t frame[SOFLE_FRAME_BYTES],
                         const struct sofle_status_view *state,
                         const uint8_t logo_pixels[128], bool invert) {
    memset(frame, 0, SOFLE_FRAME_BYTES);
    /* LVGL indexed1bit palette, BGRA. Only swap colours for DTS inversion. */
    for (int c = 0; c < 3; c++) {
        frame[c] = invert ? 255 : 0;
        frame[4 + c] = invert ? 0 : 255;
    }
    frame[3] = frame[7] = 255;
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            if (logo_pixels[y * 4 + x / 8] & (0x80u >> (x % 8))) pixel(frame, x, y);
        }
    }
    centered_text(frame, 40, state->usb_powered ? "USB" : "BAT");
    char charge[6] = "--%";
    if (!state->usb_powered && state->battery_valid) {
        snprintf(charge, sizeof(charge), "%u%%", (unsigned)state->battery_percent);
    }
    centered_text(frame, 50, charge);
    if (state->central) {
        centered_text(frame, 70, state->usb_output ? "OUT U" : "OUT B");
        char profile[6];
        /* BT_SEL indices are deliberately zero-based, not silently +1. */
        snprintf(profile, sizeof(profile), "BT%u%c", (unsigned)(state->bt_profile % 10),
                 state->connected ? '+' : (state->bonded ? '-' : '?'));
        centered_text(frame, 82, profile);
        centered_text(frame, 100, "LAYER");
        centered_text(frame, 112, state->layer);
    } else {
        centered_text(frame, 70, "LINK");
        centered_text(frame, 82, state->connected ? "ON" : "OFF");
        centered_text(frame, 112, "RIGHT");
    }
}
