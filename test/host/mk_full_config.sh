#!/usr/bin/env bash
# Build the FULL-variant golden binary: identical source, CONTRACT_SLIM
# removed via a shadowed config.h so Neil's five dropped modes compile in.
# Quoted-include search for "config.h" falls through src/ (no config.h there)
# into the -I chain; the shadow dir comes first.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT="${1:-/tmp/psi-host}"
mkdir -p "$OUT/config_full"
sed '/#define CONTRACT_SLIM/d' include/config.h > "$OUT/config_full/config.h"
c++ -std=gnu++17 -O1 -fsigned-char -fsanitize=address,undefined \
  -I test/host/native_mocks -I "$OUT/config_full" -I include \
  -o "$OUT/golden_full" test/host/golden_native.cpp
echo "built $OUT/golden_full"
