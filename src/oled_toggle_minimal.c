/* SPDX-License-Identifier: MIT
 * Source-local toggle. Existing behavior node/name stays unchanged so saved
 * Studio bindings keep resolving. One press toggles; release does nothing.
 */
#define DT_DRV_COMPAT zmk_behavior_oled_toggle
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>

extern void sofle_status_toggle(void);

static int pressed(struct zmk_behavior_binding *binding,
                    struct zmk_behavior_binding_event event) {
    sofle_status_toggle();
    return ZMK_BEHAVIOR_OPAQUE;
}
static int released(struct zmk_behavior_binding *binding,
                     struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}
static const struct behavior_driver_api api = {
    .binding_pressed = pressed,
    .binding_released = released,
    .locality = BEHAVIOR_LOCALITY_EVENT_SOURCE,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
