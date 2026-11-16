#!/usr/bin/env bash
# Run one timeline through upstream and the fork and compare the frames.
# usage: BIN=<build.sh out-dir> rundiff.sh <name> <jumper> <ee0> <ee1> <ee2> <pot> <seed> <dur_ms> [<t_ms> <cmd>]...
# SAME        every show() call identical (time, scale, 48 pixels)
# SAME-dedup  identical once consecutive re-shows of an unchanged frame are collapsed (what the eye sees)
# DIFF        the visible sequence differs; prints the first differing distinct frame
set -u
: "${BIN:?set BIN to the directory build.sh printed}"
name=$1; shift
"$BIN/diff_up"   "$BIN/out_up.txt"   "$@" 2>/dev/null
"$BIN/diff_fork" "$BIN/out_fork.txt" "$@" 2>/dev/null
for f in up fork; do awk '{k=$2" "$3; if (k!=p) print; p=k}' "$BIN/out_$f.txt" > "$BIN/n_$f.txt"; done
if cmp -s "$BIN/out_up.txt" "$BIN/out_fork.txt"; then
  echo "SAME        $name"
elif cmp -s "$BIN/n_up.txt" "$BIN/n_fork.txt"; then
  echo "SAME-dedup  $name (raw frames up=$(wc -l < "$BIN/out_up.txt") fork=$(wc -l < "$BIN/out_fork.txt"))"
else
  ln=$(cmp "$BIN/n_up.txt" "$BIN/n_fork.txt" | sed "s/.*line //")
  echo "DIFF        $name  distinct frames up=$(wc -l < "$BIN/n_up.txt") fork=$(wc -l < "$BIN/n_fork.txt"); first diff at distinct frame $ln:"
  sed -n "${ln}p" "$BIN/n_up.txt"   | cut -c1-70 | sed 's/^/      up:   /'
  sed -n "${ln}p" "$BIN/n_fork.txt" | cut -c1-70 | sed 's/^/      fork: /'
fi
