#!/usr/bin/env python3
"""knee.py FULL.f32 MUSIC.f32 EFFECTS.f32 GAIN0 GAIN [GAIN ...] -- what the
soft knee (src/jrplay.c) does to the music and the effects at each gain.

The three files are test/jr/jrlists --samples-out of the same play at
GAIN0: the whole mix, then --parts 1,0 (the music alone) and --parts 0,1
(the effects alone). The mix is linear up to the knee, so at another gain
each is scaled. For each gain: the samples that go into the knee and past
full scale, and how much the knee takes from each part (y, the knee's
output, fitted as a * music + b * effects: 0 dB is untouched).
"""
import array
import math
import sys

K = 0.75     # JRP_KNEE


def load(p):
    a = array.array("f")
    with open(p, "rb") as f:
        a.frombytes(f.read())
    return a


def knee(x):
    ax = min(abs(x), 4.0)
    y = ax if ax <= K else K + (1 - K) * (1 - math.exp(-(ax - K) / (1 - K)))
    return -y if x < 0 else y


def main():
    if len(sys.argv) < 6:
        sys.exit(__doc__)
    full, mus, eff = (load(p) for p in sys.argv[1:4])
    g0 = float(sys.argv[4])
    n = min(len(full), len(mus), len(eff))
    worst = max(abs(full[i] - mus[i] - eff[i]) for i in range(n))
    print(f"{n} samples; the mix less its two parts: at most {worst:.2e} of full scale")
    rm = math.sqrt(sum(v * v for v in mus[:n]) / n)
    re_ = math.sqrt(sum(v * v for v in eff[:n]) / n)
    print(f"at gain {g0:g}: music rms {20 * math.log10(rm):.1f}, effects rms {20 * math.log10(re_):.1f} dB of full scale")
    for g in (float(v) for v in sys.argv[5:]):
        s = g / g0
        kneed = over = 0
        smm = see = sme = smy = sey = 0.0
        sx = sy = 0.0
        for i in range(n):
            x = full[i] * s
            y = knee(x)
            if abs(x) > K:
                kneed += 1
            if abs(x) > 1:
                over += 1
            m, e = mus[i] * s, eff[i] * s
            smm += m * m; see += e * e; sme += m * e; smy += m * y; sey += e * y
            sx += x * x; sy += y * y
        det = smm * see - sme * sme
        a = (smy * see - sey * sme) / det
        b = (sey * smm - smy * sme) / det
        print(f"gain {g:5.2f}: {100 * kneed / n:5.2f}% of samples into the knee, {100 * over / n:5.2f}% past full scale; "
              f"rms {10 * math.log10(sy / sx):+.2f} dB through it; music {20 * math.log10(a):+.2f} dB, effects {20 * math.log10(b):+.2f} dB")


if __name__ == "__main__":
    main()
