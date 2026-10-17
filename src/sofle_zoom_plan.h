/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SOFLE_ZOOM_IN 0
#define SOFLE_ZOOM_OUT 1
#define SOFLE_ZOOM_REPORT_SIZE 8
/* Pure functions: never alter the live ZMK HID/modifier state. */
const char *sofle_profile_name(unsigned profile);
int sofle_zoom_overlay(uint8_t output[8], const uint8_t physical[8],
                        unsigned profile, unsigned direction);
void sofle_zoom_release_body(uint8_t output[8], const uint8_t physical[8], bool same_endpoint);
