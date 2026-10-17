/* Derived from roBa bridge serializer; Copyright (c) 2018 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause */
#include "transport.h"
#include <string.h>
size_t bridge_body_size(uint8_t id) {
    switch (id) { case 1: return 8; case 2: return 6; case 3: return 9; default: return 0; }
}
bool bridge_convert(struct bridge_item *out, uint8_t id, uint8_t type,
                    const uint8_t *body, size_t size) {
    size_t expected = bridge_body_size(id);
    if (!out || !body || type != 1 || !expected || size != expected) return false;
    memset(out, 0, sizeof(*out));
    out->len = (uint8_t)(size + 1);
    out->buf[0] = id; /* BLE characteristic body has NO ID; USB has exactly one ID. */
    memcpy(out->buf + 1, body, size);
    return true;
}
bool bridge_pop(struct bridge_queue *q, struct bridge_item *item) {
    if (q->head == q->tail) return false;
    *item = q->items[q->tail];
    q->tail = (q->tail + 1) % BRIDGE_QUEUE_LEN;
    return true;
}
void bridge_release_all(struct bridge_queue *q) {
    /* Purge stale presses and deltas; keep the separately owned in-flight USB buffer. */
    q->head = q->tail = 0;
    for (uint8_t id = 1; id <= 3; ++id) {
        struct bridge_item item = { .len = (uint8_t)(bridge_body_size(id) + 1), .buf = { id } };
        q->items[q->head++] = item;
    }
}
bool bridge_enqueue(struct bridge_queue *q, const struct bridge_item *item) {
    if (!item || item->len < 2 || item->len > sizeof(item->buf)) return false;
    uint8_t next = (q->head + 1) % BRIDGE_QUEUE_LEN;
    if (next == q->tail) {
        q->overflows++;
        bridge_release_all(q); /* fail neutral, never silently lose the last key-up */
        return false;
    }
    q->items[q->head] = *item;
    q->head = next;
    return true;
}
