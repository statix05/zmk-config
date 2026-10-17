/* SPDX-License-Identifier: MIT
 * Central is the single owner of explicit shared OLED state. Preserve existing
 * behavior_oled_toggle device name so SETTINGS_TABLE bindings remain valid.
 */
#define DT_DRV_COMPAT zmk_behavior_oled_toggle
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include "sofle_display_sync.h"

static int pressed(struct zmk_behavior_binding *binding,
                    struct zmk_behavior_binding_event event) {
    /* A saved old Studio AUX1/right OLED binding must not retain the removed
     * action. Preserve other remaps and never reset/rewrite the settings table. */
    if (event.layer == 1 && event.position == 43) return ZMK_BEHAVIOR_OPAQUE;
    sofle_display_toggle_global();
    return ZMK_BEHAVIOR_OPAQUE;
}
static int released(struct zmk_behavior_binding *binding,
                     struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}
static const struct behavior_driver_api api = {
    .binding_pressed = pressed,
    .binding_released = released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
