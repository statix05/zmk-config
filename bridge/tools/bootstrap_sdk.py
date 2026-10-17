#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Optional host-only SDK setup. Never opens a device, disk or serial port."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import urllib.request

p = argparse.ArgumentParser()
p.add_argument('workspace', type=Path)
a = p.parse_args()
w = a.workspace.resolve()
w.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[1]
manifest = w / 'bridge-manifest'
manifest.mkdir(exist_ok=True)
shutil.copy2(root / ('sdk-lock.yml' if (root / 'sdk-lock.yml').exists() else 'west.yml'), manifest / 'west.yml')
venv = w / 'venv'
if not venv.exists():
    subprocess.run(['uv', 'venv', '--python', '3.11', str(venv)], check=True)
python = str(venv / 'bin/python')
subprocess.run(['uv', 'pip', 'install', '--python', python, 'west==1.3.0'], check=True)
west = str(venv / 'bin/west')
if not (w / '.west').exists():
    subprocess.run([west, 'init', '-l', str(manifest)], cwd=w, check=True)
subprocess.run([west, 'update', '--narrow', '-o=--depth=1'], cwd=w, check=True)
subprocess.run(['uv', 'pip', 'install', '--python', python, '-r', str(w / 'zephyr/scripts/requirements.txt')], check=True)
host = {'Darwin': 'macos', 'Linux': 'linux'}[platform.system()]
arch = {'arm64': 'aarch64', 'aarch64': 'aarch64', 'x86_64': 'x86_64'}[platform.machine()]
base = 'https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.17.0/'
files = [f'zephyr-sdk-0.17.0_{host}-{arch}_minimal.tar.xz', f'toolchain_{host}-{arch}_arm-zephyr-eabi.tar.xz']
checksums = urllib.request.urlopen(base + 'sha256.sum').read().decode()
expected = {line.split()[-1].lstrip('*'): line.split()[0] for line in checksums.splitlines() if len(line.split()) == 2}
sdk = w / 'zephyr-sdk-0.17.0'
for name in files:
    archive = w / name
    if not archive.exists():
        urllib.request.urlretrieve(base + name, archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    assert digest == expected[name], f'SDK checksum failed: {name}'
    target = w if name.startswith('zephyr-sdk-') else sdk
    with tarfile.open(archive) as tf:
        tf.extractall(target, filter='data')
# Explicit ZEPHYR_SDK_INSTALL_DIR avoids a global CMake package registration.
print(json.dumps({'workspace': str(w), 'sdk': str(sdk), 'west': west, 'python': python}, indent=2))
