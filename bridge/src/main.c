/* main.c - adapted from hirosatosou/roba-ble-hid-bridge m1-hogp
 * Copyright (c) 2018 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 * Nordic nRF52840 only. See UPSTREAM.md for provenance and deliberate removals.
 */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/settings/settings.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>
#include <bluetooth/services/hogp.h>
#include "transport.h"
#include "setup.h"
#include "zmk_descriptor.h"

static struct bt_conn *default_conn;
static struct bt_hogp hogp;
static const struct device *hid_dev;
static struct bridge_queue queue;
static struct bridge_item in_flight; /* never alias a ring slot while USB owns it */
static bool ep_in_flight, usb_configured;
static atomic_t map_verified;
static struct k_spinlock hid_lock;
static struct setup_state setup;
static struct k_spinlock setup_lock;
static atomic_t conn_pending, have_bond, auto_pairing;
static bt_addr_le_t auto_target; /* exact-name auto-pair candidate in progress */
static bool auto_name_match; /* valid for the advertisement being processed */
static uint8_t feature_reply[SETUP_WIRE_SIZE];
static bool target_allowed(const bt_addr_le_t *addr) {
    k_spinlock_key_t key = k_spin_lock(&setup_lock);
    bool allowed = setup_target_allowed(&setup, k_uptime_get(), addr->type, addr->a.val);
    k_spin_unlock(&setup_lock, key);
    return allowed;
}
static void setup_end(void) {
    k_spinlock_key_t key = k_spin_lock(&setup_lock);
    setup_close(&setup);
    k_spin_unlock(&setup_lock, key);
    bt_set_bondable(false);
}
static size_t map_offset;
static uint8_t report_mask;

struct bond_lookup { const bt_addr_le_t *addr; bool found; };
static void bond_lookup_cb(const struct bt_bond_info *info, void *user_data) {
    struct bond_lookup *lookup = user_data;
    if (bt_addr_le_eq(&info->addr, lookup->addr)) lookup->found = true;
}
static bool is_bonded(const bt_addr_le_t *addr) {
    struct bond_lookup lookup = { .addr = addr };
    bt_foreach_bond(BT_ID_DEFAULT, bond_lookup_cb, &lookup);
    return lookup.found;
}
static void bond_count_cb(const struct bt_bond_info *info, void *user_data) {
    ARG_UNUSED(info); ++*(unsigned *)user_data;
}
static bool any_bond(void) {
    unsigned count = 0;
    bt_foreach_bond(BT_ID_DEFAULT, bond_count_cb, &count);
    return count != 0;
}
/* Called with hid_lock. A failed write keeps the item for periodic retry. */
static void hid_pump(void) {
    if (!usb_configured || ep_in_flight || queue.head == queue.tail) return;
    in_flight = queue.items[queue.tail];
    if (!hid_int_ep_write(hid_dev, in_flight.buf, in_flight.len, NULL)) {
        ep_in_flight = true;
        queue.tail = (queue.tail + 1) % BRIDGE_QUEUE_LEN;
    }
}
static void release_all(void) {
    k_spinlock_key_t key = k_spin_lock(&hid_lock);
    bridge_release_all(&queue);
    hid_pump();
    k_spin_unlock(&hid_lock, key);
}
static void hid_in_ready(const struct device *dev) {
    ARG_UNUSED(dev);
    k_spinlock_key_t key = k_spin_lock(&hid_lock);
    ep_in_flight = false;
    hid_pump();
    k_spin_unlock(&hid_lock, key);
}
static const struct hid_ops hid_ops = { .int_in_ready = hid_in_ready };
static void scan_restart(void);
static void unbond_work_fn(struct k_work *work) {
    ARG_UNUSED(work);
    (void)bt_unpair(BT_ID_DEFAULT, NULL); /* clear the whole store for this ID */
    atomic_clear(&have_bond);
    scan_restart(); /* bond store empty: auto-pair may run again */
}
static K_WORK_DEFINE(unbond_work, unbond_work_fn);
static int feature_set(const struct device *dev, struct usb_setup_packet *packet,
                       int32_t *len, uint8_t **data) {
    ARG_UNUSED(dev);
    if (!len || !data || *len < 0) return -EINVAL;
    k_spinlock_key_t key = k_spin_lock(&setup_lock);
    bool ok = setup_feature_set(&setup, k_uptime_get(), atomic_get(&conn_pending),
                               atomic_get(&have_bond), packet->wValue, packet->wLength,
                               *data, (size_t)*len);
    /* op byte sits at data[2]; SETUP_OK at reply[3]. The unpair itself is
     * deferred out of the USB callback context. */
    if (ok && (*data)[2] == SETUP_UNBOND && setup.reply[3] == SETUP_OK)
        k_work_submit(&unbond_work);
    k_spin_unlock(&setup_lock, key);
    return ok ? 0 : -EINVAL;
}
static int feature_get(const struct device *dev, struct usb_setup_packet *packet,
                       int32_t *len, uint8_t **data) {
    ARG_UNUSED(dev);
    if (!setup_feature_get_valid(packet->wValue, packet->wLength)) return -EINVAL;
    k_spinlock_key_t key = k_spin_lock(&setup_lock);
    memcpy(feature_reply, setup.reply, sizeof(feature_reply));
    k_spin_unlock(&setup_lock, key);
    *data = feature_reply; *len = sizeof(feature_reply);
    return 0;
}
static const struct hid_ops setup_ops = { .get_report = feature_get, .set_report = feature_set };
static void usb_status(enum usb_dc_status_code status, const uint8_t *param) {
    ARG_UNUSED(param);
    if (status == USB_DC_DISCONNECTED || status == USB_DC_RESET || status == USB_DC_SUSPEND) {
        k_spinlock_key_t sk = k_spin_lock(&setup_lock);
        setup_close(&setup); /* no pairing permission survives USB loss */
        k_spin_unlock(&setup_lock, sk);
    }
    k_spinlock_key_t key = k_spin_lock(&hid_lock);
    if (status == USB_DC_CONFIGURED) {
        usb_configured = true;
        ep_in_flight = false;
        bridge_release_all(&queue);
    } else if (status == USB_DC_DISCONNECTED || status == USB_DC_RESET) {
        usb_configured = false;
        ep_in_flight = false;
        bridge_release_all(&queue);
    }
    k_spin_unlock(&hid_lock, key);
}

