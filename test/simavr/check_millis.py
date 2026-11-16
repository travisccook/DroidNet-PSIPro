#!/usr/bin/env python3
"""Gate the millis() rate the simavr firmware probe (fwprobe) measured.

usage: check_millis.py <log> --limit-ppm N [--report-only]
The log holds one line per look: "<name>: millis=<ms> true_us=<us> err_ppm=<n>".
"""
import re
import sys

limit = int(sys.argv[sys.argv.index("--limit-ppm") + 1])
report_only = "--report-only" in sys.argv
text = open(sys.argv[1]).read()
rows = re.findall(r"^(\S[^:\n]*): millis=\d+ true_us=\d+ err_ppm=(-?\d+)", text, re.M)
rows = [(name, int(err)) for name, err in rows if name != "warmup"]   # warmup precedes Timer1 settling
if len(rows) != 13:
    sys.exit("FAIL: the probe reported %d looks, expected 13" % len(rows))
bad = [r for r in rows if abs(r[1]) > limit]
for name, err in rows:
    print("    %-22s %+7d ppm%s" % (name, err, "   <-- over" if abs(err) > limit else ""))
if bad and not report_only:
    sys.exit("FAIL: millis() is off by more than %d ppm on %d of 13 looks" % (limit, len(bad)))
print("    millis() rate: %s" % ("over the limit, REPORTED ONLY (review H1)" if bad else "within %d ppm" % limit))
