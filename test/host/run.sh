#!/usr/bin/env bash
# Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
# bolted onto Neil Hutchison's PSI Pro firmware. Copyright (c) 2026 Travis Cook.
# MIT-licensed (see LICENSE-DroidNet-Contract). The PSI Pro firmware this layer
# attaches to is the work of Neil Hutchison and contributors, is NOT covered by that
# license, and carries no license of its own; see the NOTICE in README.md.
# SPDX-License-Identifier: MIT
#
# Host checks for the PSI Pro contract fork (stages 1-3 need no hardware and no toolchain; stage 4 is the real cross-compile):
#   1. contract_core.h parser unit tests
#   2. ContractPSI.h firmware-layer type-check against a mock of the MaxPSI board API
#   3. command-buffer width guard (the v1.2 accent keys push a scored line past 63 chars)
#   4. REAL cross-compile for the ATmega32U4 (optional; needs PlatformIO)
set -e
cd "$(dirname "$0")"
echo "[1/5] contract_core parser unit tests"
clang++ -std=c++17 -Wall -Wextra -O0 test_contract_core.cpp -o /tmp/psi_contract_test
/tmp/psi_contract_test
echo "[2/5] ContractPSI.h firmware type-check"
clang++ -std=c++17 -Wall -Wextra compile_contract.cpp -o /tmp/psi_fw_syntax
/tmp/psi_fw_syntax

echo "[3/5] command-buffer width guard"
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

echo "[4/5] parser fuzz + differential (ASan/UBSan, deterministic)"
# The parsers are the one place this project eats bytes off a shared, noisy serial bus, and to
# save 1,008 B of flash on the PSI they were hand-rolled instead of using strtol/strtoul. This
# stage is the differential that keeps them honest: ~1.55M checks against an INDEPENDENT model
# and a true 32-BIT strtol/strtoul oracle (the host's own is 64-bit and would disagree on
# overflow -- which is the whole trap), plus hostile/whole-byte-range lines through
# contractParse() under AddressSanitizer + UndefinedBehaviorSanitizer.
# Fixed seed: deterministic and reproducible, not a slot machine.
clang++ -std=c++17 -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
        fuzz_parsers.cpp -o /tmp/psipro_fuzz
/tmp/psipro_fuzz

echo "[5/5] REAL cross-compile (ATmega32U4 (SparkFun Pro Micro)) -- optional"
# THIS is the stage that makes "it compiles" a claim about the FIRMWARE rather than a claim
# about our mock. Stages 1-3 are host checks: they prove the contract logic and they pin the
# board API against a HAND-WRITTEN MOCK -- and a mock is only ever as honest as its author.
# This one was not honest. The first real avr-gcc/xtensa build caught two things stages 1-3
# could never see, by construction:
#   * FastLED declares RGB as an ENUMERATOR of fl::EOrder, which name-hides a `struct RGB`.
#     Our shared core defined exactly that struct. Every plain use of the type failed to
#     compile on both FastLED boards. The mocks never modelled EOrder, so the type-check
#     passed happily.
#   * fill_column() is DEFINED in the PSI firmware but declared in no header, and our include
#     point sits above the definition. The mock DEFINED it, so the name was always in scope.
# (Both are fixed. The lesson is why this stage exists.)
#
# OPTIONAL because it needs PlatformIO and a ~200 MB toolchain. Skipped cleanly without it:
# stages 1-3 still run and still mean something -- just strictly less than they appear to.
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
[ -x "$PIO" ] || PIO="$(command -v pio 2>/dev/null || true)"
if [ -z "$PIO" ] || [ ! -x "$PIO" ]; then
  echo "SKIP: PlatformIO not found, so NOTHING here was compiled for the real MCU."
  echo "      Stages 1-3 passed, but they only type-check against our mock."
  echo "      Install it and re-run for the real check:  pip install platformio"
  echo "      Or point this at an existing install:      PIO=/path/to/pio $0"
  exit 0
fi
echo "using $PIO"
( cd ../.. && "$PIO" run -e PSIPro )
echo "cross-compiles for the real MCU (ATmega32U4 (SparkFun Pro Micro)) OK"
echo "    (a successful LINK is not a bench test: this firmware has never been flashed.)"
echo
echo "    NOTE: this board's flash budget is the tight one — 28,672 B usable, and the"
echo "    stock upstream firmware already used 87.6% of it. The linker enforces the"
echo "    ceiling, so if this stage passes, the image FITS. It fits only because of"
echo "    CONTRACT_SLIM (include/config.h) plus the codegen flags in platformio.ini and"
echo "    the PSI_NOINLINE outlining in src/contract/ContractPSI.h. Disable any of the"
echo "    three and this stage fails."
