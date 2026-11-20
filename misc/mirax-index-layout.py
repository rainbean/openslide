#!/usr/bin/env python3
#
# Dump the hierarchical record table of a MIRAX slide's index file.
#
# The table holds one pointer per combination of hierarchy values, laid out
# as a mixed-radix array with HIER_0 varying fastest (see
# docs/adr/0001-mirax-index-layout.md).  This prints the layout, the stride
# of each hierarchy, and every record that holds images, decoded into
# hierarchy values.
#
# usage: mirax-index-layout.py <slide-dir | slide.mrxs.zip> [--all]
#   slide-dir is the folder holding Slidedat.ini and the index file.
#   A zip must hold that folder.  --all lists every non-empty record;
#   by default only records at zoom level 0 are listed.
#
# Read-only.  Run with `python3 -I`.

import configparser
import math
import struct
import sys
import zipfile

INDEX_VERSION = b'01.02'


class CaseConfig(configparser.RawConfigParser):
    def optionxform(self, optionstr):
        return optionstr  # Slidedat keys are case-sensitive


def load(src):
    def parse(text):
        cp = CaseConfig(strict=False, interpolation=None)
        cp.read_string(text)
        return cp

    if src.endswith('.zip'):
        z = zipfile.ZipFile(src)
        sd = next(n for n in z.namelist() if n.endswith('/Slidedat.ini'))
        base = sd[: -len('Slidedat.ini')]
        cp = parse(z.read(sd).decode('utf-8-sig', 'replace'))
        index = z.read(base + cp['HIERARCHICAL']['INDEXFILE'])
    else:
        with open(f'{src}/Slidedat.ini', encoding='utf-8-sig', errors='replace') as f:
            cp = parse(f.read())
        with open(f"{src}/{cp['HIERARCHICAL']['INDEXFILE']}", 'rb') as f:
            index = f.read()
    return cp, index


def i32(buf, off):
    if off < 0 or off + 4 > len(buf):
        return None
    return struct.unpack_from('<i', buf, off)[0]


def record_images(index, rec):
    """Image count of the data pages a record points to, or None if empty."""
    if not rec or i32(index, rec) != 0:
        return None
    page = i32(index, rec + 4)
    total = 0
    seen = set()
    while page and page not in seen:
        seen.add(page)
        count = i32(index, page)
        if count is None:
            return None
        total += count
        page = i32(index, page + 4)
    return total or None


def main():
    if len(sys.argv) < 2:
        sys.exit('usage: mirax-index-layout.py <slide-dir | slide.mrxs.zip> [--all]')
    show_all = '--all' in sys.argv[2:]
    cp, index = load(sys.argv[1])
    gen = cp['GENERAL']
    h = cp['HIERARCHICAL']

    hiers = []
    for i in range(int(h['HIER_COUNT'])):
        count = int(h[f'HIER_{i}_COUNT'])
        values = [h.get(f'HIER_{i}_VAL_{v}', '?') for v in range(count)]
        hiers.append((h[f'HIER_{i}_NAME'], count, values))
    strides = [math.prod(c for _, c, _ in hiers[:i]) for i in range(len(hiers))]
    records = math.prod(c for _, c, _ in hiers)

    if index[: len(INDEX_VERSION)] != INDEX_VERSION:
        sys.exit(f'unexpected index version {index[:5]!r}')
    header = len(INDEX_VERSION) + len(gen['SLIDE_ID'])
    table = i32(index, header)
    if table is None:
        sys.exit('index file too short')
    print(f"slide version {gen.get('CURRENT_SLIDE_VERSION', '(none)')}")
    print(f'record table at {table}, {records} records')
    for i, ((name, count, _), stride) in enumerate(zip(hiers, strides)):
        print(f'  HIER_{i} {name!r:28} count {count:3}  stride {stride:5} ({4 * stride} B)')

    zoom = next(i for i, (n, _, _) in enumerate(hiers) if n == 'Slide zoom level')
    nonempty = 0
    for rec in range(records):
        images = record_images(index, i32(index, table + 4 * rec))
        if not images:
            continue
        nonempty += 1
        digits = [(rec // s) % c for s, (_, c, _) in zip(strides, hiers)]
        if not show_all and digits[zoom] != 0:
            continue
        coords = ', '.join(
            f'{hiers[i][2][d]}' for i, d in enumerate(digits) if i != zoom
        )
        print(f'  record {rec:5}  zoom {digits[zoom]}  [{coords}]  {images} images')
    print(f'{nonempty} non-empty records')


if __name__ == '__main__':
    main()
