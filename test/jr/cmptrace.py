#!/usr/bin/env python3
# cmptrace.py MODEL.trace MAME.trace -- the first instruction where the model's
# Z80 trace (jrhost --trace) and MAME's (trace.lua) part: by cycle count,
# PC and registers, lined up from the first cycle count both have.
import re, sys
def load(path):
    out = []
    for line in open(path):
        m = re.match(r"C=(\d+) AF=(\w+) BC=(\w+) DE=(\w+) HL=(\w+) IX=(\w+) IY=(\w+) SP=(\w+) (\w{4})", line)
        if m: out.append((int(m.group(1)), m.group(9).upper(), tuple(m.group(i) for i in range(2, 9)), line.rstrip()))
    return out
a, b = load(sys.argv[1]), load(sys.argv[2])
start = max(a[0][0], b[0][0])
ia = next(i for i, r in enumerate(a) if r[0] >= start)
ib = next(i for i, r in enumerate(b) if r[0] >= start)
while a[ia][0] != b[ib][0]:
    if a[ia][0] < b[ib][0]: ia += 1
    else: ib += 1
n = 0
while ia < len(a) and ib < len(b):
    if a[ia][:3] != b[ib][:3]:
        print(f"parted after {n} instructions:")
        for k in range(-4, 2):
            if 0 <= ia + k < len(a): print("  model", a[ia + k][3])
        for k in range(-4, 2):
            if 0 <= ib + k < len(b): print("  mame ", b[ib + k][3])
        sys.exit(1)
    ia += 1; ib += 1; n += 1
print(f"the same for {n} instructions")
