#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify actual configuration, HEX ranges, vectors and linked descriptor."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
p = argparse.ArgumentParser()
p.add_argument('build', type=Path)
a = p.parse_args()
b = a.build.resolve()
conf = (b / 'zephyr/.config').read_text()
for flag in ['BT', 'BT_CENTRAL', 'BT_CTLR', 'BT_LL_SOFTDEVICE', 'BT_SETTINGS', 'BT_PRIVACY', 'BT_SMP_APP_PAIRING_ACCEPT', 'USB_DEVICE_HID', 'BT_CTLR_TX_PWR_PLUS_8']:
    assert re.search(r'^CONFIG_' + flag + '=y$', conf, re.M), flag
for flag in ['LOG', 'PRINTK', 'CONSOLE', 'SERIAL', 'REBOOT', 'BT_KEYS_OVERWRITE_OLDEST', 'BT_BONDABLE', 'BRIDGE_PHYSICAL_PAIRING', 'BT_AUTO_PHY_UPDATE', 'BT_USER_PHY_UPDATE']:
    assert not re.search(r'^CONFIG_' + flag + '=y$', conf, re.M), flag
assert 'CONFIG_BT_MAX_CONN=1\n' in conf and 'CONFIG_BT_MAX_PAIRED=1\n' in conf
assert 'CONFIG_USB_HID_DEVICE_COUNT=2\n' in conf
setup_desc = bytes.fromhex('0631ff0974a1018540150026ff00750895200975b102c0')
upper, addresses = 0, []
for line in (b / 'zephyr/zephyr.hex').read_text().splitlines():
    raw = bytes.fromhex(line[1:]); assert sum(raw) % 256 == 0
    size, offset, typ = raw[0], int.from_bytes(raw[1:3], 'big'), raw[3]
    if typ == 4: upper = int.from_bytes(raw[4:6], 'big') << 16
    if typ == 2: upper = int.from_bytes(raw[4:6], 'big') << 4
    if typ == 0:
        start, end = upper + offset, upper + offset + size
        assert 0x26000 <= start < end <= 0xe0000, (hex(start), hex(end))
        addresses.append((start, end))
assert min(x[0] for x in addresses) == 0x26000
binary = (b / 'zephyr/zephyr.bin').read_bytes()
sp, reset = struct.unpack_from('<II', binary)
assert 0x20000000 <= sp <= 0x20040000 and reset & 1 and 0x26000 <= reset < 0xe0000
header = (Path(__file__).resolve().parents[1] / 'src/zmk_descriptor.h').read_text()
descriptor = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', header))
assert descriptor in binary, 'Actual linked BIN must contain the generated ZMK descriptor'
assert setup_desc in binary, 'Separate vendor feature descriptor must be linked'
report = {'approved_for_flash': False, 'hardware_tested': False, 'application_base': '0x26000', 'hex_end': hex(max(x[1] for x in addresses)), 'binary_bytes': len(binary), 'initial_sp': hex(sp), 'reset_vector': hex(reset), 'binary_sha256': hashlib.sha256(binary).hexdigest(), 'descriptor_bytes': len(descriptor), 'descriptor_sha256': hashlib.sha256(descriptor).hexdigest(), 'checks': ['controller linked', 'bond storage enabled', 'explicit exact-target USB pairing gate', 'two HID interfaces; vendor feature ID0x40 body32', 'no production logging/console', 'no overwrite-oldest', 'no forced PHY', 'HEX contains application addresses only (no UICR/bootloader/settings)']}
(b / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
