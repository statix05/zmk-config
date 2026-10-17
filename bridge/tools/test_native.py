#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host tests only. No USB/BLE access and no flash/reset commands."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
scratch = Path(os.environ.get('TMPDIR', Path.home() / '.hermes/cache/scratch'))
# Never use the macOS system temporary directory for probes.
if str(scratch).startswith('/var/') or str(scratch).startswith('/tmp'):
    scratch = Path.home() / '.hermes/cache/scratch'
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch) as tmp:
    exe = str(Path(tmp) / 'transport-tests')
    cmd = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g', '-I', str(root / 'src'), str(root / 'src/transport.c'), str(root / 'tests/test_transport.c'), '-o', exe]
    subprocess.run(cmd, check=True)
    subprocess.run([exe], check=True)
    exe = str(Path(tmp) / 'setup-tests')
    cmd = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g', '-I', str(root / 'src'), str(root / 'src/setup.c'), str(root / 'tests/test_setup.c'), '-o', exe]
    subprocess.run(cmd, check=True)
    subprocess.run([exe], check=True)
print('PASS: native executable compiled with ASan+UBSan; NOT a firmware/hardware test')
