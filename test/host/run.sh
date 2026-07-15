#!/usr/bin/env bash
# Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
# bolted onto Neil Hutchison's PSI Pro firmware. Copyright (c) 2026 Travis Cook.
# SPDX-License-Identifier: MIT
#
# Host checks for the PSI Pro contract fork (stages 1-6 need no hardware and no toolchain; stage 7 is the real cross-compile):
#   1. contract_core.h parser unit tests
#   2. psi_vm.h decode-walk test (every VM program: known ops, in-bounds arities, reaches OP_END)
#   3. ContractPSI.h firmware-layer type-check against a mock of the MaxPSI board API
#   4. command-buffer width guard (the v1.2 accent keys push a scored line past 63 chars)
#   5. parser fuzz + differential (ASan/UBSan)
#   6. golden-frame parity against the committed captures (test/host/golden/)
#   7. REAL cross-compile for the ATmega32U4 (optional; needs PlatformIO)
set -e
cd "$(dirname "$0")"
echo "[1/7] contract_core parser unit tests"
clang++ -std=c++17 -Wall -Wextra -O0 test_contract_core.cpp -o /tmp/psi_contract_test
/tmp/psi_contract_test
echo "[2/7] psi_vm.h decode-walk test"
# Structural check of every VM program (test/host/test_psi_vm.cpp): ops are known,
# arities consume in-bounds bytes, every program reaches OP_END within its array. Built
# in PSI_VM_TABLES_ONLY mode (defined by the test file itself) — no firmware globals, no
# dispatch, so this passes independent of whether vmPlay() is wired into runPattern() yet.
clang++ -std=c++17 -O1 -fsigned-char -fsanitize=address,undefined \
        -I . -I native_mocks test_psi_vm.cpp -o /tmp/psi_vm_decode
/tmp/psi_vm_decode
echo "[3/7] ContractPSI.h firmware type-check"
# BOTH configurations. This fork is serial-only by default (include/config.h, I2C INTAKE),
# and I2C is an opt-in -D. An option nobody compiles is an option that rots, so check both:
# the serial-only build is what ships, and the I2C build is what anyone with a MarcDuino on
# the bus will flash. Guard A11 (the ISR deferral) only exists in the I2C config.
clang++ -std=c++17 -Wall -Wextra compile_contract.cpp -o /tmp/psi_fw_syntax
/tmp/psi_fw_syntax
clang++ -std=c++17 -Wall -Wextra -D PSI_ENABLE_I2C compile_contract.cpp -o /tmp/psi_fw_syntax_i2c
/tmp/psi_fw_syntax_i2c

echo "[4/7] command-buffer width guard"
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

# --- STATIC GUARD: the I2C ISR must not parse or render -------------------------------------
# receiveEvent() (src/main.cpp) is the Wire onReceive callback and runs in TWI INTERRUPT context.
# FastLED's show() ends with an unconditional `sei` while the I2C slave is already re-armed, so
# anything that renders from in there lets the ISR re-enter itself and walk the stack down into
# .bss (~190-230 B a level against 1,076 B of headroom; three levels overflow). The contract path
# is therefore DEFERRED: the ISR calls contractQueueFromISR() and loop() calls
# contractServicePending().
# compile_contract.cpp's A11 guard proves the BEHAVIOUR of the queue, but it cannot see someone
# "simplifying" main.cpp's ISR back to a direct parseContract() call — the queue would still pass
# its own test while the hazard quietly returned. So pin the call site itself. (Comments are
# stripped first: the ISR's comment block legitimately explains why parseContract is NOT there.)
if sed 's://.*::' ../../src/main.cpp | awk '/^void receiveEvent/,/^}/' | grep -q 'parseContract'; then
  echo "FAIL: receiveEvent() names parseContract(). That is the I2C ISR — parsing (and the render"
  echo "      it triggers) must NOT happen in interrupt context. FastLED's show() re-enables"
  echo "      interrupts mid-render with the slave re-armed, so the ISR re-enters itself and the"
  echo "      stack walks into .bss. Queue with contractQueueFromISR(); loop() services it."
  exit 1
fi
if ! grep -q 'contractServicePending()' ../../src/main.cpp; then
  echo "FAIL: loop() never calls contractServicePending(), so any '!' line the I2C ISR queued is"
  echo "      never parsed — contract commands over I2C would silently do nothing."
  exit 1
fi
echo "I2C deferral: the ISR queues, loop() services (no parse/render in interrupt context) OK"

echo "[5/7] parser fuzz + differential (ASan/UBSan, deterministic)"
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

