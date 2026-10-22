#!/usr/bin/env python3
"""Reject false-green builds: inspect actual .config and generated Devicetree.

Clean-slate rules (feat/sofle-clean):
- left (central): split central, Studio RPC + DYA runtime modules, stock
  status screen (SOFLE_STATUS_SCREEN must be OFF), deep sleep off.
- right (peripheral): split peripheral, no Studio RPC, no custom screen.
- settings_reset: bare ZMK settings-reset build, only sanity checks.
"""
import argparse
import json
from pathlib import Path
import re


def numbers(body, prop, references=None):
    match = re.search(r"(?m)^\s*" + re.escape(prop) + r"\s*=\s*<(.*?)>;", body, re.S)
    if not match:
        raise AssertionError("Missing DTS property " + prop)
    raw = match[1]
    if references is not None:
        raw = re.sub(r"&(\w+)", lambda m: str(references[m[1]]), raw)
    assert "&" not in raw, (prop, "unresolved phandle")
    return [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", raw)]


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

    if name.startswith("settings_reset"):
        require("ZMK_SETTINGS_RESET_ON_START")
        require("SETTINGS")
        assert cfg.get("ZMK_SPLIT_ROLE_CENTRAL") != "y"
        return {"artifact": name, "role": "settings-reset",
                "hardware_tested": False}

    # Deep sleep is deliberately disabled on both halves: it is a per-half
    # unsynchronized sys_poweroff that breaks the split link on wake (zmk#718).
    assert cfg.get("ZMK_SLEEP") != "y", "deep sleep must stay disabled"
    # The clean-slate build uses the stock ZMK status screen only.
    assert cfg.get("SOFLE_STATUS_SCREEN") != "y", "custom status screen must stay off"
    assert cfg.get("ZMK_BEHAVIOR_OLED_TOGGLE") != "y", "oled toggle module must stay off"

    left = "_left_" in name
    if left:
        for symbol in ["ZMK_SPLIT", "ZMK_SPLIT_ROLE_CENTRAL", "USB_DEVICE_STACK",
                       "ZMK_STUDIO_RPC", "ZMK_POINTING",
                       "ZMK_RUNTIME_MACRO", "ZMK_RUNTIME_MACRO_STUDIO_RPC",
                       "ZMK_RUNTIME_COMBO", "ZMK_RUNTIME_COMBO_STUDIO_RPC",
                       "ZMK_RUNTIME_SENSOR_ROTATE", "ZMK_RUNTIME_SENSOR_ROTATE_STUDIO_RPC",
                       "ZMK_PHYSICAL_LAYOUTS_FEATURE_STUDIO_RPC",
                       "ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE"]:
            require(symbol)
        require("ZMK_HID_KEYBOARD_REPORT_SIZE", "6")
        require("ZMK_HID_CONSUMER_REPORT_SIZE", "6")
        require("ZMK_DISPLAY")
    else:
        for symbol in ["ZMK_SPLIT", "USB_DEVICE_STACK", "ZMK_DISPLAY"]:
            require(symbol)
        assert cfg.get("ZMK_SPLIT_ROLE_CENTRAL") != "y"
        assert cfg.get("ZMK_STUDIO_RPC") != "y"

    if name.endswith("_release"):
        # PRINTK may be force-enabled by imported NCS defaults; CONSOLE=n
        # keeps it silent. LOG must be off in release.
        for symbol in ["LOG", "CONSOLE", "BOOT_BANNER"]:
            assert cfg.get(symbol) != "y", ("release logging", symbol)
    else:
        require("LOG")
        # Debug builds log at DBG (4) so split-link loss/reconnect events are
        # visible; the previous release INF(3) value was too quiet to diagnose.
        require("ZMK_LOG_LEVEL", "4")

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
    layers = None
    if left:
        phandles = {}
        cells = {}
        for node, body in nodes.items():
            if re.search(r"\bphandle\s*=", body):
                phandle = numbers(body, "phandle")[0]
                phandles[node] = phandle
                if "#binding-cells" in body:
                    cells[phandle] = numbers(body, "#binding-cells")[0]
        references = {}
        for match in re.finditer(r"(?m)^\s*((?:\w+:\s*)+)([\w,@.\-]+)\s*\{", dts):
            if match[2] in phandles:
                for label in re.findall(r"(\w+):", match[1]):
                    references[label] = phandles[match[2]]
        expected_sensors = {
            "default_layer": [phandles["rsr_volume"], phandles["rsr_nav"]],
            "lower_layer": [phandles["rsr_volume"], phandles["rsr_nav"]],
            "raise_layer": [phandles["rsr_volume"], phandles["rsr_nav"]],
        }
        layers = {}
        for layer, sensors in expected_sensors.items():
            body = nodes[layer]
            assert numbers(body, "sensor-bindings", references) == sensors, (layer, "wrong encoder defaults")
            encoded = numbers(body, "bindings", references)
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
        # BT channels on RAI row 1: BT_SEL 0..3 reachable (4 spare).
        for idx, profile in ((1, 0), (2, 1), (3, 2), (4, 3)):
            assert layers["raise_layer"][idx][1] == [profile], (
                "BT_SEL", idx, profile, layers["raise_layer"][idx],
                [x[1] for x in layers["raise_layer"][:8]])
    return {"artifact": name, "role": "central" if left else "peripheral",
            "bindings_per_layer": {k: len(v) for k, v in layers.items()} if left else None,
            "encoder_defaults": "left volume (PCB inverted) / right nav, all layers",
            "bt_channels": "0/1/2 Macs, 3 dongle, 4 spare",
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
