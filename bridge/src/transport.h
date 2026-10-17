/* Derived from roBa bridge serializer; Copyright (c) 2018 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BRIDGE_QUEUE_LEN 32
struct bridge_item { uint8_t len; uint8_t buf[10]; };
struct bridge_queue {
    struct bridge_item items[BRIDGE_QUEUE_LEN];
    uint8_t head, tail;
    uint32_t overflows;
};
size_t bridge_body_size(uint8_t report_id);
bool bridge_convert(struct bridge_item *out, uint8_t report_id, uint8_t report_type,
                    const uint8_t *body, size_t size);
void bridge_release_all(struct bridge_queue *q);
bool bridge_enqueue(struct bridge_queue *q, const struct bridge_item *item);
bool bridge_pop(struct bridge_queue *q, struct bridge_item *item);
