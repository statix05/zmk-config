#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build + inspect + package, NEVER flash. Run after bootstrap_sdk.py."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
p = argparse.ArgumentParser()
p.add_argument('workspace', type=Path)
a = p.parse_args()
w = a.workspace.resolve()
root = Path(__file__).resolve().parents[1]
build = root / 'build'
env = dict(os.environ, ZEPHYR_SDK_INSTALL_DIR=str(w / 'zephyr-sdk-0.17.0'), ZEPHYR_TOOLCHAIN_VARIANT='zephyr')
env['PATH'] = str(w / 'venv/bin') + os.pathsep + env['PATH']
cmd = [str(w / 'venv/bin/west'), 'build', '--no-sysbuild', '-b', 'bridge_nano/nrf52840', '-s', str(root), '-d', str(build), '-p', 'always', '--', '-DBOARD_ROOT=' + str(root)]
print(' '.join(cmd), flush=True)
subprocess.run(cmd, cwd=w, env=env, check=True)
subprocess.run([sys.executable, str(root / 'tools/verify_build.py'), str(build)], check=True)
subprocess.run([sys.executable, str(root / 'tools/candidate_uf2.py'), str(build / 'zephyr/zephyr.bin'), str(build / 'sofle_bridge_NOT_APPROVED.uf2'), '--base', '0x26000', '--family-id', '0x239a00b3', '--acknowledge-unapproved'], check=True)