static void scan_restart_work_fn(struct k_work *work) {
    ARG_UNUSED(work);
    if (default_conn) return;
    (void)bt_scan_stop();
    (void)bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
}
static K_WORK_DELAYABLE_DEFINE(scan_restart_work, scan_restart_work_fn);
static void scan_restart(void) {
    k_work_reschedule(&scan_restart_work, K_SECONDS(1));
}
#if defined(CONFIG_BRIDGE_PHYSICAL_PAIRING)
/* Retain the negative build gate; no physical pairing path is implemented. */
BUILD_ASSERT(DT_NODE_HAS_STATUS(DT_ALIAS(bridge_pair), okay),
             "Verify physical wiring and define bridge-pair; never guess a GPIO");
BUILD_ASSERT(0, "Physical pairing unavailable: use explicit USB target selection");
#endif

/* Unlike upstream connect_if_match=1, unknown HID advertisements cannot create
 * connections outside an explicit window. Resolved bonded identity is allowed.
 */
static void try_connect(struct bt_scan_device_info *device_info, bool connectable) {
    if (!connectable || default_conn) return;
    const bt_addr_le_t *addr = device_info->recv_info->addr;
    bool bonded = is_bonded(addr);
    if (!bonded) {
        bool allowed = false;
        /* Auto-pair: ONLY when the store has zero bonds and the advertiser's
         * exact complete name equals CONFIG_BRIDGE_PEER_NAME. The keyboard
         * advertises this way only while a profile is in pairing mode, so a
         * bare dongle in a USB port pairs by plugging, with no WebHID page.
         * Any bond (even a foreign one) disables the path entirely. */
        if (IS_ENABLED(CONFIG_BRIDGE_AUTO_PAIR) && !any_bond() && auto_name_match) {
            atomic_set(&auto_pairing, 1);
            auto_target = *addr;
            allowed = true;
        } else {
            k_spinlock_key_t key = k_spin_lock(&setup_lock);
            allowed = setup_claim(&setup, k_uptime_get(), addr->type, addr->a.val);
            k_spin_unlock(&setup_lock, key);
        }
        if (!allowed) return;
        /* Set bondable before create/security, never from the USB callback. */
        bt_set_bondable(true);
    }
    if (bt_scan_stop()) {
        if (!bonded) { atomic_clear(&auto_pairing); setup_end(); }
        return;
    }
    atomic_set(&conn_pending, 1);
    struct bt_conn *conn = NULL;
    int err = bt_conn_le_create(device_info->recv_info->addr, BT_CONN_LE_CREATE_CONN,
                                BT_LE_CONN_PARAM(6, 24, 30, 400), &conn);
    if (!err) default_conn = conn; /* retain create's reference, released on disconnect */
    else { atomic_clear(&conn_pending); if (!bonded) { atomic_clear(&auto_pairing); setup_end(); } scan_restart(); }
}
static bool candidate_name(struct bt_data *data, void *user_data) {
    struct setup_peer *peer = user_data;
    if (data->type == BT_DATA_NAME_COMPLETE || data->type == BT_DATA_NAME_SHORTENED) {
        memcpy(peer->name, data->data, MIN(data->data_len, sizeof(peer->name) - 1));
        peer->name[sizeof(peer->name) - 1] = '\0';
        auto_name_match = IS_ENABLED(CONFIG_BRIDGE_AUTO_PAIR) &&
            strcmp(peer->name, CONFIG_BRIDGE_PEER_NAME) == 0;
        return false;
    }
    return true;
}
static void scan_filter_match(struct bt_scan_device_info *device_info,
                              struct bt_scan_filter_match *filter_match, bool connectable) {
    if (filter_match->uuid.match) {
        if (connectable) {
            struct setup_peer peer = { .type = device_info->recv_info->addr->type,
                                      .rssi = device_info->recv_info->rssi };
            memcpy(peer.addr, device_info->recv_info->addr->a.val, 6);
            auto_name_match = false;
            bt_data_parse(device_info->adv_data, candidate_name, &peer);
            k_spinlock_key_t key = k_spin_lock(&setup_lock);
            setup_observe(&setup, k_uptime_get(), &peer);
            k_spin_unlock(&setup_lock, key);
        }
        try_connect(device_info, connectable);
    }
}
static void scan_filter_no_match(struct bt_scan_device_info *device_info, bool connectable) {
    /* Directed reconnect often omits the HID service UUID. Bond check is mandatory. */
    if (device_info->recv_info->adv_type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND &&
        is_bonded(device_info->recv_info->addr)) try_connect(device_info, connectable);
}
BT_SCAN_CB_INIT(scan_cb, scan_filter_match, scan_filter_no_match, NULL, NULL);
static int scan_init(void) {
    struct bt_scan_init_param init = { .connect_if_match = 0 };
    bt_scan_init(&init);
    bt_scan_cb_register(&scan_cb);
    int err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_HIDS);
    if (err) return err;
    return bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
}

