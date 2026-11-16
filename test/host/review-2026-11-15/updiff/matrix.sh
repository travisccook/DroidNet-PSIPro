#!/usr/bin/env bash
# The scenarios the 2026-11-15 review ran. usage: BIN=<build.sh out-dir> matrix.sh
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
run() { "$here/rundiff.sh" "$@"; }
D="front 00 01 14 512 1"        # front jumper, alwaysOn=0, internal brightness 20, pot 512, seed 1
A="front 01 01 14 512 1"        # as D with alwaysOn=1
echo "--- sanity"
run "0T6 from boot" $D 40000 1 0T6
run "swipe 60 s" $D 60000 1 0T1
run "swipe rear seed 7" rear 00 01 14 512 7 60000 1 0T1
echo "--- |timing on every mode (command at 2000 ms)"
for m in 2 3 4 5 6 7 8 9 10 11 12 13 14 15 19 20 21 92; do run "0T$m|3" $D 12000 2000 "0T$m|3"; done
echo "--- mode to mode"
for pair in "6 15" "2 19" "13 12" "8 8" "15 6" "19 20" "20 4" "7 9" "11 2" "12 21" "21 12" "4 8" "9 14" "14 10" \
            "10 0" "0 7" "15 15" "6 6" "3 16" "16 13"; do
  set -- $pair; run "0T$1 @1000 -> 0T$2 @2300" $D 30000 1000 "0T$1" 2300 "0T$2"
done
echo "--- interrupted by D / A"
run "0T6 -> 0D -> 0T6" $D 12000 1000 0T6 2300 0D 2500 0T6
run "0T15 -> 0A -> 0T10" $D 20000 1000 0T15 1800 0A 4000 0T10
echo "--- alwaysOn=1, 60 s"
for m in 2 4 6 7 8 9 10 11 12 14 15 19 20 21; do run "alwaysOn 0T$m" $A 60000 1500 "0T$m"; done
echo "--- alwaysOn=1, then 0P0 at 40 s"
for m in 15 10 8 7 4 12 6; do run "alwaysOn 0T$m, 0P0" $A 90000 1500 "0T$m" 40000 0P0; done
echo "--- rear jumper, seed 99"
for m in 4 9 12 13 21; do run "rear 0T$m" rear 00 01 14 512 99 15000 2000 "0T$m"; done
echo "--- brightness"
run "0T6, 3P80, 3P0" $D 12000 1000 0T6 3000 3P80 6000 3P0
run "pot path 0T15" front 00 00 14 300 1 12000 1000 0T15
