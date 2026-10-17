/* SPDX-License-Identifier: MIT
 * Small explicit wire format, no memcpy of padded/endian-dependent structs.
 */
#include "sofle_display_protocol.h"
#include <errno.h>

int sofle_display_encode(uint8_t *out, size_t capacity,
                         const struct sofle_display_message *message) {
    if (!out || !message || capacity < SOFLE_DISPLAY_WIRE_SIZE) return -EINVAL;
    if (message->kind < SOFLE_DISPLAY_SET || message->kind > SOFLE_DISPLAY_ACK ||
        (message->flags & ~(SOFLE_DISPLAY_MANUAL | SOFLE_DISPLAY_ACTIVE))) return -EINVAL;
    out[0] = 1; /* version */
    out[1] = message->kind;
    out[2] = message->flags;
    out[3] = 0; /* reserved */
    for (unsigned i = 0; i < 4; i++) out[4 + i] = (uint8_t)(message->sequence >> (8 * i));
    return SOFLE_DISPLAY_WIRE_SIZE;
}

int sofle_display_decode(struct sofle_display_message *message,
                         const uint8_t *data, size_t length) {
    if (!message || !data || length != SOFLE_DISPLAY_WIRE_SIZE || data[0] != 1 || data[3])
        return -EINVAL;
    if (data[1] < SOFLE_DISPLAY_SET || data[1] > SOFLE_DISPLAY_ACK ||
        (data[2] & ~(SOFLE_DISPLAY_MANUAL | SOFLE_DISPLAY_ACTIVE))) return -EINVAL;
    message->kind = data[1];
    message->flags = data[2];
    message->sequence = 0;
    for (unsigned i = 0; i < 4; i++) message->sequence |= (uint32_t)data[4 + i] << (8 * i);
    return 0;
}

bool sofle_display_apply(struct sofle_display_control *state,
                          const struct sofle_display_message *message) {
    if (!state || !message || message->kind != SOFLE_DISPLAY_SET) return false;
    /* Sequence comparison is wrap-safe for a difference smaller than 2^31.
     * The connection handshake resets validity across central power cycles. */
    if (state->sequence_valid && message->sequence == state->sequence) {
        return state->manual_enabled == !!(message->flags & SOFLE_DISPLAY_MANUAL) &&
               state->active == !!(message->flags & SOFLE_DISPLAY_ACTIVE);
    }
    if (state->sequence_valid &&
        (uint32_t)(message->sequence - state->sequence) >= UINT32_C(0x80000000)) return false;
    state->manual_enabled = !!(message->flags & SOFLE_DISPLAY_MANUAL);
    state->active = !!(message->flags & SOFLE_DISPLAY_ACTIVE);
    state->sequence = message->sequence;
    state->sequence_valid = true;
    return true;
}

bool sofle_display_should_light(const struct sofle_display_control *state) {
    return state && state->manual_enabled && state->active;
}