static void discovery_completed_cb(struct bt_gatt_dm *dm, void *context) {
    ARG_UNUSED(context);
    int err = bt_hogp_handles_assign(dm, &hogp);
    (void)bt_gatt_dm_data_release(dm);
    if (err && default_conn)
        (void)bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}
static void discovery_service_not_found_cb(struct bt_conn *conn, void *context) {
    ARG_UNUSED(context);
    (void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}
static void discovery_error_found_cb(struct bt_conn *conn, int err, void *context) {
    ARG_UNUSED(err); discovery_service_not_found_cb(conn, context);
}
static const struct bt_gatt_dm_cb discovery_cb = {
    .completed = discovery_completed_cb,
    .service_not_found = discovery_service_not_found_cb,
    .error_found = discovery_error_found_cb,
};
static void connected(struct bt_conn *conn, uint8_t conn_err) {
    if (conn_err) {
        if (default_conn == conn) { bt_conn_unref(default_conn); default_conn = NULL; }
        atomic_clear(&conn_pending); setup_end(); scan_restart(); return;
    }
    if (!is_bonded(bt_conn_get_dst(conn)) && !target_allowed(bt_conn_get_dst(conn)) &&
        !(atomic_get(&auto_pairing) && bt_addr_le_eq(bt_conn_get_dst(conn), &auto_target))) {
        (void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN); return;
    }
    /* Do not force 2M; Sofle EXPERIMENTAL_CONN explicitly disables it.
     * Do not reject the keyboard's latency or keep reasserting min interval. */
    if (bt_conn_set_security(conn, BT_SECURITY_L2))
        (void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
}
static void disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    if (default_conn != conn) return;
    atomic_clear(&map_verified);
    release_all();
    if (bt_hogp_assign_check(&hogp)) bt_hogp_release(&hogp);
    bt_conn_unref(default_conn);
    default_conn = NULL;
    atomic_clear(&conn_pending); atomic_clear(&auto_pairing); setup_end();
    scan_restart(); /* keeps bonds; never unpair on RF/authentication failure */
}
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err) {
    if (conn != default_conn) return;
    if (err || level < BT_SECURITY_L2 ||
        (!is_bonded(bt_conn_get_dst(conn)) && !target_allowed(bt_conn_get_dst(conn)))) {
        (void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL); return;
    }
    if (bt_gatt_dm_start(conn, BT_UUID_HIDS, &discovery_cb, NULL))
        (void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}
static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param) {
    ARG_UNUSED(conn); ARG_UNUSED(param);
    return true; /* let the controller validate spec bounds; accept Sofle PPCP */
}
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected, .disconnected = disconnected,
    .security_changed = security_changed, .le_param_req = le_param_req,
};
static enum bt_security_err pairing_accept(struct bt_conn *conn,
                                           const struct bt_conn_pairing_feat *feat) {
    ARG_UNUSED(feat);
    /* Existing bonds may encrypt/reconnect, but SMP re-pair/replacement is denied. */
    return conn == default_conn && !is_bonded(bt_conn_get_dst(conn)) &&
           (target_allowed(bt_conn_get_dst(conn)) ||
            (atomic_get(&auto_pairing) &&
             bt_addr_le_eq(bt_conn_get_dst(conn), &auto_target)))
        ? BT_SECURITY_ERR_SUCCESS : BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
}
static const struct bt_conn_auth_cb auth_callbacks = { .pairing_accept = pairing_accept };
static void pairing_complete(struct bt_conn *conn, bool bonded) {
    if (!bonded) { (void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL); return; }
    atomic_set(&have_bond, 1);
    atomic_clear(&auto_pairing);
    setup_end();
}
static struct bt_conn_auth_info_cb auth_info = { .pairing_complete = pairing_complete };

