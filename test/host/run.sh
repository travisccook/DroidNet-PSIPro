#!/usr/bin/env bash
# Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
# bolted onto Neil Hutchison's PSI Pro firmware. Copyright (c) 2026 Travis Cook.
# MIT-licensed (see LICENSE-DroidNet-Contract). The PSI Pro firmware this layer
# attaches to is the work of Neil Hutchison and contributors, is NOT covered by that
# license, and carries no license of its own; see the NOTICE in README.md.
# SPDX-License-Identifier: MIT
#
# Host checks for the PSI Pro contract fork (no hardware / no AVR toolchain needed):
#   1. contract_core.h parser unit tests
#   2. ContractPSI.h firmware-layer type-check against a mock of the MaxPSI board API
#   3. command-buffer width guard (the v1.2 accent keys push a scored line past 63 chars)
set -e
cd "$(dirname "$0")"
echo "[1/3] contract_core parser unit tests"
clang++ -std=c++17 -Wall -Wextra -O0 test_contract_core.cpp -o /tmp/psi_contract_test
/tmp/psi_contract_test
echo "[2/3] ContractPSI.h firmware type-check"
clang++ -std=c++17 -Wall -Wextra compile_contract.cpp -o /tmp/psi_fw_syntax
/tmp/psi_fw_syntax

echo "[3/3] command-buffer width guard"
# buildCommand() (src/main.cpp) DROPS every byte past CMD_MAX_LENGTH-1 and terminates
# there — an over-long line is SILENTLY TRUNCATED and then parsed as if complete. The
# longest scored line the Studio emitter can produce must therefore fit, with its NUL.
# CMD_MAX_LENGTH is defined TWICE (preamble.h, then config.h, which wins by redefinition);
# both must be >= that length + 1, or the value that reaches buildCommand() is the wrong one.
worst='!P*A:i=colorcycle,c=3b82f6,s=255,b=200,at=1234,am=2,m=200,ae=colorcycle,ac=ffffff,ad=2550'
need=$(( ${#worst} + 1 ))
for f in ../../include/preamble.h ../../include/config.h; do
  val=$(sed -n 's/^#define[[:space:]]\{1,\}CMD_MAX_LENGTH[[:space:]]\{1,\}\([0-9]\{1,\}\).*/\1/p' "$f" | tail -1)
  if [ -z "$val" ]; then
    echo "FAIL: no CMD_MAX_LENGTH definition found in $f"; exit 1
  fi
  if [ "$val" -lt "$need" ]; then
    echo "FAIL: $f defines CMD_MAX_LENGTH $val, but the longest v1.2 scored line is"
    echo "      ${#worst} chars and needs $need (line + NUL). It would be silently truncated:"
    echo "      $worst"
    exit 1
  fi
done
echo "CMD_MAX_LENGTH >= $need in preamble.h + config.h (worst v1.2 line ${#worst} chars) OK"
