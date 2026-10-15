#!/usr/bin/env python3
"""Compile the firmware's exact C renderer; verify bounds and produce previews.

python3 tools/test_sofle_status.py --lvgl /path/to/pinned/lvgl --out /path
Needs a C compiler and Pillow; neither is a firmware dependency.
"""
import argparse
import ctypes as C
import itertools
import json
from pathlib import Path
import subprocess
import tempfile
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]


class State(C.Structure):
    _fields_ = [("central", C.c_bool), ("usb_powered", C.c_bool),
                ("battery_valid", C.c_bool), ("battery_percent", C.c_uint8),
                ("usb_output", C.c_bool), ("bt_profile", C.c_uint8),
                ("connected", C.c_bool), ("bonded", C.c_bool),
                ("layer", C.c_char * 6)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lvgl", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=ROOT / "docs" / "oled-preview")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sofle-native-", dir=args.out) as tmp:
        temp = Path(tmp)
        adapter = temp / "adapter.c"
        adapter.write_text('#include "sofle_union_logo.h"\n'
                           'const unsigned char *test_logo(void) {return sofle_union_logo.data+8;}\n')
        so = temp / "render.so"
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                        "-DLV_CONF_SKIP=1", "-I" + str(args.lvgl.resolve()),
                        "-I" + str(ROOT / "src"), str(ROOT / "src/sofle_status_render.c"),
                        str(ROOT / "src/sofle_union_logo.c"), str(adapter), "-o", str(so)],
                       check=True)
        lib = C.CDLL(str(so))
        lib.test_logo.restype = C.POINTER(C.c_uint8)
        logo_ptr = lib.test_logo()
        logo = bytes(logo_ptr[:128])
        fn = lib.sofle_status_render
        fn.argtypes = [C.POINTER(C.c_uint8), C.POINTER(State), C.POINTER(C.c_uint8), C.c_bool]

        def render(state, invert=False):
            # Guard bytes detect an overrun on either side of the 520-byte frame.
            raw = (C.c_uint8 * 552)(*([0xA5] * 552))
            ptr = C.cast(C.byref(raw, 16), C.POINTER(C.c_uint8))
            fn(ptr, C.byref(state), logo_ptr, invert)
            assert bytes(raw[:16]) == b'\xa5' * 16
            assert bytes(raw[-16:]) == b'\xa5' * 16
            data = bytes(raw[16:-16])
            assert data[:8] == (bytes([255, 255, 255, 255, 0, 0, 0, 255]) if invert
                                else bytes([0, 0, 0, 255, 255, 255, 255, 255]))
            # Decode the exact physical frame, then undo the clockwise mapping
            # to show it as seen on a vertically-mounted panel.
            physical = Image.new("L", (128, 32))
            portrait = Image.new("L", (32, 128))
            for y in range(128):
                for x in range(32):
                    px, py = 127 - y, x
                    value = 255 if data[8 + py * 16 + px // 8] & (128 >> (px % 8)) else 0
                    physical.putpixel((px, py), value)
                    portrait.putpixel((x, y), value)
            for y in range(32):
                for x in range(32):
                    expected = 255 if logo[y * 4 + x // 8] & (128 >> (x % 8)) else 0
                    assert portrait.getpixel((x, y)) == expected, "Logo overwritten"
            for top, bottom in [(32, 40), (47, 50), (57, 70), (77, 82), (89, 100), (107, 112), (119, 128)]:
                assert not portrait.crop((0, top, 32, bottom)).getbbox(), "Pixels escaped their region"
            return data, physical, portrait

        regions = {"logo": [0, 0, 32, 32], "power": [0, 40, 32, 47],
                   "battery": [0, 50, 32, 57], "transport": [0, 70, 32, 77],
                   "connection": [0, 82, 32, 89], "layer_label": [0, 100, 32, 107],
                   "layer_value": [0, 112, 32, 119]}
        for a, b in itertools.combinations(regions.values(), 2):
            assert not (max(a[0], b[0]) < min(a[2], b[2]) and
                        max(a[1], b[1]) < min(a[3], b[3]))

        count = 0
        for central, usb, valid, connected, pct, layer in itertools.product(
                [False, True], [False, True], [False, True], [False, True],
                [0, 1, 9, 10, 99, 100], [b"BAS", b"AUX1", b"RAI", b"ABCDE"]):
            state = State(central, usb, valid, pct, usb, 4, connected, True, layer)
            normal, _, _ = render(state)
            inverted, _, _ = render(state, True)
            assert normal[8:] == inverted[8:]
            if usb or not valid:
                other = State(central, usb, valid, (pct + 37) % 101, usb, 4, connected, True, layer)
                assert normal == render(other)[0], "Invalid/USB battery leaked into UI"
            count += 1

        examples = [("left-battery", State(True, False, True, 72, False, 0, True, True, b"BAS")),
                    ("left-usb", State(True, True, False, 100, True, 0, True, True, b"AUX1")),
                    ("right-battery", State(False, False, True, 83, False, 0, True, True, b"")),
                    ("right-settling", State(False, False, False, 100, False, 0, False, False, b""))]
        sheet = Image.new("RGB", (4 * 180, 560), "#202020")
        draw = ImageDraw.Draw(sheet)
        for i, (name, state) in enumerate(examples):
            _, physical, portrait = render(state)
            portrait.save(args.out / f"{name}-portrait.png")
            physical.save(args.out / f"{name}-panel.png")
            sheet.paste(portrait.resize((128, 512), Image.Resampling.NEAREST), (i * 180 + 20, 25))
            draw.text((i * 180 + 8, 542), name, fill="white")
        sheet.save(args.out / "overview.png")
        result = {"states_tested": count, "rotation": "clockwise, 32x128 -> 128x32",
                  "guard_bytes": "passed", "logo_overwrite": "none",
                  "region_intersection": "none", "usb_battery_mask": "passed", "regions": regions}
        (args.out / "test-results.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
