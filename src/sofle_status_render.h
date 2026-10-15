/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SOFLE_PANEL_WIDTH 128
#define SOFLE_PANEL_HEIGHT 32
#define SOFLE_FRAME_BYTES (8 + SOFLE_PANEL_WIDTH * SOFLE_PANEL_HEIGHT / 8)

/* All fields describe this controller, except layer/profile on central. */
struct sofle_status_view {
    bool central;
    bool usb_powered;
    bool battery_valid;
    uint8_t battery_percent;
    bool usb_output;
    uint8_t bt_profile; /* zero-based, as in BT_SEL */
    bool connected;
    bool bonded;
    char layer[6]; /* five printable ASCII characters, NUL terminated */
};

/* Render one physical 128x32 indexed1bit image. Logical reading layout is
 * portrait 32x128, rotated clockwise once (as the previous OLED interface).
 * invert compensates for the existing DTS inversion-on without changing DTS.
 * logo_pixels points at the 128 pixel bytes after the indexed1bit palette. */
void sofle_status_render(uint8_t frame[SOFLE_FRAME_BYTES],
                         const struct sofle_status_view *state,
                         const uint8_t logo_pixels[128], bool invert);
