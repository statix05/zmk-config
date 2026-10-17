/* SPDX-License-Identifier: MIT
 * PandaKB Sofle: ONE status image, no nice_oled screen/children/animations.
 * UI work runs only on ZMK's display queue; ADC uses ZMK's low-priority queue.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <string.h>
#include <stdio.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/activity.h>
#include <zmk/workqueue.h>
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/activity_state_changed.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#else
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#endif
#include "sofle_status_render.h"
#include "sofle_union_logo.h"
#include "sofle_display_sync.h"

LOG_MODULE_REGISTER(sofle_status, CONFIG_ZMK_LOG_LEVEL);
BUILD_ASSERT(DT_PROP(DT_CHOSEN(zephyr_display), width) == SOFLE_PANEL_WIDTH,
             "Sofle status screen requires the existing 128x32 OLED");
BUILD_ASSERT(DT_PROP(DT_CHOSEN(zephyr_display), height) == SOFLE_PANEL_HEIGHT,
             "Do not silently change display resolution");

static lv_obj_t *status_image;
static uint8_t frame[SOFLE_FRAME_BYTES];
static lv_image_dsc_t frame_desc = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_I1,
    .header.w = SOFLE_PANEL_WIDTH,
    .header.h = SOFLE_PANEL_HEIGHT,
    .header.stride = SOFLE_PANEL_WIDTH / 8,
    .data_size = sizeof(frame),
    .data = frame,
};

/* RAM only: neither toggle nor rendering writes settings or Bluetooth bonds. */
static atomic_t battery_valid;
static atomic_t battery_percent;
static atomic_t power_generation;
static bool panel_on = true; /* ZMK turns panel on once after screen creation. */
static bool screen_ready; /* Apply manual state AFTER ZMK's initial unblank. */
BUILD_ASSERT(IS_ENABLED(CONFIG_USB_DEVICE_STACK),
             "Both OLEDs require USB power detection, including peripheral");

static bool usb_powered(void) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    return zmk_usb_is_powered();
#else
    return false;
#endif
}

struct status_snapshot {
    struct sofle_status_view view;
    bool active;
    bool manual_off;
};

static struct status_snapshot capture_state(const zmk_event_t *event) {
    struct status_snapshot state = {0};
    state.manual_off = !sofle_display_manual_enabled();
    state.active = sofle_display_activity_active();
    state.view.usb_powered = usb_powered();
    state.view.battery_valid = atomic_get(&battery_valid) && !state.view.usb_powered;
    state.view.battery_percent = atomic_get(&battery_percent);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    state.view.central = true;
    state.view.usb_output = zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_USB;
    state.view.bt_profile = zmk_ble_active_profile_index();
    state.view.connected = zmk_ble_active_profile_is_connected();
    state.view.bonded = !zmk_ble_active_profile_is_open();
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    const char *name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index));
    if (name && *name) {
        snprintf(state.view.layer, sizeof(state.view.layer), "%.5s", name);
    } else {
        snprintf(state.view.layer, sizeof(state.view.layer), "%u", (unsigned)index);
    }
#else
    state.view.connected = zmk_split_bt_peripheral_is_connected();
#endif
    return state;
}

