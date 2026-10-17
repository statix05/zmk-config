#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Package build-only BIN, requiring explicit address AND observed family ID.
No mounts, serial ports, copy-to-device or flashing. Family mismatch stays visible.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
p = argparse.ArgumentParser()
p.add_argument('binary', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--base', required=True, type=lambda v: int(v, 0))
p.add_argument('--family-id', required=True, type=lambda v: int(v, 0))
p.add_argument('--acknowledge-unapproved', required=True, action='store_true')
a = p.parse_args()
assert a.base == 0x26000, 'Only explicit compile-only nice_nano-compatible offset is supported'
assert a.family_id == 0x239a00b3, 'Observed CURRENT.UF2 family differs: investigate, do NOT normalize to ADA52840'
assert a.output.name.endswith('NOT_APPROVED.uf2'), 'Artifact must be visibly unapproved'
assert not str(a.output.resolve()).startswith(('/Volumes/', '/dev/')), 'Device paths forbidden'
data = a.binary.read_bytes()
assert data and a.base + len(data) <= 0xe0000
sp, reset = struct.unpack_from('<II', data)
assert 0x20000000 <= sp <= 0x20040000 and reset & 1 and a.base <= (reset & ~1) < a.base + len(data), 'Invalid candidate vectors'
n = (len(data) + 255) // 256
uf2 = bytearray()
for i in range(n):
    chunk = data[256*i:256*(i+1)]
    header = struct.pack('<8I', 0x0a324655, 0x9e5d5157, 0x2000, a.base + 256*i, 256, i, n, a.family_id)
    uf2 += header + chunk.ljust(476, b'\x00') + struct.pack('<I', 0x0ab16f30)
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_bytes(uf2)
# Read-back the packaged artifact and validate all block headers/address bounds.
r = a.output.read_bytes()
for i in range(n):
    h = struct.unpack_from('<8I', r, 512*i)
    assert h == (0x0a324655, 0x9e5d5157, 0x2000, a.base + 256*i, 256, i, n, a.family_id)
    assert struct.unpack_from('<I', r, 512*i + 508)[0] == 0x0ab16f30
assert b''.join(r[i*512+32:i*512+288] for i in range(n))[:len(data)] == data
print(json.dumps({'approved_for_flash': False, 'base': hex(a.base), 'family_id_observed_not_normalized': hex(a.family_id), 'binary_bytes': len(data), 'blocks': n, 'sha256': hashlib.sha256(r).hexdigest()}, indent=2))
