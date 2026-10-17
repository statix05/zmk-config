#!/usr/bin/env python3
"""Reject false-green builds: inspect actual .config and generated Devicetree."""
import argparse
import json
from pathlib import Path
import re


def numbers(body, prop):
    match = re.search(r"(?m)^\s*" + re.escape(prop) + r"\s*=\s*<(.*?)>;", body, re.S)
    if not match:
        raise AssertionError("Missing DTS property " + prop)
    return [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", match[1])]


def verify(build, name):
    config_path = build / "zephyr/.config"
    dts_path = build / "zephyr/zephyr.dts"
    if not config_path.exists():
        config_path, dts_path = build / "config.txt", build / "zephyr.dts"
    config = config_path.read_text()
    dts = dts_path.read_text()
    cfg = dict(re.findall(r"^CONFIG_(\w+)=(.*)$", config, re.M))
    def require(symbol, value="y"):
        assert cfg.get(symbol) == value, (symbol, cfg.get(symbol), value)
    for symbol in ["SOFLE_STATUS_SCREEN", "USB_DEVICE_STACK", "ZMK_SPLIT",
                   "ZMK_SPLIT_RELAY_EVENT", "LV_USE_IMAGE", "ZMK_DISPLAY_STATUS_SCREEN_CUSTOM"]:
        require(symbol)
    assert cfg.get("ZMK_DISPLAY_BLANK_ON_IDLE") != "y"
    assert cfg.get("NICE_OLED") != "y"
    assert cfg.get("ZMK_BEHAVIOR_LOCAL_ID_TYPE_CRC16") != "y"
    left = "_left_" in name
    if left:
        for symbol in ["ZMK_SPLIT_ROLE_CENTRAL", "ZMK_STUDIO_RPC", "ZMK_POINTING",
                       "ZMK_RUNTIME_MACRO", "ZMK_RUNTIME_MACRO_STUDIO_RPC",
                       "ZMK_RUNTIME_COMBO", "ZMK_RUNTIME_COMBO_STUDIO_RPC",
                       "ZMK_RUNTIME_SENSOR_ROTATE", "ZMK_RUNTIME_SENSOR_ROTATE_STUDIO_RPC",
                       "ZMK_PHYSICAL_LAYOUTS_FEATURE_STUDIO_RPC",
                       "ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE"]:
            require(symbol)
        require("ZMK_HID_KEYBOARD_REPORT_SIZE", "6")
        require("ZMK_HID_CONSUMER_REPORT_SIZE", "6")
    else:
        assert cfg.get("ZMK_SPLIT_ROLE_CENTRAL") != "y"
        assert cfg.get("ZMK_STUDIO_RPC") != "y"
    if name.endswith("_release"):
        for symbol in ["LOG", "PRINTK", "CONSOLE", "BOOT_BANNER"]:
            assert cfg.get(symbol) != "y", ("release logging", symbol)
    else:
        require("LOG")
        require("ZMK_LOG_LEVEL", "3")
    nodes = {}
    for match in re.finditer(r"(?m)^\s*(?:\w+:\s*)?([\w,@.\-]+)\s*\{([^{}]*)\};", dts, re.S):
        nodes[match[1]] = match[2]
    # Same UF2 application/NVS/bootloader layout as the working nice_nano_v2.
    for node, expected in {
        "partition@26000": [0x26000, 0xc6000],
        "partition@ec000": [0xec000, 0x8000],
        "partition@f4000": [0xf4000, 0xc000],
    }.items():
        assert numbers(nodes[node], "reg") == expected, (node, "partition changed")
    phandles = {}
    cells = {}
    for node, body in nodes.items():
        if re.search(r"\bphandle\s*=", body):
            phandle = numbers(body, "phandle")[0]
            phandles[node] = phandle
            if "#binding-cells" in body:
                cells[phandle] = numbers(body, "#binding-cells")[0]
    expected_sensors = {
        "default_layer": [phandles["rsr_volume"], phandles["rsr_nav"]],
        "lower_layer": [phandles["rsr_volume"], phandles["rsr_zoom"]],
        "raise_layer": [phandles["rsr_volume"], phandles["rsr_nav"]],
    }
    layers = {}
    for layer, sensors in expected_sensors.items():
        body = nodes[layer]
        assert numbers(body, "sensor-bindings") == sensors, (layer, "wrong encoder defaults")
        encoded = numbers(body, "bindings")
        decoded = []
        i = 0
        while i < len(encoded):
            controller = encoded[i]
            assert controller in cells, (layer, "unknown behavior", controller)
            count = cells[controller]
            decoded.append((controller, encoded[i + 1:i + 1 + count]))
            i += count + 1
        assert i == len(encoded) and len(decoded) == 60, (layer, len(decoded))
        layers[layer] = decoded
    assert layers["default_layer"][43][0] == phandles["dbl_click"]
    assert layers["lower_layer"][42][0] == phandles["behavior_oled_toggle"]
    assert layers["lower_layer"][43][0] == phandles["behavior_none"]
    assert layers["default_layer"][57][1] == [1]
    assert layers["default_layer"][58][1] == [2]
    assert numbers(nodes["rsr_zoom"], "cw-binding") == [phandles["profile_zoom"], 0]
    assert numbers(nodes["rsr_zoom"], "ccw-binding") == [phandles["profile_zoom"], 1]
    return {"artifact": name, "role": "central" if left else "peripheral",
            "bindings_per_layer": {k: len(v) for k, v in layers.items()},
            "encoder_order": "left volume / right nav, zoom, nav",
            "default_pushes": "BASE right dbl_click; AUX1 left shared OLED / right none",
            "release_logging_off": name.endswith("_release"), "hardware_tested": False}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True, type=Path)
    ap.add_argument("--name", required=True)
    args = ap.parse_args()
    result = verify(args.build, args.name)
    out = args.build / "artifacts" / (args.name + "-metadata")
    out.mkdir(parents=True, exist_ok=True)
    (out / "SOFLE_BUILD_CHECK.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