static uint8_t hogp_notify_cb(struct bt_hogp *client, struct bt_hogp_rep_info *rep,
                              uint8_t err, const uint8_t *data) {
    ARG_UNUSED(client);
    if (!data) return BT_GATT_ITER_STOP;
    if (err || !atomic_get(&map_verified)) return BT_GATT_ITER_CONTINUE;
    struct bridge_item item;
    /* Report Reference determines ID and TYPE; NOT enumeration order/handle.
     * Only the body is provided by HOGP; append exactly one USB report ID. */
    if (!bridge_convert(&item, bt_hogp_rep_id(rep), bt_hogp_rep_type(rep),
                        data, bt_hogp_rep_size(rep))) return BT_GATT_ITER_CONTINUE;
    k_spinlock_key_t key = k_spin_lock(&hid_lock);
    (void)bridge_enqueue(&queue, &item);
    hid_pump();
    k_spin_unlock(&hid_lock, key);
    return BT_GATT_ITER_CONTINUE;
}
static void subscribe_work_fn(struct k_work *work) {
    ARG_UNUSED(work);
    if (!default_conn || !bt_hogp_assign_check(&hogp) || !atomic_get(&map_verified)) return;
    struct bt_hogp_rep_info *rep = NULL;
    report_mask = 0;
    while ((rep = bt_hogp_rep_next(&hogp, rep))) {
        uint8_t id = bt_hogp_rep_id(rep);
        if (bt_hogp_rep_type(rep) != BT_HIDS_REPORT_TYPE_INPUT) continue;
        if (!bridge_body_size(id) || (report_mask & BIT(id))) goto fail;
        report_mask |= BIT(id);
        if (bt_hogp_rep_subscribe(&hogp, rep, hogp_notify_cb)) goto fail;
    }
    if (report_mask != (BIT(1) | BIT(2) | BIT(3))) goto fail;
    return;
fail:
    atomic_clear(&map_verified);
    release_all();
    (void)bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}
