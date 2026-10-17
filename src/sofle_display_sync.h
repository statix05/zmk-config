/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>

void sofle_display_control_start(void);
bool sofle_display_manual_enabled(void);
bool sofle_display_activity_active(void);
void sofle_display_toggle_global(void);
/* Implemented by the status screen: enqueue a fresh display snapshot. */
void sofle_status_refresh(void);
