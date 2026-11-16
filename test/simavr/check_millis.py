#!/usr/bin/env python3
"""Gate the millis() rate the simavr firmware probe (fwprobe) measured.

usage: check_millis.py <log> --limit-ppm N [--max-pass-us N] [--looks N] [--report-only]
The log holds one line per look: "<name>: millis=<ms> true_us=<us> max_pass_us=<us> err_ppm=<n>".
--max-pass-us gates the longest single pass of loop() in any look (contract v1.3 section 11: a
pass must end well inside the 66 ms a 64-byte RX ring holds of a gapless 9600-baud burst).
"""
import re
import sys

limit = int(sys.argv[sys.argv.index("--limit-ppm") + 1])
report_only = "--report-only" in sys.argv
text = open(sys.argv[1]).read()
max_pass = int(sys.argv[sys.argv.index("--max-pass-us") + 1]) if "--max-pass-us" in sys.argv else None
rows = re.findall(r"^(\S[^:\n]*): millis=\d+ true_us=\d+ max_pass_us=(\d+) err_ppm=(-?\d+)", text, re.M)
passes = [(name, int(us)) for name, us, err in rows if name != "warmup"]
rows = [(name, int(err)) for name, us, err in rows if name != "warmup"]   # warmup precedes Timer1 settling
looks = int(sys.argv[sys.argv.index("--looks") + 1]) if "--looks" in sys.argv else 13
if len(rows) != looks:
    sys.exit("FAIL: the probe reported %d looks, expected %d" % (len(rows), looks))
bad = [r for r in rows if abs(r[1]) > limit]
for name, err in rows:
    print("    %-22s %+7d ppm%s" % (name, err, "   <-- over" if abs(err) > limit else ""))
if bad and not report_only:
    sys.exit("FAIL: millis() is off by more than %d ppm on %d of %d looks" % (limit, len(bad), looks))
print("    millis() rate: %s" % ("over the limit, REPORTED ONLY (review H1)" if bad else "within %d ppm" % limit))
if max_pass is not None:
    name, us = max(passes, key=lambda r: r[1])
    print("    longest loop() pass: %d us (%s); the limit is %d" % (us, name, max_pass))
    if us > max_pass:
        sys.exit("FAIL: a pass of loop() took %d us in %s: a gapless burst could overrun the RX ring" % (us, name))