static void update_display(struct status_snapshot state) {
    if (!status_image) return;
    const struct device *panel = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(panel)) return;
    bool wanted_on = state.active && !state.manual_off;
    uint8_t next_frame[SOFLE_FRAME_BYTES];
    sofle_status_render(next_frame, &state.view, sofle_union_logo.data + 8,
                        DT_PROP_OR(DT_CHOSEN(zephyr_display), inversion_on, 0));
    if (memcmp(frame, next_frame, sizeof(frame)) != 0) {
        memcpy(frame, next_frame, sizeof(frame));
        lv_image_cache_drop(&frame_desc);
        lv_obj_invalidate(status_image);
    }
    if (screen_ready && wanted_on != panel_on) {
        /* On wake flush current information BEFORE unblank, not an old frame. */
        if (wanted_on) lv_refr_now(NULL);
        int rc = wanted_on ? display_blanking_off(panel) : display_blanking_on(panel);
        if (!rc) panel_on = wanted_on;
        else LOG_ERR("OLED panel blanking failed: %d", rc);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(sofle_status_widget, struct status_snapshot,
                            update_display, capture_state);

static void sample_battery(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(sample_work, sample_battery);

static void schedule_sample(uint32_t delay_ms) {
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &sample_work,
                                K_MSEC(delay_ms));
}

static void sample_battery(struct k_work *work) {
    atomic_val_t generation = atomic_get(&power_generation);
    /* Stop our ADC polling on USB and idle. A USB/activity event restarts it.
     * Same queue as ZMK battery.c serializes access to the shared sensor. */
    if (usb_powered() || !sofle_display_activity_active()) return;
    const struct device *battery = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));
    struct sensor_value soc;
    int rc = device_is_ready(battery)
                 ? sensor_sample_fetch_chan(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE)
                 : -ENODEV;
    if (!rc) rc = sensor_channel_get(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &soc);
    /* A USB transition during conversion invalidates this sample. */
    if (generation != atomic_get(&power_generation) || usb_powered()) return;
    if (!rc && soc.val1 >= 0 && soc.val1 <= 100) {
        atomic_set(&battery_percent, soc.val1);
        atomic_set(&battery_valid, 1);
    } else {
        atomic_set(&battery_valid, 0);
        LOG_WRN("Local battery estimate unavailable: %d", rc);
    }
    sofle_status_widget_cb(NULL);
    schedule_sample(CONFIG_SOFLE_STATUS_BATTERY_INTERVAL_MS);
}

static int screen_event(const zmk_event_t *event) {
    if (as_zmk_usb_conn_state_changed(event)) {
        atomic_inc(&power_generation);
        atomic_set(&battery_valid, 0);
        if (!usb_powered()) schedule_sample(CONFIG_SOFLE_STATUS_BATTERY_SETTLE_MS);
    }
    if (as_zmk_activity_state_changed(event)) {
        if (zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE && !usb_powered()) {
            schedule_sample(CONFIG_SOFLE_STATUS_BATTERY_SETTLE_MS);
        }
    }
    return sofle_status_widget_cb(event);
}
ZMK_LISTENER(sofle_status_input, screen_event);
ZMK_SUBSCRIPTION(sofle_status_input, zmk_usb_conn_state_changed);
ZMK_SUBSCRIPTION(sofle_status_input, zmk_activity_state_changed);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
ZMK_SUBSCRIPTION(sofle_status_input, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(sofle_status_input, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(sofle_status_input, zmk_layer_state_changed);
#else
ZMK_SUBSCRIPTION(sofle_status_input, zmk_split_peripheral_status_changed);
#endif

/* Called by shared state synchronization; publication uses the display queue.
 * Built-in BLANK_ON_IDLE is disabled: there is no competing wake/unblank path
 * and no delayed 200ms reblank that briefly lights a manually-off display. */
void sofle_status_refresh(void) {
    sofle_status_widget_cb(NULL);
    if (sofle_display_activity_active() && !usb_powered() &&
        !k_work_delayable_is_pending(&sample_work)) {
        schedule_sample(CONFIG_SOFLE_STATUS_BATTERY_SETTLE_MS);
    }
}

/* Queued to the same display queue as initialize_display(). It cannot run
 * until core has finished loading the screen and doing its one initial unblank.
 * This also applies a toggle pressed before the screen existed. */
static void finish_initialization(struct k_work *work) {
    panel_on = true;
    screen_ready = true;
    update_display(capture_state(NULL));
}
static K_WORK_DEFINE(finish_init_work, finish_initialization);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, SOFLE_PANEL_WIDTH, SOFLE_PANEL_HEIGHT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    status_image = lv_image_create(screen);
    lv_obj_remove_style_all(status_image);
    lv_image_set_src(status_image, &frame_desc);
    lv_obj_set_pos(status_image, 0, 0);
    sofle_display_control_start();
    sofle_status_widget_init();
    k_work_submit_to_queue(zmk_display_work_q(), &finish_init_work);
    if (!usb_powered()) schedule_sample(CONFIG_SOFLE_STATUS_BATTERY_SETTLE_MS);
    LOG_INF("Sofle minimal OLED: single 128x32 frame, no stock art");
    return screen;
}
