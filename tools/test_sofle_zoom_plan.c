/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "sofle_zoom_plan.h"
int main(void) {
    unsigned cases = 0;
    for (unsigned profile = 0; profile < 5; profile++) {
        for (unsigned direction = 0; direction < 2; direction++) {
            for (unsigned mods = 0; mods < 256; mods++) {
                for (unsigned held = 0; held <= 6; held++) {
                    uint8_t physical[8] = {mods}, original[8], output[8];
                    for (unsigned i = 0; i < held; i++) physical[2+i] = 4+i;
                    memcpy(original, physical, 8);
                    int rc = sofle_zoom_overlay(output, physical, profile, direction);
                    if (profile == 2 || profile == 3) assert(rc == -ENOTSUP);
                    else if (held == 6) assert(rc == -ENOSPC);
                    else {
                        assert(rc == 0);
                        assert(output[0] == (mods | (profile == 4 ? 1 : 8)));
                        assert(output[2+held] == (direction ? 0x2d : 0x2e));
                        for (unsigned i = 0; i < held; i++) assert(output[2+i] == physical[2+i]);
                        sofle_zoom_release_body(output, physical, true);
                        assert(!memcmp(output, physical, 8));
                        sofle_zoom_release_body(output, physical, false);
                        for (unsigned i = 0; i < 8; i++) assert(output[i] == 0);
                    }
                    assert(!memcmp(original, physical, 8));
                    cases++;
                }
            }
        }
    }
    uint8_t physical[8] = {0,0,0x2e}, output[8];
    assert(sofle_zoom_overlay(output, physical, 0, SOFLE_ZOOM_IN) == -EALREADY);
    physical[2] = 0x2d;
    assert(sofle_zoom_overlay(output, physical, 4, SOFLE_ZOOM_OUT) == -EALREADY);
    assert(!strcmp(sofle_profile_name(0), "MAC1"));
    assert(!strcmp(sofle_profile_name(1), "MAC2"));
    assert(!strcmp(sofle_profile_name(4), "DONGLE"));
    assert(!sofle_profile_name(2) && !sofle_profile_name(3));
    printf("PASS: %u zoom plans; physical state unchanged; old-target clear vs same-target restore; reserved profiles and held Equal/Minus\n", cases);
    return 0;
}
