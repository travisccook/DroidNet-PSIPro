#!/usr/bin/env bash
# Build the two differential binaries on the fork's own host mocks.
# usage: build.sh <dir holding upstream PSIPro.ino, config.h, matrices.h> [out-dir]
# Prints the out-dir (default: a fresh mktemp -d). Nothing is written inside the repository.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../../.." && pwd)"
up="$1"
out="${2:-$(mktemp -d)}"
mkdir -p "$out"
cp "$up/PSIPro.ino" "$out/up_main.cpp"
cp "$up/config.h" "$up/matrices.h" "$here/Wire.h" "$out/"
CXX="${CXX:-g++}"
FLAGS="-std=gnu++17 -O1 -fsigned-char -w"
# Upstream: a flat Arduino sketch, so it needs the fork's prototype header (include/functions.h)
# ahead of it and a Wire stub. Its own config.h and matrices.h sit beside it in $out.
$CXX $FLAGS -DUPSTREAM '-DFW="up_main.cpp"' -I "$repo/test/host" -I "$repo/test/host/native_mocks" \
  -I "$out" -I "$repo/include" "$here/diffdrv.cpp" -o "$out/diff_up"
$CXX $FLAGS "-DFW=\"$repo/src/main.cpp\"" '-DWDP_FW="x"' -I "$repo/test/host" \
  -I "$repo/test/host/native_mocks" -I "$repo/include" "$here/diffdrv.cpp" -o "$out/diff_fork"
echo "$out"
