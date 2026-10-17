#!/usr/bin/env python3
"""Preserve actual tool versions, frozen west manifest, configs and ELF/map."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

IMAGE = "zmkfirmware/zmk-build-arm@sha256:ca4978c179533772d3a77296bc628f8a3ae1ed75ee1cc8b411570f9810fd821e"


def command(args, cwd=None):
    p = subprocess.run(args, cwd=cwd, text=True, capture_output=True)
    return {"returncode": p.returncode, "stdout": p.stdout.strip(), "stderr": p.stderr.strip()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", type=Path, required=True)
    ap.add_argument("--build", type=Path, required=True)
    ap.add_argument("--name", required=True)
    args = ap.parse_args()
    out = args.build / "artifacts" / (args.name + "-metadata")
    out.mkdir(parents=True, exist_ok=True)
    frozen = command(["west", "manifest", "--freeze"], args.base)
    if frozen["returncode"]:
        raise RuntimeError(frozen["stderr"])
    (out / "frozen-west.yml").write_text(frozen["stdout"] + "\n")
    cache = args.build / "CMakeCache.txt"
    compiler = None
    if cache.exists():
        match = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.*)$", cache.read_text(), re.M)
        if match:
            compiler = match[1]
    sdk_versions = {}
    for root in [Path("/opt"), Path("/opt/toolchains")]:
        for sdk in root.glob("zephyr-sdk*"):
            v = sdk / "sdk_version"
            sdk_versions[str(sdk)] = v.read_text().strip() if v.exists() else sdk.name
    meta = {"source_commit": os.environ.get("GITHUB_SHA"), "artifact": args.name,
            "container_image": IMAGE, "python": sys.version, "sdk": sdk_versions,
            "west": command(["west", "--version"]), "cmake": command(["cmake", "--version"]),
            "compiler": command([compiler, "--version"]) if compiler else None,
            "hardware_tested": False}
    files = {".config": "config.txt", "zephyr.dts": "zephyr.dts",
             "zmk.elf": "zmk.elf", "zephyr.elf": "zephyr.elf",
             "zmk.map": "zmk.map", "zephyr.map": "zephyr.map"}
    hashes = {}
    for source, target in files.items():
        p = args.build / "zephyr" / source
        if p.exists():
            shutil.copy2(p, out / target)
            hashes[target] = hashlib.sha256(p.read_bytes()).hexdigest()
    meta["sha256"] = hashes
    (out / "BUILD_INFO.json").write_text(json.dumps(meta, indent=2) + "\n")
    print(json.dumps({k: v for k, v in meta.items() if k != "sha256"}, indent=2))


if __name__ == "__main__":
    main()
