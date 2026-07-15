#!/usr/bin/env bash
# Build the FULL-variant golden binary: identical source, run through a shadowed
# config.h. Historically this sed stripped CONTRACT_SLIM so Neil's five dropped modes
# compiled back in for the golden capture; CONTRACT_SLIM is gone (Task 15 — all five
# modes are permanent VM bytecode now, see include/psi_vm.h), so the sed below matches
# nothing and config_full/config.h comes out byte-identical to include/config.h. Left
# in place, as a documented no-op, rather than deleted: the shadow-include mechanism
# (below) is still how this script produces the "ground truth" binary run.sh's golden
# stage diffs against, and a future CONTRACT_SLIM-shaped hatch would drop back in here
# with one line.
# Quoted-include search for "config.h" falls through src/ (no config.h there)
# into the -I chain; the shadow dir comes first.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT="${1:-/tmp/psi-host}"
mkdir -p "$OUT/config_full"
sed '/#define CONTRACT_SLIM/d' include/config.h > "$OUT/config_full/config.h"  # no-op now; see above
c++ -std=gnu++17 -O1 -fsigned-char -fsanitize=address,undefined \
  -I test/host/native_mocks -I "$OUT/config_full" -I include \
  -o "$OUT/golden_full" test/host/golden_native.cpp
echo "built $OUT/golden_full"
