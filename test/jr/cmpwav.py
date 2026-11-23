#!/usr/bin/env python3
# cmpwav.py MODEL.wav MAME.wav [from_s to_s] -- the model's sound against
# MAME's -wavwrite of the same play: the level of each second (dBFS) and how
# alike the two are (correlation at the best lag within 5 ms)
import array, math, sys, wave

def load(path):
    w = wave.open(path, 'rb')
    ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
    a = array.array('h', w.readframes(n))
    if ch == 2: a = array.array('h', a[0::2])
    return rate, a

ra, a = load(sys.argv[1])
rb, b = load(sys.argv[2])
assert ra == rb, (ra, rb)
lo = int(float(sys.argv[3]) * ra) if len(sys.argv) > 3 else 0
hi = int(float(sys.argv[4]) * ra) if len(sys.argv) > 4 else min(len(a), len(b))
def db(x): return 20 * math.log10(x / 32768) if x > 0 else -99
print(" s   model  mame   corr")
for s0 in range(lo, hi - ra + 1, ra):
    xa, xb = a[s0:s0 + ra], b[s0:s0 + ra]
    rms_a = math.sqrt(sum(v * v for v in xa) / ra)
    rms_b = math.sqrt(sum(v * v for v in xb) / ra)
    best = 0
    if rms_a > 30 and rms_b > 30:
        ma, mb = sum(xa) / ra, sum(xb) / ra
        for lag in range(-240, 241, 4):
            num = sa = sb = 0.0
            for i in range(max(0, -lag), min(ra, ra - lag), 3):
                p, q = xa[i] - ma, xb[i + lag] - mb
                num += p * q; sa += p * p; sb += q * q
            c = num / math.sqrt(sa * sb) if sa and sb else 0
            best = max(best, c)
    print(f"{s0 // ra:3d} {db(rms_a):6.1f} {db(rms_b):6.1f}  {best:5.2f}")
