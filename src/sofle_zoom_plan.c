/* SPDX-License-Identifier: MIT */
#include "sofle_zoom_plan.h"
#include <errno.h>
#include <string.h>
#ifndef CONFIG_SOFLE_MAC1_BT_PROFILE
#define CONFIG_SOFLE_MAC1_BT_PROFILE 0
#endif
#ifndef CONFIG_SOFLE_MAC2_BT_PROFILE
#define CONFIG_SOFLE_MAC2_BT_PROFILE 1
#endif
#ifndef CONFIG_SOFLE_DONGLE_BT_PROFILE
#define CONFIG_SOFLE_DONGLE_BT_PROFILE 4
#endif

const char *sofle_profile_name(unsigned profile) {
    if (profile == CONFIG_SOFLE_MAC1_BT_PROFILE) return "MAC1";
    if (profile == CONFIG_SOFLE_MAC2_BT_PROFILE) return "MAC2";
    if (profile == CONFIG_SOFLE_DONGLE_BT_PROFILE) return "DONGLE";
    return NULL;
}
int sofle_zoom_overlay(uint8_t output[8], const uint8_t physical[8],
                        unsigned profile, unsigned direction) {
    if (!output || !physical || direction > SOFLE_ZOOM_OUT) return -EINVAL;
    unsigned modifier;
    if (profile == CONFIG_SOFLE_MAC1_BT_PROFILE || profile == CONFIG_SOFLE_MAC2_BT_PROFILE)
        modifier = 0x08; /* HID Left GUI; browser Cmd+Equal does not need Shift. */
    else if (profile == CONFIG_SOFLE_DONGLE_BT_PROFILE)
        modifier = 0x01; /* HID Left Control, both Windows and Linux. */
    else return -ENOTSUP; /* Reserved profiles have no guessed OS assignment. */
    uint8_t usage = direction == SOFLE_ZOOM_IN ? 0x2e : 0x2d;
    int free_slot = -1;
    for (unsigned i = 2; i < SOFLE_ZOOM_REPORT_SIZE; i++) {
        if (physical[i] == usage) return -EALREADY; /* Never steal a held key. */
        if (!physical[i] && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return -ENOSPC; /* No rollover or forced key release. */
    memcpy(output, physical, SOFLE_ZOOM_REPORT_SIZE);
    output[0] |= modifier;
    output[free_slot] = usage;
    return 0;
}
void sofle_zoom_release_body(uint8_t output[8], const uint8_t physical[8], bool same_endpoint) {
    if (same_endpoint) memcpy(output, physical, SOFLE_ZOOM_REPORT_SIZE);
    else memset(output, 0, SOFLE_ZOOM_REPORT_SIZE);
}
