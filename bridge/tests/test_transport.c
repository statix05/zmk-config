/* SPDX-License-Identifier: LicenseRef-Nordic-5-Clause */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "transport.h"
#include "zmk_descriptor.h"
static void check_release(struct bridge_queue *q) {
    struct bridge_item item;
    for (unsigned id = 1; id <= 3; ++id) {
        assert(bridge_pop(q, &item));
        assert(item.buf[0] == id && item.len == bridge_body_size(id) + 1);
        for (unsigned j = 1; j < item.len; ++j) assert(item.buf[j] == 0);
    }
    assert(!bridge_pop(q, &item));
}
int main(void) {
    struct bridge_item item, got;
    uint8_t keys[] = { 0x02, 0, 4, 5, 6, 0x68, 0x73, 0 };
    assert(bridge_convert(&item, 1, 1, keys, sizeof(keys)));
    assert(item.len == 9 && item.buf[0] == 1 && !memcmp(item.buf + 1, keys, sizeof(keys)));
    uint8_t consumer[] = { 0xe9, 0xea, 0xcd, 0, 0, 0 };
    assert(bridge_convert(&item, 2, 1, consumer, sizeof(consumer)));
    assert(item.len == 7 && item.buf[0] == 2 && item.buf[1] == 0xe9 && item.buf[2] == 0xea);
    uint8_t mouse[] = { 0x05, 0x00, 0x80, 0xff, 0x7f, 0xff, 0xff, 0x34, 0x12 };
    assert(bridge_convert(&item, 3, 1, mouse, sizeof(mouse)));
    assert(item.len == 10 && !memcmp(item.buf + 1, mouse, 9));
    /* Negative X=-32768,Y=32767,wheel=-1,pan=4660: no truncation/sign reinterpretation. */
    for (unsigned id = 0; id < 256; ++id)
        for (unsigned type = 0; type < 5; ++type)
            for (unsigned size = 0; size <= 12; ++size) {
                uint8_t body[12] = {0};
                bool expected = type == 1 && bridge_body_size(id) && size == bridge_body_size(id);
                assert(bridge_convert(&got, id, type, body, size) == expected);
            }
    assert(!bridge_convert(NULL, 1, 1, keys, 8));
    assert(!bridge_convert(&got, 1, 1, NULL, 8));
    assert(!bridge_convert(&got, 2, 1, keys, 8)); /* no roBa FULL/LE16 consumer assumption */
    struct bridge_queue q = {0};
    /* Deliberately shuffled BLE report references, unrelated to handle ordering. */
    for (unsigned i = 0; i < 10000; ++i) {
        uint8_t id = (uint8_t)(3 - i % 3), body[9] = {0};
        assert(bridge_convert(&item, id, 1, body, bridge_body_size(id)));
        assert(bridge_enqueue(&q, &item));
        assert(bridge_pop(&q, &got) && got.buf[0] == id);
        assert(!bridge_pop(&q, &got));
    }
    assert(bridge_convert(&item, 1, 1, keys, 8));
    assert(bridge_enqueue(&q, &item));
    bridge_release_all(&q); /* disconnect discards pending key-down */
    check_release(&q);
    for (unsigned i = 0; i < BRIDGE_QUEUE_LEN - 1; ++i) assert(bridge_enqueue(&q, &item));
    assert(!bridge_enqueue(&q, &item));
    assert(q.overflows == 1);
    check_release(&q); /* queue exhaustion also fails neutral */
    assert(bridge_enqueue(&q, &item));
    struct bridge_item inflight;
    assert(bridge_pop(&q, &inflight));
    bridge_release_all(&q);
    assert(inflight.buf[3] == 4); /* storage remains owned separately until USB completes */
    check_release(&q);
    /* Independent HID item walker verifies input bit lengths, not a magic array size. */
    unsigned bits[4] = {0}, id = 0, count = 0, size = 0;
    for (size_t i = 0; i < sizeof(bridge_report_desc);) {
        uint8_t prefix = bridge_report_desc[i++];
        unsigned n = prefix & 3; if (n == 3) n = 4;
        assert(i + n <= sizeof(bridge_report_desc));
        unsigned value = 0; for (unsigned j = 0; j < n; ++j) value |= bridge_report_desc[i+j] << (8*j);
        i += n;
        switch (prefix & 0xfc) {
        case 0x84: id = value; assert(id >= 1 && id <= 3); break;
        case 0x74: size = value; break;
        case 0x94: count = value; break;
        case 0x80: bits[id] += count * size; break;
        }
    }
    assert(bits[1] == 64 && bits[2] == 48 && bits[3] == 72);
    puts("PASS: report refs/IDs, HKRO, BASIC consumer, signed mouse, malformed sizes/types, 10000 ring wraps, disconnect release-all, overflow neutral, in-flight lifetime, descriptor bit lengths");
}
