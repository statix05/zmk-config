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
import yaml
from west.manifest import Manifest

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
    manifest = Manifest(topdir=args.base)
    resolved = manifest.as_dict()
    active = {p.name: p for p in manifest.projects
              if p.name != "manifest" and manifest.is_active(p)}
    frozen_projects = []
    inactive = []
    for record in resolved["manifest"]["projects"]:
        project = active.get(record["name"])
        if project is None:
            inactive.append(record["name"])
            continue
        if not project.is_cloned():
            raise RuntimeError("Active west project is not cloned: " + project.name)
        record["revision"] = project.sha("HEAD")
        frozen_projects.append(record)
    resolved["manifest"]["projects"] = frozen_projects
    (out / "frozen-west.yml").write_text(yaml.safe_dump(resolved, sort_keys=False))
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
            "container_image": IMAGE if os.environ.get("GITHUB_ACTIONS") == "true" else None,
            "configured_build_image": IMAGE, "python": sys.version, "sdk": sdk_versions,
            "west": command(["west", "--version"]), "cmake": command(["cmake", "--version"]),
            "compiler": command([compiler, "--version"]) if compiler else None,
            "hardware_tested": False, "frozen_scope": "all active projects",
            "active_project_count": len(frozen_projects),
            "excluded_inactive_projects": inactive}
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