static K_WORK_DEFINE(subscribe_work, subscribe_work_fn);
static void hogp_map_check_cb(struct bt_hogp *client, uint8_t err,
                              const uint8_t *data, size_t size, size_t offset) {
    if (!default_conn) return;
    if (err || offset != map_offset) goto fail;
    if (!data || !size) {
        if (map_offset != sizeof(bridge_report_desc)) goto fail;
        atomic_set(&map_verified, 1);
        k_work_submit(&subscribe_work);
        return;
    }
    if (offset > sizeof(bridge_report_desc) || size > sizeof(bridge_report_desc) - offset ||
        memcmp(data, bridge_report_desc + offset, size)) goto fail;
    map_offset += size;
    if (bt_hogp_map_read(client, hogp_map_check_cb, map_offset, K_NO_WAIT)) goto fail;
    return;
fail:
    atomic_clear(&map_verified);
    (void)bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}
static void hids_ready_work_fn(struct k_work *work) {
    ARG_UNUSED(work);
    if (!default_conn || !bt_hogp_assign_check(&hogp)) return;
    atomic_clear(&map_verified);
    map_offset = 0;
    if (bt_hogp_map_read(&hogp, hogp_map_check_cb, 0, K_NO_WAIT))
        (void)bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}
static K_WORK_DEFINE(hids_ready_work, hids_ready_work_fn);
static void hogp_ready_cb(struct bt_hogp *client) {
    ARG_UNUSED(client); k_work_submit(&hids_ready_work);
}
static void hogp_prep_fail_cb(struct bt_hogp *client, int err) {
    ARG_UNUSED(client); ARG_UNUSED(err);
    if (default_conn) (void)bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}
static const struct bt_hogp_init_params hogp_init_params = {
    .ready_cb = hogp_ready_cb, .prep_error_cb = hogp_prep_fail_cb,
};
static void expire_connection(struct bt_conn *conn, void *user_data) {
    ARG_UNUSED(user_data);
    /* bt_conn_foreach owns a temporary reference; don't dereference a racing
     * default_conn from the main thread while the RX callback can unref it. */
    if (!is_bonded(bt_conn_get_dst(conn)) && !target_allowed(bt_conn_get_dst(conn)))
        (void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}
static void count_bond(const struct bt_bond_info *info, void *user_data) {
    ARG_UNUSED(info); ARG_UNUSED(user_data); atomic_set(&have_bond, 1);
}
int main(void) {
    bt_hogp_init(&hogp, &hogp_init_params);
    int err = bt_conn_auth_cb_register(&auth_callbacks);
    if (err) return err;
    err = bt_conn_auth_info_cb_register(&auth_info);
    if (err) return err;
    err = bt_enable(NULL); /* retain upstream's BT-before-USB ordering */
    if (err) return err;
    err = settings_load();
    if (err) return err;
    bt_set_bondable(false); /* NO auto-pairing on empty NVS, errors or reboots */
    bt_foreach_bond(BT_ID_DEFAULT, count_bond, NULL);
    hid_dev = device_get_binding("HID_0");
    if (!hid_dev) return -ENODEV;
    usb_hid_register_device(hid_dev, bridge_report_desc, sizeof(bridge_report_desc), &hid_ops);
    err = usb_hid_init(hid_dev);
    if (err) return err;
    const struct device *setup_dev = device_get_binding("HID_1");
    if (!setup_dev) return -ENODEV;
    usb_hid_register_device(setup_dev, setup_report_desc, sizeof(setup_report_desc), &setup_ops);
    err = usb_hid_init(setup_dev);
    if (err) return err;
    err = usb_enable(usb_status);
    if (err) return err;
    err = scan_init();
    if (err) return err;

    scan_restart();
    while (1) {
        k_spinlock_key_t sk = k_spin_lock(&setup_lock);
        setup_expire(&setup, k_uptime_get());
        bool armed = setup.pair_until != 0 || atomic_get(&auto_pairing);
        k_spin_unlock(&setup_lock, sk);
        if (!armed) {
            bt_set_bondable(false);
            bt_conn_foreach(BT_CONN_TYPE_LE, expire_connection, NULL);
        }
        k_spinlock_key_t key = k_spin_lock(&hid_lock);
        hid_pump(); /* retries USB busy/not-ready; no new input required */
        k_spin_unlock(&hid_lock, key);
        k_msleep(1);
    }
    return 0;
}