echo "[6/7] golden-frame parity: mocks + Neil's animation code must reproduce"
echo "      the committed goldens bit-for-bit (test/host/golden/)."
# The FULL-variant binary (built via mk_full_config.sh's shadowed config.h — a
# historical CONTRACT_SLIM strip that is now a documented no-op, see that script) is
# the ground truth. golden_matrix.txt's 31 rows are not 31 modes: 28 of them are captures of
# the 23 upstream modes' (0-21 and 92) native ground truth (several modes get more than one row —
# front/rear jumper, alwaysOn); two more (contract_scan/contract_sparkle) are the contract
# CE_SCAN/CE_SPARKLE looks captured from the pre-shim natives (scanCol/DiscoBall at 9502cad),
# which the VM shims must reproduce byte-for-byte; the last, mode22_process, has "ground truth" from the
# VM program itself (see vmc_process's comment, include/psi_vm.h) rather than any native code.
# golden_matrix.txt drives the same capture
# command line that produced the committed .psig files; any regenerated capture
# that doesn't cmp byte-identical to its committed twin means the mocks or Neil's
# unmodified main.cpp no longer reproduce what we locked in as ground truth.
# Matrix columns: <name> <cmd> <ms> <jumper> <eeprom> <pot> [at_ms] — the optional
# 7th column delays the command feed so buffer-transforming modes (FadeOut) get a
# painted default-swipe buffer to work on instead of dimming an all-black one.
BUILD="/tmp/psi-host"
GOLD="$BUILD/golden"
mkdir -p "$GOLD"
./mk_full_config.sh "$BUILD"
while read -r name cmd ms jumper ee pot at; do
  [ -z "$name" ] && continue
  "$BUILD/golden_full" --cmd "$cmd" --ms "$ms" --jumper "$jumper" \
    --eeprom "$ee" --pot "$pot" --at "${at:-0}" --out "$GOLD/$name.psig" 2>/dev/null
  if ! cmp -s "golden/$name.psig" "$GOLD/$name.psig"; then
    echo "GOLDEN MISMATCH: $name"
    python3 golden_compare.py "golden/$name.psig" "$GOLD/$name.psig" || true
    exit 1
  fi
done < golden_matrix.txt
echo "      $(wc -l < golden_matrix.txt | tr -d ' ') captures identical."

# Set equality, both directions, between golden_matrix.txt's row names and
# test/host/golden/*.psig's basenames: the loop above only proves that every
# NAMED row's regenerated capture matches its committed twin — it says
# nothing about a matrix row with no committed golden (silently never
# diffed) or a committed .psig nothing in the matrix drives (a stale capture
# left behind by a renamed/deleted row, never regenerated or checked again).
matrix_names="$(awk 'NF{print $1}' golden_matrix.txt | sort -u)"
golden_names="$(cd golden && ls -1 *.psig | sed 's/\.psig$//' | sort -u)"
if [ "$matrix_names" != "$golden_names" ]; then
  echo "MATRIX/GOLDEN NAME MISMATCH (golden_matrix.txt rows vs test/host/golden/*.psig):"
  diff <(echo "$matrix_names") <(echo "$golden_names") || true
  exit 1
fi

echo "[7/7] REAL cross-compile (ATmega32U4 (SparkFun Pro Micro)) -- optional"
# THIS is the stage that makes "it compiles" a claim about the FIRMWARE rather than a claim
# about our mock. Stages 1-6 are host checks: they prove the contract logic, fuzz the
# parsers, gate the golden frames, and pin the board API against a HAND-WRITTEN MOCK -- and
# a mock is only ever as honest as its author.
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
# stages 1-6 still run and still mean something -- just strictly less than they appear to.
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
[ -x "$PIO" ] || PIO="$(command -v pio 2>/dev/null || true)"
if [ -z "$PIO" ] || [ ! -x "$PIO" ]; then
  echo "SKIP: PlatformIO not found, so NOTHING here was compiled for the real MCU."
  echo "      Stages 1-6 passed, but they only run against our host mocks."
  echo "      Install it and re-run for the real check:  pip install platformio"
  echo "      Or point this at an existing install:      PIO=/path/to/pio $0"
  exit 0
fi
echo "using $PIO"
( cd ../.. && "$PIO" run -e PSIPro -e PSIPro-i2c )
echo "cross-compiles for the real MCU (ATmega32U4 (SparkFun Pro Micro)) OK"
echo "    BOTH configs built: PSIPro (serial-only) and PSIPro-i2c — sizes below."
echo "    (a successful LINK is not a bench test: this firmware has never been flashed.)"
echo
echo "    NOTE: this board's flash budget is the tight one — 28,672 B usable, and the"
echo "    stock upstream firmware already used 87.6% of it. The linker enforces the"
echo "    ceiling, so if this stage passes, both configs FIT — THAT is the invariant this"
echo "    stage exists to hold, not any specific byte count, which will keep moving as"
echo "    animations are added. (as of feature/animation-vm: PSIPro 26,760 B / 93.3%,"
echo "    PSIPro-i2c 28,202 B / 98.4% — re-measure with 'pio run && pio run -e PSIPro-i2c'"
echo "    rather than trusting this comment.) It fits because of the codegen flags in"
echo "    platformio.ini and the PSI_NOINLINE outlining in src/contract/ContractPSI.h —"
echo "    disable either and this stage fails. There used to be a third leg here,"
echo "    CONTRACT_SLIM, that dropped five native modes to make room; those five are"
echo "    now permanent PROGMEM bytecode (include/psi_vm.h), all 23 upstream modes"
echo "    ship, and CONTRACT_SLIM is gone."
