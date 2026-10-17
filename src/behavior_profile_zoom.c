/* SPDX-License-Identifier: MIT
 * Browser zoom as recipient-pinned temporary report overlays, not a global
 * modifier macro. The live HID state and held physical modifiers are untouched.
 */
#define DT_DRV_COMPAT statix_profile_zoom
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/event_manager.h>
#include <zmk/hid.h>
#include <zmk/hog.h>
#include <zmk/usb_hid.h>
#include <zmk/usb.h>
#include "sofle_zoom_plan.h"

LOG_MODULE_REGISTER(sofle_profile_zoom, CONFIG_ZMK_LOG_LEVEL);
BUILD_ASSERT(IS_ENABLED(CONFIG_ZMK_HID_REPORT_TYPE_HKRO) && CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE == 6,
             "Snapshot zoom requires the declared HKRO6 bridge contract");
BUILD_ASSERT(sizeof(struct zmk_hid_keyboard_report_body) == SOFLE_ZOOM_REPORT_SIZE);
BUILD_ASSERT(CONFIG_SOFLE_MAC1_BT_PROFILE != CONFIG_SOFLE_MAC2_BT_PROFILE &&
             CONFIG_SOFLE_MAC1_BT_PROFILE != CONFIG_SOFLE_DONGLE_BT_PROFILE &&
             CONFIG_SOFLE_MAC2_BT_PROFILE != CONFIG_SOFLE_DONGLE_BT_PROFILE);
struct zoom_gesture {
    struct zmk_endpoint_instance endpoint;
    struct bt_conn *conn;
    uint8_t profile;
    uint8_t direction;
};
K_MSGQ_DEFINE(zoom_gestures, sizeof(struct zoom_gesture), 16, 4);
static struct zoom_gesture active;
static atomic_t pressed;
static unsigned release_retries;
static void start_gesture(struct k_work *work);
static void finish_gesture(struct k_work *work);
static K_WORK_DEFINE(zoom_start_work, start_gesture);
static K_WORK_DELAYABLE_DEFINE(zoom_release_work, finish_gesture);

static bool same_endpoint(const struct zoom_gesture *gesture) {
    return zmk_endpoint_instance_eq(gesture->endpoint, zmk_endpoint_get_selected());
}
static int send_snapshot(const struct zoom_gesture *gesture, const uint8_t bytes[8]) {
    struct zmk_hid_keyboard_report_body body;
    memcpy(&body, bytes, sizeof(body));
    if (gesture->endpoint.transport == ZMK_TRANSPORT_BLE)
        return zmk_hog_send_keyboard_snapshot(gesture->conn, &body);
    if (gesture->endpoint.transport == ZMK_TRANSPORT_USB)
        return zmk_usb_hid_send_keyboard_snapshot(&body);
    return -ENOTSUP;
}
static void release_reference(struct zoom_gesture *gesture) {
    if (gesture->conn) { bt_conn_unref(gesture->conn); gesture->conn = NULL; }
}
static void finish_gesture(struct k_work *work) {
    if (atomic_get(&pressed)) {
        uint8_t restore[8];
        /* If output changed, release EVERYTHING at the OLD recipient only.
         * Otherwise restore fresh live state, including physical held keys. */
        sofle_zoom_release_body(restore,
            (const uint8_t *)&zmk_hid_get_keyboard_report()->body, same_endpoint(&active));
        int rc = send_snapshot(&active, restore);
        if (rc && rc != -ENOTCONN && rc != -ENODEV) {
            release_retries++;
            if (active.conn && release_retries >= 32) {
                /* A failed release must not leave a modifier at an old peer.
                 * Disconnect that exact connection only; bonds are untouched. */
                bt_conn_disconnect(active.conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            } else {
                /* USB may be suspended: finish the old report when it resumes.
                 * This delays our gestures, never scanning or ordinary input. */
                k_work_reschedule_for_queue(zmk_hog_work_queue(), &zoom_release_work,
                                            K_MSEC(release_retries < 10 ? 5 : 100));
                return;
            }
        }
        release_reference(&active);
        atomic_clear(&pressed);
    }
    k_work_submit_to_queue(zmk_hog_work_queue(), &zoom_start_work);
}
static void start_gesture(struct k_work *work) {
    if (atomic_get(&pressed)) return;
    struct zoom_gesture gesture;
    while (!k_msgq_get(&zoom_gestures, &gesture, K_NO_WAIT)) {
        uint8_t temporary[8];
        int rc = same_endpoint(&gesture) ?
            sofle_zoom_overlay(temporary,
                (const uint8_t *)&zmk_hid_get_keyboard_report()->body,
                gesture.profile, gesture.direction) : -ECANCELED;
        if (!rc) rc = send_snapshot(&gesture, temporary);
        if (rc) { release_reference(&gesture); continue; }
        active = gesture;
        release_retries = 0;
        atomic_set(&pressed, 1);
        k_work_reschedule_for_queue(zmk_hog_work_queue(), &zoom_release_work,
                                    K_MSEC(CONFIG_SOFLE_ZOOM_TAP_MS));
        return;
    }
}
static int on_press(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    if (binding->param1 > SOFLE_ZOOM_OUT) return -EINVAL;
    struct zoom_gesture gesture = {
        .endpoint = zmk_endpoint_get_selected(),
        .profile = zmk_ble_active_profile_index(),
        .direction = binding->param1,
    };
    if (!sofle_profile_name(gesture.profile)) return ZMK_BEHAVIOR_OPAQUE;
    if (gesture.endpoint.transport == ZMK_TRANSPORT_BLE) {
        gesture.profile = gesture.endpoint.ble.profile_index;
        const bt_addr_le_t *address = zmk_ble_profile_address(gesture.profile);
        if (!address) return -ENOTCONN;
        gesture.conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, address);
        if (!gesture.conn) return -ENOTCONN;
    } else if (gesture.endpoint.transport != ZMK_TRANSPORT_USB) return -ENOTCONN;
    int rc = k_msgq_put(&zoom_gestures, &gesture, K_NO_WAIT);
    if (rc) { release_reference(&gesture); return rc; }
    k_work_submit_to_queue(zmk_hog_work_queue(), &zoom_start_work);
    return ZMK_BEHAVIOR_OPAQUE;
}
static int on_release(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE; /* One tap per press; physical release does nothing. */
}
static int endpoint_changed(const zmk_event_t *event) {
    if (as_zmk_endpoint_changed(event) && atomic_get(&pressed))
        k_work_reschedule_for_queue(zmk_hog_work_queue(), &zoom_release_work, K_NO_WAIT);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(sofle_zoom_endpoint, endpoint_changed);
ZMK_SUBSCRIPTION(sofle_zoom_endpoint, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata directions[] = {
    {.display_name = "Zoom in", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = SOFLE_ZOOM_IN},
    {.display_name = "Zoom out", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = SOFLE_ZOOM_OUT},
};
static const struct behavior_parameter_metadata_set parameter_sets[] = {
    {.param1_values = directions, .param1_values_len = ARRAY_SIZE(directions)},
};
static const struct behavior_parameter_metadata metadata = {
    .sets = parameter_sets, .sets_len = ARRAY_SIZE(parameter_sets),
};
#endif
static const struct behavior_driver_api api = {
    .binding_pressed = on_press, .binding_released = on_release,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};
BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
