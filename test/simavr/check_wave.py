#!/usr/bin/env python3
"""Check a simavr VCD of the WS2812 data pin (PD4) against the device timing and the frame bytes.

usage: check_wave.py wave.vcd [--min-gap-us N]
Expects three identical frames of 48 LEDs: leds[i] = (r=i*5, g=255-i, b=i^0xA5), sent G,R,B.
"""
import sys

T0H, T1H, PERIOD = 250, 750, 1250           # ns, from include/FastLED.h's cycle budget
SPEC_T0H = (200, 500)                       # WS2812 350 +/- 150 ns
SPEC_T1H = (550, 850)                       # WS2812 700 +/- 150 ns
# ns between two frames. The original WS2812 resets after 50 us low; later revisions want 280 us.
LATCH_MIN = 1000 * int(sys.argv[sys.argv.index("--min-gap-us") + 1]) if "--min-gap-us" in sys.argv else 50_000


def edges(path):
    t, out = 0, []
    for line in open(path):
        line = line.strip()
        if line.startswith("#"):
            t = int(line[1:])
        elif line in ("0!", "1!"):
            out.append((t, int(line[0])))
    return out


def main():
    ev = edges(sys.argv[1])
    rises = [t for t, v in ev if v == 1]
    falls = [t for t, v in ev if v == 0 and t > rises[0]]
    highs = [f - r for r, f in zip(rises, falls)]
    bits = [1 if h > 500 else 0 for h in highs]
    gaps = [b - a for a, b in zip(rises, rises[1:])]
    frame_bits = 48 * 24
    fail = []
    if len(bits) != 3 * frame_bits:
        fail.append("expected %d bits in three frames, saw %d" % (3 * frame_bits, len(bits)))
    h0 = sorted(set(h for h, b in zip(highs, bits) if b == 0))
    h1 = sorted(set(h for h, b in zip(highs, bits) if b == 1))
    if h0 != [T0H] or h1 != [T1H]:
        fail.append("high times: 0-bit %s ns, 1-bit %s ns (want %d / %d)" % (h0, h1, T0H, T1H))
    if not (SPEC_T0H[0] <= T0H <= SPEC_T0H[1] and SPEC_T1H[0] <= T1H <= SPEC_T1H[1]):
        fail.append("design constants are outside the WS2812 windows")
    in_frame = [g for i, g in enumerate(gaps) if (i + 1) % frame_bits != 0]
    periods = sorted(set(in_frame))
    if not set(periods) <= {PERIOD, 1437, 1438}:
        fail.append("bit periods %s ns (want 1250 mid-byte, 1437.5 at a byte boundary)" % periods)
    want = []
    for i in range(48):
        want += [(255 - i) & 0xFF, (i * 5) & 0xFF, (i ^ 0xA5) & 0xFF]
    for f in range(len(bits) // frame_bits):
        fb = bits[f * frame_bits:(f + 1) * frame_bits]
        got = [int("".join(map(str, fb[k:k + 8])), 2) for k in range(0, frame_bits, 8)]
        if got != want:
            bad = next(k for k in range(len(want)) if got[k] != want[k])
            fail.append("frame %d byte %d: wire 0x%02x, want 0x%02x" % (f, bad, got[bad], want[bad]))
    frame_ns = falls[frame_bits - 1] - rises[0]
    latches = [rises[k] - falls[k - 1] for k in range(frame_bits, len(rises), frame_bits)]
    print("bits=%d  T0H=%s  T1H=%s  periods=%s  frame=%.1f us  gaps between frames=%s us"
          % (len(bits), h0, h1, periods, frame_ns / 1000.0, [round(g / 1000.0, 1) for g in latches]))
    if latches and min(latches) < LATCH_MIN:
        fail.append("latch gap %d ns is under %d ns" % (min(latches), LATCH_MIN))
    for f in fail:
        print("FAIL:", f)
    sys.exit(1 if fail else 0)


if __name__ == "__main__":
    main()
