/* SPDX-License-Identifier: MIT
 * Central-owned display intent. Explicit SET+ACK, request on reconnect, no
 * remote toggle, no flash writes, no polling after handshake acknowledgement.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/transport/types.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/central.h>
#else
#include <zmk/split/peripheral.h>
#include <zmk/split/bluetooth/peripheral.h>
#endif
#include "sofle_display_protocol.h"
#include "sofle_display_sync.h"

LOG_MODULE_REGISTER(sofle_display_sync, CONFIG_ZMK_LOG_LEVEL);
BUILD_ASSERT(IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT), "OLED sync requires split relay");
BUILD_ASSERT(CONFIG_ZMK_SPLIT_RELAY_EVENT_TYPE_NAME_LEN >= 4, "Relay identifier too short");
BUILD_ASSERT(CONFIG_ZMK_SPLIT_RELAY_EVENT_DATA_LEN >= SOFLE_DISPLAY_WIRE_SIZE,
             "Relay cannot carry explicit OLED state");
#define RELAY_ID "oled"
K_MUTEX_DEFINE(control_lock);
static struct sofle_display_control control = {
    .manual_enabled = true, .active = true,
    .sequence_valid = IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL),
};
static atomic_t started;
static unsigned retries;
static bool pending;
static bool connected;
static void retry_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(retry_work, retry_handler);

static struct sofle_display_control snapshot(void) {
    k_mutex_lock(&control_lock, K_FOREVER);
    struct sofle_display_control copy = control;
    k_mutex_unlock(&control_lock);
    return copy;
}
static uint8_t state_flags(struct sofle_display_control state) {
    return (state.manual_enabled ? SOFLE_DISPLAY_MANUAL : 0) |
           (state.active ? SOFLE_DISPLAY_ACTIVE : 0);
}

static int transmit(struct sofle_display_message message) {
    uint8_t wire[SOFLE_DISPLAY_WIRE_SIZE];
    int size = sofle_display_encode(wire, sizeof(wire), &message);
    if (size < 0) return size;
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    struct zmk_split_relay_event_payload payload = {0};
    strcpy(payload.event_type, RELAY_ID);
    payload.header.event_type_size = strlen(RELAY_ID);
    payload.header.event_data_size = size;
    memcpy(payload.event_data, wire, size);
    return zmk_split_central_send_relay_event(&payload);
#else
    struct zmk_split_transport_peripheral_event event = {
        .type = ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_RELAY_EVENT,
    };
    strcpy(event.data.relay_event.event_type, RELAY_ID);
    event.data.relay_event.header.event_type_size = strlen(RELAY_ID);
    event.data.relay_event.header.event_data_size = size;
    memcpy(event.data.relay_event.event_data, wire, size);
    return zmk_split_peripheral_report_event(&event);
#endif
}

static void begin_handshake(void) {
    k_mutex_lock(&control_lock, K_FOREVER);
    pending = true;
    retries = 0;
    k_mutex_unlock(&control_lock);
    /* Work queue execution avoids sending from a Bluetooth RX/event callback. */
    k_work_reschedule(&retry_work, K_MSEC(50));
}

static void retry_handler(struct k_work *work) {
    k_mutex_lock(&control_lock, K_FOREVER);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    bool send = pending;
#else
    /* Retry REQUEST until SET, including slow discovery/encryption. Back off
     * after the initial burst; normal steady state sends no periodic traffic. */
    bool send = pending && connected && !control.sequence_valid;
#endif
    struct sofle_display_control copy = control;
    if (send && retries < 12) retries++;
    bool fast_retry = retries < 12;
    k_mutex_unlock(&control_lock);
    if (!send) return;
    struct sofle_display_message message = {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        .kind = SOFLE_DISPLAY_SET,
        .flags = state_flags(copy),
        .sequence = copy.sequence,
#else
        .kind = SOFLE_DISPLAY_REQUEST,
#endif
    };
    int rc = transmit(message);
    if (rc) LOG_DBG("OLED relay pending: %d", rc);
    /* Central retries are bounded. Peripheral requests back off until the
     * first authoritative SET; an ACKed link has no heartbeat/polling. */
    k_work_reschedule(&retry_work, K_MSEC(fast_retry ? 500 : 5000));
}

