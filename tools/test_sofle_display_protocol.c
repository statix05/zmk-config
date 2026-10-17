/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sofle_display_protocol.h"

static void deliver(struct sofle_display_control *right, uint8_t flags, uint32_t seq) {
    struct sofle_display_message input = {SOFLE_DISPLAY_SET, flags, seq}, output;
    uint8_t data[8];
    assert(sofle_display_encode(data, sizeof(data), &input) == 8);
    assert(sofle_display_decode(&output, data, sizeof(data)) == 0);
    assert(output.kind == input.kind && output.flags == input.flags && output.sequence == seq);
    assert(sofle_display_apply(right, &output));
}
int main(void) {
    unsigned roundtrips = 0;
    for (unsigned type = 1; type <= 3; type++) {
        for (unsigned flags = 0; flags < 4; flags++) {
            uint32_t values[] = {0, 1, 255, 256, 65535, UINT32_MAX};
            for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); i++) {
                struct sofle_display_message src = {type, flags, values[i]}, dst;
                uint8_t data[10]; memset(data, 0xa5, sizeof(data));
                assert(sofle_display_encode(data + 1, 8, &src) == 8);
                assert(data[0] == 0xa5 && data[9] == 0xa5);
                assert(sofle_display_decode(&dst, data + 1, 8) == 0);
                assert(dst.kind == type && dst.flags == flags && dst.sequence == values[i]);
                roundtrips++;
            }
        }
    }
    struct sofle_display_control right = {true, true, false, 0};
    deliver(&right, 3, 1); assert(sofle_display_should_light(&right));
    deliver(&right, 2, 2); assert(!sofle_display_should_light(&right));
    /* Duplicate OFF delivered repeatedly never becomes ON. */
    for (unsigned i = 0; i < 100; i++) deliver(&right, 2, 2);
    assert(!right.manual_enabled && right.active);
    struct sofle_display_message stale = {SOFLE_DISPLAY_SET, 3, 1};
    assert(!sofle_display_apply(&right, &stale));
    struct sofle_display_message conflict = {SOFLE_DISPLAY_SET, 3, 2};
    assert(!sofle_display_apply(&right, &conflict));
    /* Ordinary typing changes activity, never manual intent. */
    deliver(&right, 0, 3); assert(!sofle_display_should_light(&right));
    deliver(&right, 2, 4); assert(!sofle_display_should_light(&right));
    /* Right disconnected during OFF: reconnect request gets latest explicit OFF. */
    right.sequence_valid = false; deliver(&right, 2, 20);
    assert(!sofle_display_should_light(&right));
    /* Both ON, idle, wake retain the one central manual state. */
    deliver(&right, 3, 21); assert(sofle_display_should_light(&right));
    deliver(&right, 1, 22); assert(!sofle_display_should_light(&right));
    deliver(&right, 3, 23); assert(sofle_display_should_light(&right));
    /* New central boot resets sequence, accepted after connection reset only. */
    right.sequence_valid = false; deliver(&right, 3, 0);
    /* Sequence wrap is accepted, older packets after wrap are rejected. */
    right.sequence_valid = false; deliver(&right, 3, UINT32_MAX);
    deliver(&right, 2, 0); assert(!sofle_display_should_light(&right));
    stale.sequence = UINT32_MAX; assert(!sofle_display_apply(&right, &stale));
    uint8_t invalid[8] = {1, SOFLE_DISPLAY_SET, 3, 0, 0, 0, 0, 0};
    struct sofle_display_message dst;
    for (unsigned len = 0; len < 8; len++) assert(sofle_display_decode(&dst, invalid, len));
    invalid[0] = 2; assert(sofle_display_decode(&dst, invalid, 8)); invalid[0] = 1;
    invalid[1] = 9; assert(sofle_display_decode(&dst, invalid, 8)); invalid[1] = SOFLE_DISPLAY_SET;
    invalid[2] = 4; assert(sofle_display_decode(&dst, invalid, 8)); invalid[2] = 3;
    invalid[3] = 1; assert(sofle_display_decode(&dst, invalid, 8));
    printf("PASS: %u wire roundtrips, duplicate/stale/conflict, OFF+typing, reconnect, central reboot, wrap and malformed packets\n", roundtrips);
    return 0;
}
