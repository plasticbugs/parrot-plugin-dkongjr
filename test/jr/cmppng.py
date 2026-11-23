#!/usr/bin/env python3
# cmppng.py A.png B.png -- how many pixels differ between two pictures of
# the same size (8-bit RGB or RGBA PNGs, as jrhost and MAME write them)
import struct, sys, zlib

def load(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', path
    pos, idat, w = 8, b'', 0
    while pos < len(d):
        n, kind = struct.unpack('>I4s', d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            assert depth == 8 and ctype in (2, 6), (path, depth, ctype)
            bpp = 3 if ctype == 2 else 4
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(w)])
        prev = line
    return w, h, rows

if __name__ == '__main__':
    wa, ha, a = load(sys.argv[1])
    wb, hb, b = load(sys.argv[2])
    if (wa, ha) != (wb, hb):
        print(f"sizes differ: {wa}x{ha} and {wb}x{hb}"); sys.exit(1)
    diff = [(x, y) for y in range(ha) for x in range(wa) if a[y][x] != b[y][x]]
    print(f"{len(diff)} of {wa * ha} pixels differ" + (f", first at {diff[0]}" if diff else ""))
    sys.exit(1 if diff else 0)
