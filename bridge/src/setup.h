/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SETUP_REPORT_ID 0x40
#define SETUP_BODY_SIZE 32
#define SETUP_WIRE_SIZE 33
#define SETUP_VERSION 1
#define SETUP_MAX_CANDIDATES 16
#define SETUP_SCAN_MS 30000
#define SETUP_PAIR_MS 20000
/* Fixed wire schema documented in ../PAIRING.md. Addresses are BLE little endian. */
enum setup_op { SETUP_SCAN=1, SETUP_READ=2, SETUP_SELECT=3, SETUP_ARM=4, SETUP_STATUS=5,
                SETUP_UNBOND=6 };
enum setup_status { SETUP_OK, SETUP_INVALID, SETUP_BUSY, SETUP_EXPIRED, SETUP_NOT_SEEN, SETUP_NOT_SELECTED, SETUP_BONDED };
struct setup_peer { uint8_t type, addr[6]; int8_t rssi; char name[18]; };
struct setup_state {
    struct setup_peer peers[SETUP_MAX_CANDIDATES], target;
    uint8_t count;
    bool selected, claimed;
    uint64_t scan_until, pair_until;
    uint8_t reply[SETUP_WIRE_SIZE];
};
extern const uint8_t setup_report_desc[23];
void setup_expire(struct setup_state *s, uint64_t now);
void setup_close(struct setup_state *s);
void setup_observe(struct setup_state *s, uint64_t now, const struct setup_peer *p);
bool setup_target_allowed(const struct setup_state *s, uint64_t now, uint8_t type, const uint8_t addr[6]);
bool setup_claim(struct setup_state *s, uint64_t now, uint8_t type, const uint8_t addr[6]);
/* Returns false for malformed USB envelope (STALL); protocol errors are replies.
 * No Bluetooth, storage, reset, input payload or Studio operations exist here. */
bool setup_feature_set(struct setup_state *s, uint64_t now, bool connected, bool bonded,
                       uint16_t value, uint16_t requested, const uint8_t *data, size_t len);
bool setup_feature_get_valid(uint16_t value, uint16_t requested);
