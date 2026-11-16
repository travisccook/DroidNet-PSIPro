#!/usr/bin/env python3
"""Gate what the simavr lifecycle probe saw on the panel at each checkpoint.

usage: check_lifecycle.py <log>
The log holds one line per checkpoint:
"cp<n> t=<ms> lit=<n> green=<n> white=<n> code=<lastPSIeventCode> run=<0|1>".
"""
import re
import sys

# What must be on the panel at each checkpoint (test/simavr/lifecycle/src/main.cpp names the rows).
EXPECT = [
    ("swipe", "boot: the idle swipe"),
    ("swipe", "M:v=show arms and keeps the idle look"),
    ("green", "the live look"),
    ("dark", "X blacks out and stays dark"),
    ("swipe", "2 s after M:v=idle: the swipe is back and stayed"),
    ("swipe", "armed again"),
    ("knight", "the show's native:15"),
    ("swipe", "2 s after a native 0T1 took the panel back"),
    ("white", "a pulse over the native look"),
    ("swipe", "the native look returns after the pulse"),
    ("swipe", "armed a third time"),
    ("green", "the live look"),
    ("swipe", "2 s after M:v=idle: the swipe again"),
    ("swipe", "armed with wd=2"),
    ("green", "1.3 s of host silence: the look holds"),
    ("swipe", "2.8 s of host silence: the watchdog gave the panel back"),
    ("swipe", "armed, one section loaded"),
    ("swipe", "loaded, before Play"),
    ("green", "the scored section plays"),
    ("swipe", "2 s after a V that does not match: back to the native look"),
]
KINDS = {
    "swipe": lambda c: c["lit"] == 48 and c["green"] == 0 and c["white"] == 0 and c["code"] == 1,
    "green": lambda c: c["green"] == 48,
    "white": lambda c: c["white"] == 48,
    "dark": lambda c: c["lit"] == 0,
    "knight": lambda c: 0 < c["lit"] <= 12,
}

text = open(sys.argv[1]).read()
rows = re.findall(r"^cp(\d+) t=(\d+) lit=(\d+) green=(\d+) white=(\d+) code=(-?\d+) run=(\d)", text, re.M)
if len(rows) != len(EXPECT) or "lifecycle done" not in text:
    sys.exit("FAIL: the probe reported %d checkpoints, expected %d" % (len(rows), len(EXPECT)))
bad = 0
for row, (kind, why) in zip(rows, EXPECT):
    c = dict(zip(("n", "t", "lit", "green", "white", "code", "run"), map(int, row)))
    ok = KINDS[kind](c)
    bad += not ok
    print("    cp%-2d t=%-6d %-6s lit=%-2d code=%-2d run=%d  %s%s"
          % (c["n"], c["t"], kind, c["lit"], c["code"], c["run"], why, "" if ok else "   <-- NOT " + kind))
if bad:
    sys.exit("FAIL: %d of %d lifecycle checkpoints are wrong on the simulated board" % (bad, len(EXPECT)))
print("    lifecycle: %d checkpoints as expected" % len(EXPECT))
