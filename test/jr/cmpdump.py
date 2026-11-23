#!/usr/bin/env python3
# cmpdump.py A/dump.bin B/dump.bin -- the model's dumps against MAME's, frame by frame:
# the first frame where the Z80's RAM differs (and where), then the 8035's.
import sys
REC = 0x1800 + 64 + 6
# The stack's top ($6BC0-$6BFF) is left out: MAME's dump can come a few
# cycles before or after the model's (where its scheduler stops the Z80 for
# the frame), and the last return address pushed is all that shows it.
SKIP = range(0x0BC0, 0x0C00)
# and for the same reason the main loop's counter ($6019) one ahead in the model
COUNTER = 0x19
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
n = min(len(a), len(b)) // REC
print(f"{n} frames to compare")
first_z = first_s = None
zdiff = sdiff = 0
for f in range(n):
    ra, rb = a[f*REC:(f+1)*REC], b[f*REC:(f+1)*REC]
    za = bytearray(ra[:0x1800]); zb = bytearray(rb[:0x1800])
    for i in SKIP: za[i] = zb[i] = 0
    if (za[COUNTER] - zb[COUNTER]) & 255 == 1: za[COUNTER] = zb[COUNTER]
    if za != zb:
        zdiff += 1
        if first_z is None:
            first_z = f
            d = [i for i in range(0x1800) if za[i] != zb[i]]
            print(f"Z80 RAM first differs at frame {f+1}: {len(d)} bytes, e.g. " +
                  ", ".join(f"{0x6000+i:04X}:{za[i]:02X}/{zb[i]:02X}" for i in d[:12]))
    sa, sb = bytearray(ra[0x1800:]), bytearray(rb[0x1800:])
    sa[65] = sb[65] = 0     # MAME's dump has the 8035's SP state there, the model its PSW
    if sa != sb:
        sdiff += 1
        if first_s is None:
            first_s = f
            d = [i for i in range(len(sa)) if sa[i] != sb[i]]
            print(f"8035 first differs at frame {f+1}: " + ", ".join(f"{i}:{sa[i]:02X}/{sb[i]:02X}" for i in d[:16]))
print(f"Z80 RAM differs in {zdiff} of {n} frames, the 8035 in {sdiff}")
sys.exit(1 if zdiff or sdiff else 0)