bool sofle_display_manual_enabled(void) { return snapshot().manual_enabled; }
bool sofle_display_activity_active(void) {
    struct sofle_display_control state = snapshot();
#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    /* A standalone right half can show its own battery before its first sync. */
    if (!state.sequence_valid) return zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE;
#endif
    return state.active;
}

void sofle_display_toggle_global(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    k_mutex_lock(&control_lock, K_FOREVER);
    control.manual_enabled = !control.manual_enabled;
    control.active = zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE;
    control.sequence++;
    k_mutex_unlock(&control_lock);
    sofle_status_refresh();
    begin_handshake();
#endif
}

static int control_event(const zmk_event_t *event) {
    const struct zmk_relay_event_received *relay = as_zmk_relay_event_received(event);
    if (relay && strcmp(relay->event_name, RELAY_ID) == 0) {
        struct sofle_display_message message;
        if (sofle_display_decode(&message, relay->event_data, relay->event_data_size))
            return ZMK_EV_EVENT_HANDLED;
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        if (message.kind == SOFLE_DISPLAY_REQUEST) {
            begin_handshake();
        } else if (message.kind == SOFLE_DISPLAY_ACK) {
            k_mutex_lock(&control_lock, K_FOREVER);
            if (message.sequence == control.sequence && message.flags == state_flags(control))
                pending = false;
            k_mutex_unlock(&control_lock);
        }
#else
        if (message.kind == SOFLE_DISPLAY_SET && relay->source == 0) {
            k_mutex_lock(&control_lock, K_FOREVER);
            bool applied = sofle_display_apply(&control, &message);
            if (applied) pending = false;
            k_mutex_unlock(&control_lock);
            if (applied) {
                sofle_status_refresh();
                message.kind = SOFLE_DISPLAY_ACK;
                /* Peripheral transport API queues its event, does not wait. */
                transmit(message);
            }
        }
#endif
        return ZMK_EV_EVENT_HANDLED;
    }
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (as_zmk_activity_state_changed(event)) {
        bool active = zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE;
        k_mutex_lock(&control_lock, K_FOREVER);
        bool changed = active != control.active;
        if (changed) { control.active = active; control.sequence++; }
        k_mutex_unlock(&control_lock);
        if (changed) { sofle_status_refresh(); begin_handshake(); }
    }
#else
    const struct zmk_split_peripheral_status_changed *status =
        as_zmk_split_peripheral_status_changed(event);
    if (status) {
        k_mutex_lock(&control_lock, K_FOREVER);
        connected = status->connected;
        /* A new central boot may reset its sequence. Never reset manual state
         * just because the link drops; ordinary keys must not turn it on. */
        control.sequence_valid = false;
        pending = status->connected;
        k_mutex_unlock(&control_lock);
        if (status->connected) begin_handshake();
        else k_work_cancel_delayable(&retry_work);
    }
#endif
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(sofle_display_control, control_event);
ZMK_SUBSCRIPTION(sofle_display_control, zmk_relay_event_received);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
ZMK_SUBSCRIPTION(sofle_display_control, zmk_activity_state_changed);
#else
ZMK_SUBSCRIPTION(sofle_display_control, zmk_split_peripheral_status_changed);
#endif

void sofle_display_control_start(void) {
    if (!atomic_cas(&started, 0, 1)) return;
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    k_mutex_lock(&control_lock, K_FOREVER);
    control.active = zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE;
    k_mutex_unlock(&control_lock);
    begin_handshake();
#else
    k_mutex_lock(&control_lock, K_FOREVER);
    connected = zmk_split_bt_peripheral_is_connected();
    bool request = connected && !control.sequence_valid;
    k_mutex_unlock(&control_lock);
    if (request) begin_handshake();
#endif
}
