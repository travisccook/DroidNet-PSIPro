#!/usr/bin/env bash
# Upstream PSIPro v1.7 and the fork, on the same host mocks, through the same timelines.
# usage: diff_upstream.sh <tmp-dir> <c++>        STRICT=1 treats every row as `same`
#
# row <same|diff> <name> <jumper> <ee0> <ee1> <ee2> <pot> <seed> <dur_ms> [<t_ms> <cmd>]...
#   same  the frames are identical: every show(), with its millisecond, its scale and its pixels
#         (repeated shows of an unchanged frame included: the swipe's redraws are upstream's too)
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
  for f in fork fork_i2c; do
    if cmp -s "$T/up.txt" "$T/$f.txt"; then got=same; else got=diff; fi
    [ "$got" = "$want" ] || { echo "FAIL: '$name' is $got on $f, expected $want"; bad=1; }
  done
}
# rowi: a row with an I2C line that lands inside a show() ("<t> I2C:<cmd>"), which only upstream and
# the fork's I2C build can take.
rowi() {
  want=$1; name=$2; shift 2; n=$((n + 1))
  "$T/diff_up" "$T/up.txt" "$@" 2>/dev/null
  "$T/diff_fork_i2c" "$T/fork_i2c.txt" "$@" 2>/dev/null
  if cmp -s "$T/up.txt" "$T/fork_i2c.txt"; then got=same; else got=diff; fi
  [ "$got" = "$want" ] || { echo "FAIL: '$name' is $got on fork_i2c, expected $want"; bad=1; }
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

# Review finding 7: three places where the fork was not upstream, none of them listed.
# (1) Upstream's mode functions clear a flag its swipe shares (updateLed), and the swipe, back
# after a mode, redraws 100 ms after its last frame or at its next change of colour: the fork's
# bytecode modes left the flag set, and the swipe was back up to 90 ms sooner (and followed the
# brightness pot sooner) when a D, an A or 0T1 came just after a mode command.
row same "0T2 @1000, 0D @1030" front 00 01 14 512 1 9000 1000 0T2 1030 0D
row same "0T6 @1000, 0A @1050" front 00 01 14 512 1 9000 1000 0T6 1050 0A
row same "0T15 @1000, 0T1 @1040" front 00 01 14 512 1 9000 1000 0T15 1040 0T1
row same "pot path, 0T5|2 @324, 0D5 @354" rear 00 00 05 100 3738 20000 324 "0T5|2" 354 0D5
# ...and the flag is set by a step that DRAWS, not by every step: radar's last call is the blank
# after its last quadrant and the battle's is its end, upstream leaves the flag clear on both, and
# when the swipe's next change of colour is still to come its redraws are 104 ms apart (the fork's
# were 26 ms apart: second review, N3). The seeds and times are ones where that change is late.
for st in "2 1200" "5 300" "6 2600" "3 2600"; do
  set -- $st
  row same "0T8 ends by itself, seed $1, sent at $2" front 00 01 14 512 $1 30000 $2 0T8
  row same "0T19 ends by itself, seed $1, sent at $2" rear 00 01 14 512 $1 30000 $2 0T19
done
# ...and the rebel symbol does not touch the flag at all (upstream's displayMatrixColor() never
# does): after the symbol the swipe redraws as the mode BEFORE the symbol left it to.
row same "0T9 @2118, 0T14 @6148 and its 5 s" front 00 01 14 512 2581 30000 2118 4T9 6148 04T14
row same "0T8 @601, 0T14 @4601, 0D @7661" front 01 ff 14 100 2275 20000 601 0T8 4601 0T14 7661 0D
row same "rear 0T9 @1732, 0T14 @3233" rear 00 ff ff 0 2901 20000 1732 0T9 3233 0T14
# (2) The rebel symbol is redrawn every poll. A mode command in the same millisecond as a redraw
# started one poll (26 ms) later than upstream's, behind the 1 ms delay the bytecode set there...
row same "0T14 @2790, 0T11 @2990" front 00 01 14 512 1 9000 2790 0T14 2990 0T11
row same "alwaysOn 0T14 @666, 0T2 @6266" rear 01 ff ff 0 113 20000 666 0T14 6266 0T2
# ...and upstream's symbol never touches the step delay, so the delay the mode BEFORE the symbol
# left running still holds back the mode after it. The fork's started that mode at once.
row same "0T11 @1000, 0T14 @1100, 0T6 @1130" front 00 01 14 512 1 12000 1000 0T11 1100 0T14 1130 0T6
row same "0T15 @1000, 0T14 @1100, 0T19 @1130" front 00 01 14 512 1 12000 1000 0T15 1100 0T14 1130 0T19
row same "0T20 @1000, 0T14 @1100, 0T15 @1200" front 00 01 14 512 1 12000 1000 0T20 1100 0T14 1200 0T15
# (3) A timing that arrives while short circuit or the lightsaber battle runs, from a command that
# starts nothing (the same mode again, or a mode that does not exist): upstream's two functions
# have no timer of their own. The fork's ended short circuit at once, and the battle a step early.
row same "0T4 @2896, 0T4|20 @2926" front 00 01 14 512 1 40000 2896 0T4 2926 "0T4|20"
row same "0T4 @2896, 0T23|2 @2926" front 00 01 14 512 1 40000 2896 0T4 2926 "0T23|2"
row same "0T19 @2896, 0T23|2 @3526" front 00 01 14 512 1 40000 2896 0T19 3526 "0T23|2"
row same "0T19 @2896, 0T19|2 @3000" front 00 01 14 512 1 40000 2896 0T19 3000 "0T19|2"

# An I2C mode command whose interrupt lands inside the running mode's show() (review finding 3).
# Upstream runs the new mode there and then; the interrupted mode function then finishes the step
# it was in (draws the rest of it, shows, sets its delay). The fork's bytecode step does the same
# and leaves the VM to the new mode (it used to store its own place over the new program's, and
# the old mode went on under the new number: 247 of these 441 rows differed then).
# The rows that still differ, and stay so, are upstream's own doing: its interrupted function also
# steps the counters every one of its modes shares (ledPatternState and the like), so the mode the
# command started runs a step ahead or behind, or skips its first frame, or (the flash after Leia,
# Knight Rider or the lightsaber battle) stays lit without flashing for its 4 s; and a command for
# the mode that is already running shifts upstream's next step. The fork's new mode starts at its
# first step. Which pairs depends on where in the old mode the interrupt lands; this is the list
# for the first show at or after 2300 ms.
UPSTREAM_STEPS_ON=" 2>2 3>3 4>4 5>5 6>2 6>3 6>4 6>5 6>6 6>7 6>8 6>9 6>10 6>11 6>12 6>13 6>15 6>19 6>20 7>7 8>8 9>9 \
10>2 10>3 10>4 10>5 10>6 10>7 10>8 10>9 10>10 10>11 10>12 10>13 10>15 10>19 10>20 11>11 12>12 \
13>13 15>2 15>3 15>4 15>5 15>6 15>7 15>8 15>9 15>10 15>11 15>12 15>13 15>15 15>19 15>20 19>2 \
19>3 19>4 19>5 19>6 19>7 19>8 19>9 19>10 19>11 19>12 19>13 19>15 19>20 20>20 "
MODES="2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 92"
for was in $MODES; do for now in $MODES; do
  case "$UPSTREAM_STEPS_ON" in *" $was>$now "*) want=diff ;; *) want=same ;; esac
  rowi $want "0T$was @1000, I2C 0T$now inside a show from 2300" $D 12000 1000 "0T$was" 2300 "I2C:0T$now"
done; done
# ...and commands that start no mode, or the default one
rowi same "0T6, I2C 0D inside a show" $D 12000 1000 0T6 2300 "I2C:0D"
rowi same "0T15, I2C 3P80 inside a show" $D 12000 1000 0T15 2300 "I2C:3P80"
rowi same "0T12, I2C 0T7|3 inside a show" $D 12000 1000 0T12 2300 "I2C:0T7|3"
rowi same "alwaysOn 0T12, I2C 0T21 inside a show" $A 40000 1500 0T12 4000 "I2C:0T21"
rowi same "alwaysOn 0T20, I2C 0T8 inside a show" $A 40000 1500 0T20 4000 "I2C:0T8"
rowi same "rear 0T9, I2C 0T12 inside a show" rear 00 01 14 512 99 15000 2000 0T9 3300 "I2C:0T12"

[ $bad = 0 ] && echo "upstream differential: $n timelines, all as expected (those with no I2C line on each build)"
exit $bad
