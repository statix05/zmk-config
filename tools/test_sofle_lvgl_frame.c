/* Real LVGL 9 software renderer -> packed I1 display flush, no mock API. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <lvgl.h>
#include "sofle_status_render.h"
#include "sofle_union_logo.h"

#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR != 3
#error "Run against the selected manifest's pinned LVGL 9.3"
#endif

static uint8_t pixels[SOFLE_FRAME_BYTES] __attribute__((aligned(4)));
static uint8_t output[SOFLE_PANEL_WIDTH * SOFLE_PANEL_HEIGHT];
static unsigned flush_count;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
    assert(lv_display_get_color_format(display) == LV_COLOR_FORMAT_I1);
    assert(area->x1 % 8 == 0 && (area->x2 + 1) % 8 == 0);
    const uint32_t stride = lv_draw_buf_width_to_stride(lv_area_get_width(area),
                                                       LV_COLOR_FORMAT_I1);
    /* LVGL 9 I1 flush buffers retain 8 reserved palette bytes. */
    data += 8;
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            assert(x >= 0 && x < SOFLE_PANEL_WIDTH && y >= 0 && y < SOFLE_PANEL_HEIGHT);
            const unsigned offset = x - area->x1;
            output[y * SOFLE_PANEL_WIDTH + x] =
                !!(data[(y - area->y1) * stride + offset / 8] & (0x80 >> (offset % 8)));
        }
    }
    flush_count++;
    lv_display_flush_ready(display);
}

int main(void) {
    lv_init();
    lv_display_t *display = lv_display_create(SOFLE_PANEL_WIDTH, SOFLE_PANEL_HEIGHT);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_I1);
    lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, SOFLE_PANEL_WIDTH, SOFLE_PANEL_HEIGHT);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *image = lv_image_create(screen);
    lv_obj_remove_style_all(image);
    static uint8_t frame[SOFLE_FRAME_BYTES];
    static lv_image_dsc_t desc = {
        .header.magic = LV_IMAGE_HEADER_MAGIC, .header.cf = LV_COLOR_FORMAT_I1,
        .header.w = SOFLE_PANEL_WIDTH, .header.h = SOFLE_PANEL_HEIGHT,
        .header.stride = SOFLE_PANEL_WIDTH / 8,
        .data_size = sizeof(frame), .data = frame,
    };
    assert(sizeof(frame) == 520);
    assert(sofle_union_logo.header.magic == LV_IMAGE_HEADER_MAGIC);
    assert(sofle_union_logo.header.cf == LV_COLOR_FORMAT_I1);
    assert(sofle_union_logo.header.w == 32 && sofle_union_logo.header.h == 32);
    assert(sofle_union_logo.header.stride == 4 && sofle_union_logo.data_size == 136);
    lv_image_set_src(image, &desc);
    lv_obj_set_pos(image, 0, 0);
    lv_screen_load(screen);
    static const uint8_t percents[] = {0, 1, 9, 10, 99, 100};
    static const char *layers[] = {"BAS", "AUX1", "RAI", "ABCDE"};
    unsigned states = 0, frames = 0;
    unsigned long mismatches = 0;
    for (int central = 0; central < 2; central++)
    for (int usb = 0; usb < 2; usb++)
    for (int valid = 0; valid < 2; valid++)
    for (int connected = 0; connected < 2; connected++)
    for (unsigned p = 0; p < sizeof(percents); p++)
    for (unsigned l = 0; l < sizeof(layers) / sizeof(layers[0]); l++) {
        struct sofle_status_view state = {
            .central = central, .usb_powered = usb, .battery_valid = valid,
            .battery_percent = percents[p], .usb_output = usb, .bt_profile = 4,
            .connected = connected, .bonded = true,
        };
        strcpy(state.layer, layers[l]);
        states++;
        for (int inverted = 0; inverted < 2; inverted++) {
            sofle_status_render(frame, &state, sofle_union_logo.data + 8, inverted);
            /* Same descriptor/source throughout: mirrors firmware RAM mutation. */
            lv_image_cache_drop(&desc);
            lv_obj_invalidate(image);
            memset(output, 0xa5, sizeof(output));
            const unsigned before = flush_count;
            lv_refr_now(NULL);
            assert(flush_count > before);
            unsigned count = 0;
            for (int y = 0; y < SOFLE_PANEL_HEIGHT; y++)
            for (int x = 0; x < SOFLE_PANEL_WIDTH; x++) {
                const int bit = !!(frame[8 + y * 16 + x / 8] & (0x80 >> (x % 8)));
                if (output[y * SOFLE_PANEL_WIDTH + x] != (bit ^ inverted)) count++;
            }
            if (count) fprintf(stderr, "state=%u inverted=%d mismatches=%u\n", states, inverted, count);
            mismatches += count;
            assert(count == 0);
            assert(lv_obj_get_child_count(screen) == 1);
            frames++;
        }
    }
    printf("LVGL %d.%d.%d native I1: states=%u frames=%u flushes=%u mismatches=%lu children=%u\n",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
           states, frames, flush_count, mismatches, (unsigned)lv_obj_get_child_count(screen));
    lv_deinit();
    return 0;
}
