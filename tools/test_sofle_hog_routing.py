#!/usr/bin/env python3
"""Compile the ACTUAL HOG queue code with mock transport; not a hardware test."""
import argparse
from pathlib import Path
import subprocess
import tempfile

STUB = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#define IS_ENABLED(x) (x)
#define CONFIG_ZMK_POINTING 1
#define ZMK_BLE_PROFILE_COUNT 5
#define CONFIG_ZMK_BLE_KEYBOARD_REPORT_QUEUE_SIZE 8
#define CONFIG_ZMK_BLE_CONSUMER_REPORT_QUEUE_SIZE 8
#define CONFIG_ZMK_BLE_MOUSE_REPORT_QUEUE_SIZE 8
#define K_MSEC(x) (x)
#define K_NO_WAIT 0
#define BT_ID_DEFAULT 0
#define BT_SECURITY_L2 2
#define LOG_WRN(...) ((void)0)
#define LOG_DBG(...) ((void)0)
struct zmk_hid_keyboard_report_body { uint8_t data[8]; };
struct zmk_hid_consumer_report_body { uint8_t data[6]; };
struct zmk_hid_mouse_report_body { uint8_t data[9]; };
struct k_work { void (*fn)(struct k_work *); };
struct k_work_q { int unused; } hog_work_q;
#define K_WORK_DEFINE(name, cb) struct k_work name = {.fn=cb}
struct queue { size_t size, capacity, count; uint8_t data[4096]; };
#define K_MSGQ_DEFINE(name, item_size, cap, align) struct queue name={.size=item_size,.capacity=cap}
static int k_msgq_put(struct queue *q, const void *item, int wait) {
    if (q->count == q->capacity) return -EAGAIN;
    memcpy(q->data+q->count*q->size,item,q->size); q->count++; return 0;
}
static int k_msgq_get(struct queue *q, void *item, int wait) {
    if (!q->count) return -EAGAIN;
    memcpy(item,q->data,q->size); q->count--;
    memmove(q->data,q->data+q->size,q->count*q->size); return 0;
}
static int k_work_submit_to_queue(struct k_work_q *q, struct k_work *work) {return 1;}
typedef struct {uint8_t profile;} bt_addr_le_t;
struct bt_conn {uint8_t profile;};
static struct bt_conn peers[5]={{0},{1},{2},{3},{4}};
static bt_addr_le_t addresses[5]={{0},{1},{2},{3},{4}};
static int connected[5]={1,1,1,1,1};
static unsigned current_profile;
static uint8_t zmk_ble_active_profile_index(void) {return current_profile;}
static const bt_addr_le_t *zmk_ble_profile_address(uint8_t p) {return &addresses[p];}
static struct bt_conn *bt_conn_lookup_addr_le(int id,const bt_addr_le_t *a) {
    return connected[a->profile] ? &peers[a->profile] : NULL;
}
static void bt_conn_unref(struct bt_conn *conn) {}
static int bt_conn_set_security(struct bt_conn *conn, int level) {return 0;}
static struct {int attrs[32];} hog_svc;
struct bt_gatt_notify_params {const void *attr; const void *data; size_t len;};
static struct {unsigned profile;ptrdiff_t attr;size_t len;uint8_t data[16];} sent[128];
static unsigned sent_count;
static int bt_gatt_notify_cb(struct bt_conn *conn,const struct bt_gatt_notify_params *p) {
    assert(sent_count < 128 && p->len <= 16);
    sent[sent_count].profile=conn->profile;
    sent[sent_count].attr=(const int *)p->attr-hog_svc.attrs;
    sent[sent_count].len=p->len;
    memcpy(sent[sent_count].data,p->data,p->len);sent_count++;return 0;
}
int zmk_hog_send_keyboard_report_to_profile(const struct zmk_hid_keyboard_report_body *,uint8_t);
int zmk_hog_send_consumer_report_to_profile(const struct zmk_hid_consumer_report_body *,uint8_t);
int zmk_hog_send_mouse_report_to_profile(const struct zmk_hid_mouse_report_body *,uint8_t);
'''
TEST = r'''
int main(void) {
    struct zmk_hid_keyboard_report_body keyboard={{8,0,0x2e}}, zero_keyboard={{0}};
    struct zmk_hid_consumer_report_body consumer={{0xe9}}, zero_consumer={{0}};
    struct zmk_hid_mouse_report_body mouse={{1}}, zero_mouse={{0}};
    assert(!zmk_hog_send_keyboard_report_to_profile(&keyboard,0));
    assert(!zmk_hog_send_keyboard_report_to_profile(&zero_keyboard,0));
    assert(!zmk_hog_send_keyboard_report_to_profile(&keyboard,4));
    current_profile=1; send_keyboard_report_callback(NULL);
    assert(sent_count==3 && sent[0].profile==0 && sent[1].profile==0 && sent[2].profile==4);
    assert(sent[0].attr==5 && sent[0].len==8 && sent[0].data[0]==8);
    for(unsigned i=0;i<8;i++) assert(!sent[1].data[i]);
    assert(!zmk_hog_send_consumer_report_to_profile(&consumer,0));
    assert(!zmk_hog_send_consumer_report_to_profile(&zero_consumer,0));
    current_profile=4; send_consumer_report_callback(NULL);
    assert(sent[3].profile==0 && sent[3].len==6 && sent[3].attr==9);
    assert(sent[4].profile==0 && sent[4].data[0]==0);
    assert(!zmk_hog_send_mouse_report_to_profile(&mouse,1));
    assert(!zmk_hog_send_mouse_report_to_profile(&zero_mouse,1));
    current_profile=0; send_mouse_report_callback(NULL);
    assert(sent[5].profile==1 && sent[5].len==9 && sent[5].attr==13);
    assert(sent[6].profile==1 && sent[6].data[0]==0);
    /* Public compatibility wrapper freezes the profile when queued. */
    current_profile=0; assert(!zmk_hog_send_keyboard_report(&keyboard));
    keyboard.data[0]=1;current_profile=4;send_keyboard_report_callback(NULL);
    assert(sent[7].profile==0 && sent[7].data[0]==8);
    /* Disconnected old recipient must not stall a valid next recipient. */
    assert(!zmk_hog_send_keyboard_report_to_profile(&keyboard,0));
    assert(!zmk_hog_send_keyboard_report_to_profile(&keyboard,4));
    connected[0]=0;send_keyboard_report_callback(NULL);
    assert(sent_count==9 && sent[8].profile==4);
    connected[0]=1;
    assert(zmk_hog_send_keyboard_report_to_profile(&keyboard,5)==-EINVAL);
    assert(zmk_hog_send_consumer_report_to_profile(NULL,0)==-EINVAL);
    /* Existing bounded-queue overflow policy retained, never reselecting a peer. */
    for(unsigned i=0;i<12;i++) {
        keyboard.data[2]=i;assert(!zmk_hog_send_keyboard_report_to_profile(&keyboard,i%5));
    }
    current_profile=3;send_keyboard_report_callback(NULL);
    assert(sent_count==17);
    for(unsigned i=0;i<8;i++) {
        assert(sent[9+i].profile==(i+4)%5);
        assert(sent[9+i].data[2]==i+4);
    }
    printf("PASS: actual HOG source, %u transport deliveries; keyboard/consumer/mouse old-target clears, queued-profile switch, payload sizes, disconnect and overflow routing\n",sent_count);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--core", type=Path, required=True)
    args = parser.parse_args()
    source = (args.core / "app/src/hog.c").read_text()
    begin = source.index("/* Queue the intended profile")
    end = source.index("static int zmk_hog_init", begin)
    endpoints = (args.core / "app/src/endpoints.c").read_text()
    for kind in ["keyboard", "consumer", "mouse"]:
        assert f"zmk_hog_send_{kind}_report_to_profile(&{kind}_report->body, current_instance.ble.profile_index)" in endpoints
    with tempfile.TemporaryDirectory(prefix="sofle-hog-", dir="/Users/statix/.hermes/cache/scratch") as directory:
        c = Path(directory) / "test.c"
        exe = Path(directory) / "test"
        c.write_text(STUB + source[begin:end] + TEST)
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                        "-fsanitize=address,undefined", str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
