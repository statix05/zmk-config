#!/usr/bin/env python3
"""Rasterize Union's actual SVG path, never an OS thumbnail.

Usage: python3 tools/generate_sofle_logo.py /path/to/Union.svg
Requires Python 3 and Pillow. Default outputs are relative to this script's repo.
Full viewBox is mapped to 32x32, with zero margin by default; --margin 1 maps
it into the central 30x30 without cropping. Light source fill becomes white
(index 1), background opaque black (index 0). No polarity auto-detection.
Cubic curves are adaptively flattened to <= 1/64 output pixel error, rendered
at 16x resolution, BOX-downsampled once, then thresholded at coverage >=128.
Only a single, closed, non-self-intersecting M/L/H/V/C/Z path is supported;
unsupported SVG features fail explicitly instead of silently changing artwork.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw

SIZE = 32
SCALE = 16
PALETTE = bytes((0, 0, 0, 255, 255, 255, 255, 255))
TOKEN = re.compile(r"[MmLlHhVvCcZz]|[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?")


def tokenize(data):
    tokens = []
    last = 0
    for match in TOKEN.finditer(data):
        if data[last:match.start()].strip(' ,\t\r\n'):
            raise ValueError('Unsupported SVG path syntax')
        tokens.append(match.group())
        last = match.end()
    if data[last:].strip(' ,\t\r\n'):
        raise ValueError('Unsupported SVG path syntax')
    return tokens


def flatten_cubic(p0, p1, p2, p3, tolerance, output, depth=0):
    # Control-polygon excess also detects collinear overshoot/backtracking.
    chord = math.dist(p0, p3)
    excess = sum(math.dist(a, b) for a, b in zip((p0, p1, p2), (p1, p2, p3))) - chord
    if chord:
        distances = [abs((p3[0]-p0[0])*(p[1]-p0[1]) -
                         (p3[1]-p0[1])*(p[0]-p0[0])) / chord for p in (p1, p2)]
    else:
        distances = [math.dist(p0, p1), math.dist(p0, p2)]
    if max(distances) <= tolerance and excess <= tolerance:
        output.append(p3)
        return
    if depth >= 24:
        raise ValueError('Cubic subdivision did not converge')
    midpoint = lambda a, b: ((a[0]+b[0])/2, (a[1]+b[1])/2)
    a, b, c = midpoint(p0, p1), midpoint(p1, p2), midpoint(p2, p3)
    d, e = midpoint(a, b), midpoint(b, c)
    f = midpoint(d, e)
    flatten_cubic(p0, a, d, f, tolerance, output, depth+1)
    flatten_cubic(f, e, c, p3, tolerance, output, depth+1)


def parse_path(data, tolerance):
    tokens = tokenize(data)
    position = (0.0, 0.0)
    points = []
    command = None
    i = 0
    closed = False
    commands = set()
    while i < len(tokens):
        if tokens[i].isalpha():
            command = tokens[i]
            commands.add(command.upper())
            i += 1
        if command is None or closed:
            raise ValueError('Unexpected path data')
        relative = command.islower()
        kind = command.upper()
        if kind == 'Z':
            if not points:
                raise ValueError('Empty closed path')
            points.append(points[0])
            position = points[0]
            closed = True
            command = None
            continue
        count = {'M': 2, 'L': 2, 'H': 1, 'V': 1, 'C': 6}[kind]
        values = tokens[i:i+count]
        if len(values) != count or any(v.isalpha() for v in values):
            raise ValueError('Incomplete SVG command')
        values = [float(v) for v in values]
        if not all(math.isfinite(v) for v in values):
            raise ValueError('Non-finite SVG coordinate')
        i += count
        def pair(offset):
            x, y = values[offset:offset+2]
            return (x+position[0], y+position[1]) if relative else (x, y)
        if kind == 'M':
            if points:
                raise ValueError('Multiple subpaths are not supported')
            position = pair(0)
            points.append(position)
            command = 'l' if relative else 'L'
        elif kind == 'L':
            position = pair(0)
            points.append(position)
        elif kind == 'H':
            position = (values[0] + (position[0] if relative else 0), position[1])
            points.append(position)
        elif kind == 'V':
            position = (position[0], values[0] + (position[1] if relative else 0))
            points.append(position)
        elif kind == 'C':
            p1, p2, p3 = pair(0), pair(2), pair(4)
            flatten_cubic(position, p1, p2, p3, tolerance, points)
            position = p3
    if not closed:
        raise ValueError('Expected one explicitly closed SVG path')
    return points, sorted(commands)


def render(svg, margin):
    root = ET.fromstring(svg)
    if root.tag.rsplit('}', 1)[-1] != 'svg':
        raise ValueError('Expected SVG root')
    children = list(root)
    if len(children) != 1 or children[0].tag.rsplit('}', 1)[-1] != 'path':
        raise ValueError('Expected exactly one path and no other elements')
    if root.get('transform') or children[0].get('transform'):
        raise ValueError('Transforms are not supported')
    box = [float(n) for n in root.attrib['viewBox'].replace(',', ' ').split()]
    if len(box) != 4 or box[2] <= 0 or box[3] <= 0:
        raise ValueError('Invalid viewBox')
    factor = (SIZE-2*margin) / max(box[2:])
    points, commands = parse_path(children[0].attrib['d'], 1/(64*factor))
    dx = margin + ((SIZE-2*margin)-box[2]*factor)/2
    dy = margin + ((SIZE-2*margin)-box[3]*factor)/2
    # Sample at supersample-pixel centers, not image corners.
    polygon = [(((x-box[0])*factor+dx)*SCALE-.5,
                ((y-box[1])*factor+dy)*SCALE-.5) for x, y in points]
    high = Image.new('L', (SIZE*SCALE, SIZE*SCALE), 0)
    ImageDraw.Draw(high).polygon(polygon, fill=255)
    coverage = high.resize((SIZE, SIZE), Image.Resampling.BOX)
    image = coverage.point([0]*128 + [255]*128, mode='1')
    return image, box, commands, len(points)


def pack(image):
    rows = bytearray()
    for y in range(SIZE):
        for x in range(0, SIZE, 8):
            rows.append(sum(bool(image.getpixel((x+bit, y))) << (7-bit) for bit in range(8)))
    assert len(rows) == 128
    result = PALETTE + rows
    assert len(result) == 136
    decoded = Image.new('1', (SIZE, SIZE))
    for y in range(SIZE):
        for x in range(SIZE):
            decoded.putpixel((x, y), 255 if rows[y*4+x//8] & (0x80 >> (x%8)) else 0)
    assert decoded.tobytes() == image.tobytes(), 'MSB-first round-trip failed'
    assert image.getbbox() and sum(byte.bit_count() for byte in image.tobytes()) < SIZE*SIZE
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input_svg', type=Path)
    parser.add_argument('--output-root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--margin', type=int, choices=(0, 1), default=0)
    args = parser.parse_args()
    svg = args.input_svg.read_bytes()
    image, box, commands, vertices = render(svg, args.margin)
    data = pack(image)
    digest = hashlib.sha256(svg).hexdigest()
    root = args.output_root
    for directory in ('src', 'docs'):
        (root/directory).mkdir(parents=True, exist_ok=True)
    header = '''/* Generated by tools/generate_sofle_logo.py; do not edit. */
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_img_dsc_t sofle_union_logo;

#ifdef __cplusplus
}
#endif
'''
    lines = ['    ' + ', '.join(f'0x{n:02x}' for n in data[i:i+8]) + ',' for i in range(0, len(data), 8)]
    source = f'''/* Generated from Union.svg by tools/generate_sofle_logo.py.
 * Input SHA256: {digest}
 * Full viewBox {box}; margin {args.margin} pixels; no crop or inversion.
 * Palette: opaque black index 0, opaque white index 1. MSB-first rows.
 * 8 palette bytes + 128 pixel bytes = 136 bytes.
 */
#include "sofle_union_logo.h"

static const uint8_t sofle_union_logo_map[] = {{
{chr(10).join(lines)}
}};

_Static_assert(sizeof(sofle_union_logo_map) == 136, "Invalid indexed image size");

const lv_img_dsc_t sofle_union_logo = {{
    .header.cf = LV_IMG_CF_INDEXED_1BIT,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 32,
    .header.h = 32,
    .data_size = sizeof(sofle_union_logo_map),
    .data = sofle_union_logo_map,
}};
'''
    (root/'src/sofle_union_logo.h').write_text(header)
    (root/'src/sofle_union_logo.c').write_text(source)
    image.save(root/'docs/sofle-union-logo.png')
    image.resize((512, 512), Image.Resampling.NEAREST).save(root/'docs/sofle-union-logo-preview.png')
    print(json.dumps({'size': list(image.size), 'viewBox': box, 'margin': args.margin,
                      'commands': commands, 'flattened_vertices': vertices,
                      'palette_bytes': 8, 'pixel_bytes': len(data)-8, 'total_bytes': len(data),
                      'pixel_bbox': image.getbbox(), 'white_pixels': sum(byte.bit_count() for byte in image.tobytes()),
                      'input_sha256': digest, 'asset_sha256': hashlib.sha256(data).hexdigest(),
                      'msb_round_trip': True}, indent=2))


if __name__ == '__main__':
    main()
