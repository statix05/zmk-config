/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SOFLE_DISPLAY_WIRE_SIZE 8
#define SOFLE_DISPLAY_SET 1
#define SOFLE_DISPLAY_REQUEST 2
#define SOFLE_DISPLAY_ACK 3
#define SOFLE_DISPLAY_MANUAL 1
#define SOFLE_DISPLAY_ACTIVE 2

struct sofle_display_message {
    uint8_t kind;
    uint8_t flags;
    uint32_t sequence;
};
struct sofle_display_control {
    bool manual_enabled;
    bool active;
    bool sequence_valid;
    uint32_t sequence;
};
int sofle_display_encode(uint8_t *out, size_t capacity,
                         const struct sofle_display_message *message);
int sofle_display_decode(struct sofle_display_message *message,
                         const uint8_t *data, size_t length);
/* Accept newer/equal SETs; duplicate SET never toggles. Clear sequence_valid
 * on a new split connection so central reboot/reset sequence is accepted. */
bool sofle_display_apply(struct sofle_display_control *state,
                          const struct sofle_display_message *message);
bool sofle_display_should_light(const struct sofle_display_control *state);
