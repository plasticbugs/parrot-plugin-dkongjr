#!/usr/bin/env python3
"""level.py ARCADE.wav PLUGIN.wav -- how loud the plugin is, measured
fairly against MAME's recording of the arcade (test/jr/check.sh makes both).

tools/wavlevel.py counts everything in a second as sound. MAME's recording
of the console carries a tone at 16-18 kHz (the DAC's three tones sit at
period 1, and MAME's resampling folds them down) at about -40 dBFS, and
the DAC's resting level drifts back slowly (a few Hz) after a loud sound,
so seconds the game is silent count as sound and pull the plugin's rms
down. Here each second's level is its power from 20 Hz to 15 kHz only (an
FFT of the second through a Hann window, the bins in that band), and the
plugin is measured over the seconds where the arcade has sound, after
lining the two up by their envelopes.

Prints, in dBFS: each file's rms over those seconds (as wavlevel.py's rms:
the power mean of the seconds with sound), the same with everything, and
the offset found. Pure Python: about ten seconds.
"""
import cmath
import math
import struct
import sys
import wave

BAND = (20.0, 15000.0)
SILENT_DB = -60.0       # as tools/wavlevel.py


def load(path):
    with wave.open(path, "rb") as w:
        ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        if w.getsampwidth() != 2:
            sys.exit(f"{path}: not 16-bit PCM")
        v = struct.unpack(f"<{n * ch}h", w.readframes(n))
    if ch > 1:
        v = [sum(v[i:i + ch]) / ch for i in range(0, len(v), ch)]
    return rate, v


def fft(x):
    """in place, radix 2, len(x) a power of 2"""
    n = len(x)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            x[i], x[j] = x[j], x[i]
    size = 2
    while size <= n:
        half = size // 2
        w = [cmath.exp(-2j * math.pi * k / size) for k in range(half)]
        for start in range(0, n, size):
            for k in range(half):
                a = x[start + k]
                b = x[start + k + half] * w[k]
                x[start + k] = a + b
                x[start + k + half] = a - b
        size *= 2
    return x


_hann = {}
HANN_MS = 0.375     # a Hann window's mean square


def hann(n):
    if n not in _hann:
        _hann[n] = [0.5 - 0.5 * math.cos(2 * math.pi * (i + 0.5) / n) for i in range(n)]
    return _hann[n]


def band_db(a, b, rate):
    """two seconds at once (a real, b imaginary): each one's level in BAND, dBFS"""
    n = 1
    while n < len(a):
        n *= 2
    # a Hann window, so what's below 20 Hz (a resting level drifting back
    # after a sound) doesn't leak into the band
    w = hann(len(a))
    x = [complex(a[i] * w[i], b[i] * w[i]) for i in range(len(a))] + [0j] * (n - len(a))
    X = fft(x)
    lo, hi = int(BAND[0] * n / rate) + 1, int(BAND[1] * n / rate)
    pa = pb = 0.0
    for k in range(lo, hi + 1):
        xk, xm = X[k], X[n - k].conjugate()
        pa += abs(xk + xm) ** 2 / 4
        pb += abs(xk - xm) ** 2 / 4
    # Parseval: both sides of the spectrum, over the second's samples, and
    # the window's mean square
    def db(p):
        ms = 2 * p / n / len(a) / HANN_MS
        return 10 * math.log10(ms / 32768 ** 2) if ms > 0 else -200.0
    return db(pa), db(pb)


def plain_db(x):
    m = sum(x) / len(x)
    ms = sum((v - m) ** 2 for v in x) / len(x)
    return 10 * math.log10(ms / 32768 ** 2) if ms > 0 else -200.0


def envelope(x, rate, ms=10):
    n = int(rate * ms / 1000)
    out = []
    for i in range(0, len(x) - n + 1, n):
        w = x[i:i + n]
        m = sum(w) / n
        out.append(math.sqrt(sum((v - m) ** 2 for v in w) / n))
    return out, n


def offset(a, b, rate, most_s=3.0):
    """b's samples later than a's (or earlier, negative), by envelope correlation"""
    ea, step = envelope(a[:rate * 40], rate)
    eb, _ = envelope(b[:rate * 40], rate)
    best = (-2.0, 0)
    span = int(most_s * 1000 / 10)
    for off in range(-span, span + 1):
        pairs = [(ea[i], eb[i + off]) for i in range(len(ea)) if 0 <= i + off < len(eb)]
        if len(pairs) < 500:
            continue
        ma = sum(p[0] for p in pairs) / len(pairs)
        mb = sum(p[1] for p in pairs) / len(pairs)
        sab = sum((p[0] - ma) * (p[1] - mb) for p in pairs)
        saa = sum((p[0] - ma) ** 2 for p in pairs)
        sbb = sum((p[1] - mb) ** 2 for p in pairs)
        c = sab / math.sqrt(saa * sbb) if saa and sbb else 0
        if c > best[0]:
            best = (c, off)
    return best[1] * step, best[0]


def power_mean(dbs):
    return 10 * math.log10(sum(10 ** (d / 10) for d in dbs) / len(dbs)) if dbs else float("nan")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    ra, a = load(sys.argv[1])
    rb, b = load(sys.argv[2])
    if ra != rb:
        sys.exit("the two files' rates differ")
    off, corr = offset(a, b, ra)
    print(f"the plugin's recording is {off / ra:+.3f} s from the arcade's (envelopes correlate {corr:.3f})")
    secs = []
    for s in range(len(a) // ra):
        i, j = s * ra, s * ra + off
        if j < 0 or j + ra > len(b):
            continue
        da, db_ = band_db(a[i:i + ra], b[j:j + ra], ra)
        secs.append((s, da, db_, plain_db(a[i:i + ra]), plain_db(b[j:j + ra])))
    loud = [t for t in secs if t[1] > SILENT_DB]
    print(f"{len(secs)} seconds overlap; the arcade has sound in {len(loud)} of them")
    print(f"  20 Hz-15 kHz:  arcade rms {power_mean([t[1] for t in loud]):6.1f}   plugin rms {power_mean([t[2] for t in loud]):6.1f} dBFS")
    print(f"  everything:    arcade rms {power_mean([t[3] for t in loud]):6.1f}   plugin rms {power_mean([t[4] for t in loud]):6.1f} dBFS")
    quiet = [t for t in secs if t[1] <= SILENT_DB]
    if quiet:
        print(f"  the arcade's silent seconds, in the plugin: {power_mean([t[2] for t in quiet]):.1f} dBFS in the band, "
              f"{power_mean([t[4] for t in quiet]):.1f} with everything")


if __name__ == "__main__":
    main()
