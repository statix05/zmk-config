/*
 * Copyright (c) 2026 STATIX
 * SPDX-License-Identifier: MIT
 *
 * OLED toggle behavior: turns the LOCAL half's display off/on on each press.
 *
 * - Locality is BEHAVIOR_LOCALITY_EVENT_SOURCE, so the behavior runs on the
 *   half where the physical key lives (left encoder push -> left half,
 *   right encoder push -> right half). Each half toggles only its own OLED.
 * - Only the display panel is blanked (display_blanking_on/off) — the
 *   controller, BT, key scanning and the other half are untouched.
 * - A manual "off" survives activity events: if ZMK's display code unblanks
 *   the panel on activity, we re-assert blanking shortly after. Manual state
 *   is kept in RAM only (no flash writes per toggle). After a deep sleep /
 *   power cycle the display starts ON again.
 */

#define DT_DRV_COMPAT zmk_behavior_oled_toggle

#ifdef CONFIG_DISPLAY

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>

LOG_MODULE_REGISTER(oled_toggle, CONFIG_ZMK_LOG_LEVEL);

static bool manual_off;

static void reblank_work_handler(struct k_work *work);

/* Re-assert manual "off" after ZMK's activity handler has had its chance to
 * unblank the display on wake. 200 ms is enough to lose that race on purpose. */
static K_WORK_DELAYABLE_DEFINE(reblank_work, reblank_work_handler);

static const struct device *get_display(void) {
    const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display)) {
        LOG_ERR("Display device not ready");
        return NULL;
    }
    return display;
}

static void reblank_work_handler(struct k_work *work) {
    const struct device *display = get_display();
    if (!display || !manual_off) {
        return;
    }
    int rc = display_blanking_on(display);
    if (rc != 0) {
        LOG_ERR("Failed to re-assert OLED off: %d", rc);
    }
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *display = get_display();
    if (display == NULL) {
        return -ENODEV;
    }

    manual_off = !manual_off;

    int rc = manual_off ? display_blanking_on(display) : display_blanking_off(display);
    if (rc != 0) {
        LOG_ERR("Failed to %s OLED: %d", manual_off ? "blank" : "unblank", rc);
        manual_off = !manual_off; // keep state in sync with what actually happened
        return rc;
    }

    if (manual_off) {
        k_work_cancel_delayable(&reblank_work);
    }
    LOG_INF("OLED turned %s", manual_off ? "OFF" : "ON");
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static int activity_handler(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return -ENOTSUP;
    }
    if (!manual_off) {
        return 0;
    }
    // Re-assert manual "off" shortly after the display code may have unblanked.
    k_work_reschedule(&reblank_work, K_MSEC(200));
    return 0;
}

ZMK_LISTENER(oled_toggle, activity_handler);
ZMK_SUBSCRIPTION(oled_toggle, zmk_activity_state_changed);

static const struct behavior_driver_api behavior_oled_toggle_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_EVENT_SOURCE,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_oled_toggle_driver_api);

#endif /* IS_ENABLED(CONFIG_DISPLAY) */
