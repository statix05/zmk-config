#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile ONLY the real ZMK descriptor initializer with a tiny HID macro shim.
No guessed report map. Requires a source checkout at the pinned commit.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('core', type=Path)
p.add_argument('--check', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
core = a.core.resolve()
commit = subprocess.check_output(['git', '-C', str(core), 'rev-parse', 'HEAD'], text=True).strip()
assert commit == 'e5c9b6915b56801193e359dd9bad4a167ce0d1b8', commit
hid = core / 'app/include/zmk/hid.h'
s = hid.read_text()
start = s.index('#define ZMK_HID_MAIN_VAL_DATA')
end = s.index('\n};', s.index('static const uint8_t zmk_hid_report_desc[]')) + 3
shim = '''#include <stdint.h>
#include <stdio.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#define IS_ENABLED(x) x
#define CONFIG_ZMK_HID_REPORT_TYPE_HKRO 1
#define CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_BASIC 1
#define CONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE 6
#define CONFIG_ZMK_HID_CONSUMER_REPORT_SIZE 6
#define CONFIG_ZMK_POINTING 1
#define HID_COLLECTION_APPLICATION 1
#define HID_COLLECTION_PHYSICAL 0
#define HID_COLLECTION_LOGICAL 2
#define HID_USAGE_PAGE(x) 0x05,x
#define HID_USAGE(x) 0x09,x
#define HID_COLLECTION(x) 0xa1,x
#define HID_REPORT_ID(x) 0x85,x
#define HID_USAGE_MIN8(x) 0x19,x
#define HID_USAGE_MAX8(x) 0x29,x
#define HID_USAGE_MAX16(a,b) 0x2a,a,b
#define HID_LOGICAL_MIN8(x) 0x15,x
#define HID_LOGICAL_MAX8(x) 0x25,x
#define HID_LOGICAL_MIN16(a,b) 0x16,a,b
#define HID_LOGICAL_MAX16(a,b) 0x26,a,b
#define HID_REPORT_SIZE(x) 0x75,x
#define HID_REPORT_COUNT(x) 0x95,x
#define HID_INPUT(x) 0x81,x
#define HID_OUTPUT(x) 0x91,x
#define HID_END_COLLECTION 0xc0
#define HID_ITEM_TAG_USAGE 0
#define HID_USAGE_GEN_DESKTOP 1
#define HID_ITEM_TAG_PHYSICAL_MIN 3
#define HID_ITEM_TAG_PHYSICAL_MAX 4
#define HID_ITEM_TYPE_GLOBAL 1
#define HID_ITEM_TYPE_LOCAL 2
#define HID_ITEM(tag,type,size) ((tag<<4)|(type<<2)|size)
'''
# Real header defines these above the descriptor region.
shim += '#define ZMK_HID_MOUSE_NUM_BUTTONS 5\n'
struct_start = s.index('struct zmk_hid_keyboard_report_body {')
struct_end = s.index('#if IS_ENABLED(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING)', struct_start)
# Compile the actual report structures too, not a hand-written replica.
import re
typedefs = ''
for filename, typename in [('keys.h', 'zmk_mod_flags_t'), ('pointing.h', 'zmk_mouse_button_flags_t')]:
    text = (core / 'app/include/zmk' / filename).read_text()
    match = re.search(r'typedef uint8_t ' + typename + r';', text)
    assert match, typename
    typedefs += match.group(0) + '\n'
structs = '#define __packed __attribute__((packed))\n' + typedefs + s[struct_start:struct_end] + '\n#endif\n'
assertions = '''
_Static_assert(sizeof(struct zmk_hid_keyboard_report_body) == 8, "HKRO body drift");
_Static_assert(sizeof(struct zmk_hid_consumer_report_body) == 6, "BASIC consumer drift");
_Static_assert(sizeof(struct zmk_hid_mouse_report_body) == 9, "Four int16 mouse axes drift");
_Static_assert(sizeof(struct zmk_hid_keyboard_report) == 9, "USB keyboard drift");
_Static_assert(sizeof(struct zmk_hid_consumer_report) == 7, "USB consumer drift");
_Static_assert(sizeof(struct zmk_hid_mouse_report) == 10, "USB mouse drift");
'''
program = shim + s[start:end] + '\n' + structs + assertions + '\nint main(void) { return fwrite(zmk_hid_report_desc, 1, sizeof(zmk_hid_report_desc), stdout) != sizeof(zmk_hid_report_desc); }\n'
scratch = Path(__import__('os').environ.get('TMPDIR', str(Path.home() / '.hermes/cache/scratch')))
if str(scratch).startswith(('/var/', '/tmp')):
    scratch = Path.home() / '.hermes/cache/scratch'
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch) as tmp:
    src, exe = Path(tmp) / 'desc.c', Path(tmp) / 'desc'
    src.write_text(program)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Werror', '-I', str(core / 'app/include'), str(src), '-o', str(exe)], check=True)
    data = subprocess.check_output([str(exe)])
text = '/* Generated from ZMK hid.h; Copyright (c) 2020 The ZMK Contributors\n * SPDX-License-Identifier: MIT\n * Run tools/derive_descriptor.py with the pinned core to reproduce. */\n#pragma once\n#include <stdint.h>\nstatic const uint8_t bridge_report_desc[] = {\n'
for i in range(0, len(data), 16):
    text += '    ' + ', '.join(f'0x{v:02x}' for v in data[i:i+16]) + ',\n'
text += '};\n'
output = root / 'src/zmk_descriptor.h'
if a.check:
    assert output.read_text() == text, 'ZMK descriptor drift'
else:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text)
print(json.dumps({'core': commit, 'descriptor_bytes': len(data), 'descriptor_sha256': hashlib.sha256(data).hexdigest(), 'hid_h_sha256': hashlib.sha256(hid.read_bytes()).hexdigest()}, indent=2))
