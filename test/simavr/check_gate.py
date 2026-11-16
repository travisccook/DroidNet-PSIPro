#!/usr/bin/env python3
"""Count dark-to-lit onsets in the frames the simavr S9 probe put on the LED data pin.

usage: check_gate.py <gate.vcd> <gate.log>
Decodes every WS2812 frame in the VCD (a 1-bit is high for more than 500 ns; a frame ends at a
low of more than 50 us), takes each frame's field level (the mean over the 48 LEDs of the
brightest channel on the wire) and counts onsets as three observers would, each with its own
thresholds for dark and lit, against the owner's brightness the probe set (100).
"""
import sys

CEIL = 100
OBSERVERS = ((10, 50), (19, 75), (4, 31))     # dark at or under, lit at or over: percent of the ceiling
MIN_GAP_MS = 340


def frames(path):
    t, rise, bits, start, last_fall, out = 0, None, [], None, None, []
    for line in open(path):
        line = line.strip()
        if line.startswith("#"):
            t = int(line[1:])
        elif line == "1!":
            if last_fall is not None and t - last_fall > 50_000 and bits:
                out.append((start, bits))
                bits = []
            if not bits:
                start = t
            rise = t
        elif line == "0!" and rise is not None:
            bits.append(1 if t - rise > 500 else 0)
            last_fall = t
    if bits:
        out.append((start, bits))
    return out


def main():
    if "gate done" not in open(sys.argv[2]).read():
        sys.exit("FAIL: the gate probe did not finish its script")
    levels = []
    for start, bits in frames(sys.argv[1]):
        if len(bits) != 48 * 24:
            sys.exit("FAIL: a frame of %d bits at %d ns (want %d)" % (len(bits), start, 48 * 24))
        b = [int("".join(map(str, bits[k:k + 8])), 2) for k in range(0, len(bits), 8)]
        levels.append((start / 1e6, sum(max(b[k:k + 3]) for k in range(0, len(b), 3)) / 48.0))
    if len(levels) < 200:
        sys.exit("FAIL: only %d frames on the wire in 8 s" % len(levels))
    fail = []
    over = [(t, lv) for t, lv in levels if lv > CEIL + 0.5]
    if over:
        fail.append("%d frames above the owner's brightness (first at %.0f ms: %.1f)" % (len(over), over[0][0], over[0][1]))
    for dark, lit in OBSERVERS:
        on, state = [], False
        for t, lv in levels:
            if not state and lv * 100 >= CEIL * lit:
                state = True
                on.append(t)
            elif state and lv * 100 <= CEIL * dark:
                state = False
        gaps = [b - a for a, b in zip(on, on[1:])]
        worst = max((sum(1 for u in on if t - 1000 < u <= t) for t in on), default=0)
        print("    dark <= %2d %%, lit >= %2d %%: %2d onsets in 7 s, most in one second %d, closest two %s ms"
              % (dark, lit, len(on), worst, "%.0f" % min(gaps) if gaps else "-"))
        if len(on) < 6:
            fail.append("observer %d/%d saw %d onsets: the streams are not exercising the gate" % (dark, lit, len(on)))
        if worst > 3 or (gaps and min(gaps) < MIN_GAP_MS - 1):
            fail.append("observer %d/%d: %d onsets in one second, closest two %.0f ms" % (dark, lit, worst, min(gaps)))
    for f in fail:
        print("FAIL:", f)
    if not fail:
        print("    S9 on the wire: %d frames decoded, never more than 3 onsets a second" % len(levels))
    sys.exit(1 if fail else 0)


if __name__ == "__main__":
    main()
