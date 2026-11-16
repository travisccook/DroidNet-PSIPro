#!/usr/bin/env bash
# Upstream PSIPro v1.7 and the fork, on the same host mocks, through the same timelines.
# usage: diff_upstream.sh <tmp-dir> <c++>        STRICT=1 treats every row as `same`
#
# row <same|diff> <name> <jumper> <ee0> <ee1> <ee2> <pot> <seed> <dur_ms> [<t_ms> <cmd>]...
#   same  the visible frame sequence is identical (repeated shows of an unchanged frame collapsed)
#   diff  a known difference, with its reason on the line above the row; it must STAY different,
#         so a row that starts matching is reported too
set -u
cd "$(dirname "$0")"
T="$1"; CXX="$2"; bad=0; n=0
F="-std=gnu++17 -O1 -fsigned-char -w -I . -I native_mocks -I ../../include"
"$CXX" $F -DUPSTREAM -I upstream-v1.7 diff_upstream.cpp -o "$T/diff_up" || exit 1
# The fork twice, as its two environments are built: the default (serial-only, with the contract
# layer), and PSIPro-i2c (the I2C intake, no contract layer). Every row holds for both.
"$CXX" $F '-DWDP_FW="x"' -DPSI_NO_STRIP_LOOKS diff_upstream.cpp -o "$T/diff_fork" || exit 1
"$CXX" $F '-DWDP_FW="x"' -DPSI_NO_STRIP_LOOKS -DPSI_ENABLE_I2C -DPSI_NO_CONTRACT diff_upstream.cpp -o "$T/diff_fork_i2c" || exit 1
row() {
  want=$1; name=$2; shift 2; n=$((n + 1))
  [ "${STRICT:-0}" = 1 ] && want=same
  "$T/diff_up" "$T/up.txt" "$@" 2>/dev/null
  "$T/diff_fork" "$T/fork.txt" "$@" 2>/dev/null
  "$T/diff_fork_i2c" "$T/fork_i2c.txt" "$@" 2>/dev/null
  for f in up fork fork_i2c; do awk '{k=$2" "$3; if (k!=p) print; p=k}' "$T/$f.txt" > "$T/n_$f.txt"; done
  for f in fork fork_i2c; do
    if cmp -s "$T/n_up.txt" "$T/n_$f.txt"; then got=same; else got=diff; fi
    [ "$got" = "$want" ] || { echo "FAIL: '$name' is $got on $f, expected $want"; bad=1; }
  done
}
D="front 00 01 14 512 1"        # front jumper, alwaysOn=0, internal brightness 20, pot 512, seed 1
A="front 01 01 14 512 1"        # as D with alwaysOn=1

row same "0T6 from boot" $D 40000 1 0T6
row same "swipe 60 s" $D 60000 1 0T1
row same "swipe rear seed 7" rear 00 01 14 512 7 60000 1 0T1

# |timing on every mode, command at 2000 ms
for m in 2 3 4 5 6 7 8 9 10 11 12 13 14 15 20 21 92; do row same "0T$m|3" $D 12000 2000 "0T$m|3"; done
# 0T19|n: upstream's comment says the battle ignores a timing, but its code aborts the battle at
# once (allOFF's stale timer). The fork does what the comment says and plays the battle whole
# (the timeline scenario the_saber_ignores_a_command_timing), so this row differs from upstream
# by decision.
row diff "0T19|3" $D 12000 2000 "0T19|3"

# mode to mode
for pair in "6 15" "2 19" "13 12" "8 8" "15 6" "19 20" "20 4" "7 9" "11 2" "12 21" "21 12" "4 8" "14 10" \
            "10 0" "0 7" "15 15" "6 6" "3 16" "16 13"; do
  set -- $pair; row same "0T$1 @1000 -> 0T$2 @2300" $D 30000 1000 "0T$1" 2300 "0T$2"
done
# the rebel symbol shows on entry, as upstream's does, whatever delay the previous mode left running
row same "0T9 @1000 -> 0T14 @2300" $D 30000 1000 0T9 2300 0T14
row same "0T11 @1000 -> 0T14 @2300" $D 30000 1000 0T11 2300 0T14

row same "0T6 -> 0D -> 0T6" $D 12000 1000 0T6 2300 0D 2500 0T6
row same "0T15 -> 0A -> 0T10" $D 20000 1000 0T15 1800 0A 4000 0T10

# alwaysOn=1
for m in 2 6 7 8 9 10 11 12 14 15 19 20 21; do row same "alwaysOn 0T$m" $A 60000 1500 "0T$m"; done
for m in 15 10 8 7 12 6; do row same "alwaysOn 0T$m, 0P0" $A 90000 1500 "0T$m" 40000 0P0; done
# short circuit under alwaysOn: upstream's 8-bit loop counter wraps and re-triggers its thresholds,
# the bytecode repeats its 36 ticks. Both are nearly dark by then (2 pixels). Kept.
row diff "alwaysOn 0T4" $A 60000 1500 0T4
row diff "alwaysOn 0T4, 0P0" $A 90000 1500 0T4 40000 0P0

for m in 4 9 12 13 21; do row same "rear 0T$m" rear 00 01 14 512 99 15000 2000 "0T$m"; done
row same "0T6, 3P80, 3P0" $D 12000 1000 0T6 3000 3P80 6000 3P0
row same "pot path 0T15" front 00 00 14 300 1 12000 1000 0T15

# the timelines golden_matrix.txt captures as sequences: what is captured is upstream's behaviour
row same "seq_leia_knight" $D 12000 1000 0T6 2300 0T15
row same "seq_march_d_march" $D 8000 1000 0T11 2300 0D 2500 0T11
row same "seq_disco_p0" $A 12000 1500 0T13 6000 0P0
row same "timing_leia_3s" $D 9000 2000 "0T6|3"
row same "timing_knight_rear" rear 00 01 14 512 1 9000 2000 "0T15|3"
row same "timing_vu_3s" $D 9000 2000 "0T21|3"

[ $bad = 0 ] && echo "upstream differential: $n timelines on each build, all as expected"
exit $bad
