#!/usr/bin/env bash
# Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
# bolted onto Neil Hutchison's PSI Pro firmware. Copyright (c) 2026 Travis Cook.
# SPDX-License-Identifier: MIT
#
# Host checks for the PSI Pro contract fork (stages 1-8 need no hardware and no toolchain; stage 9 is the real cross-compile,
# stage 10 runs AVR code in simavr):
#   1. contract_core.h parser unit tests
#   1f. contract_core.h v1.3 unit tests (safety limits, clock rules, lifecycle, score, query)
#   1g. the same, built with stand-ins for this fork's flash-string hooks (PSTR / pgm_read_byte)
#   1h. the shared core files are the commit in src/contract/CORE_PIN (when that checkout is here)
#   1i. no floating point in the core or in the contract layer (and, stage 9b, none in the linked image)
#   1b. wdp_announce_core.h WDP device announce unit tests (shared with the RSeries and Flthy forks)
#   1c. wdp_fork.h: this fork's real WDP identity builds its line, front and rear
#   1d. the WDP announce in the firmware: main.cpp + mocks (TX room, the Q ack, the jumper, the seed)
#   2. psi_vm.h decode-walk test (every VM program: known ops, in-bounds arities, reaches OP_END)
#   3. (retired: the firmware-layer guards are timeline scenarios now, stage 6c, on the real main.cpp)
#   4. command-buffer width guard (the v1.2 accent keys push a scored line past 63 chars)
#   5. parser fuzz + differential (ASan/UBSan)
#   6. golden-frame parity against the committed captures (test/host/golden/)
#   6d. the I2C build's six stand-in looks draw scan, sparkle and solid, frame for frame
#   6e. contract S9 as a property of the firmware: hostile and random line streams, frames measured
#   7. scale-math oracle: the clean-room driver's scalers == FastLED 3.5.0 == the mock
#   8. HSV oracle: CE_RAINBOW (CHSV->CRGB) == FastLED 3.5.0 hsv2rgb_rainbow (full cube)
#   9. REAL cross-compile for the ATmega32U4 (optional; needs PlatformIO)
set -e
cd "$(dirname "$0")"

# Where the build products go, and which compilers build them. Nothing is written to a fixed path:
# PSI_TMP overrides the directory, CXX_SAN the sanitizer compiler.
T="${PSI_TMP:-$(mktemp -d "${TMPDIR:-/tmp}/psi-host.XXXXXX")}"
mkdir -p "$T"
# A compiler that can LINK the sanitizers: clang++ where its runtime is installed, else g++.
pick_san_cxx() {
  for c in ${CXX_SAN:-} clang++ g++ c++; do
    command -v "$c" >/dev/null 2>&1 || continue
    echo 'int main(){return 0;}' > "$T/san.cpp"
    if "$c" -fsanitize=address,undefined "$T/san.cpp" -o "$T/san" >/dev/null 2>&1; then echo "$c"; return 0; fi
  done
  return 1
}
CXX_SAN="$(pick_san_cxx)" || { echo "FAIL: no compiler here links -fsanitize=address,undefined"; exit 1; }
export CXX_SAN
CXX_HOST="$(command -v clang++ || command -v g++)"
echo "host compiler: $CXX_HOST   sanitizer compiler: $CXX_SAN   temp: $T"
echo "[1/9] contract_core parser unit tests"
"$CXX_HOST" -std=c++17 -Wall -Wextra -O0 test_contract_core.cpp -o "$T/psi_contract_test"
"$T/psi_contract_test"
echo "[1f] contract_core v1.3 unit tests"
# Shared with the RSeries and Flthy forks, like stage 1. -std=c++11: avr-gcc builds the core as gnu++11.
"$CXX_HOST" -std=c++11 -Wall -Wextra -O0 test_contract_v13.cpp -o "$T/psi_contract_v13"
"$T/psi_contract_v13"
echo "[1g] contract_core v1.3 unit tests, flash-string hooks on"
# ContractPSI.h defines CONTRACT_PSTR / CONTRACT_READ_BYTE as PSTR() / pgm_read_byte() on the AVR.
# This build stands in for that: every read of a name table must go through CONTRACT_READ_BYTE, or
# it faults here instead of reading garbage from RAM on the board.
"$CXX_HOST" -std=c++11 -Wall -Wextra -O0 -DCONTRACT_TEST_FLASH_HOOKS test_contract_v13.cpp -o "$T/psi_contract_v13_hooks"
"$T/psi_contract_v13_hooks"
echo "[1h] shared core files match src/contract/CORE_PIN"
# The core lives in the RSeries fork; this repo carries a byte-identical copy of every file in that
# repo's test/host/core-files.txt at the pinned commit. Checked when that checkout is on this
# machine (CORE_REPO, default ../DroidNet-RSeriesLogics beside this repo) and has the commit.
CORE_REPO="${CORE_REPO:-../../../DroidNet-RSeriesLogics}"
CORE_PIN="$(cat ../../src/contract/CORE_PIN)"
if git -C "$CORE_REPO" cat-file -e "$CORE_PIN^{commit}" 2>/dev/null; then
  for f in $(git -C "$CORE_REPO" show "$CORE_PIN:test/host/core-files.txt"); do
    git -C "$CORE_REPO" show "$CORE_PIN:$f" | cmp -s - "../../$f" || { echo "FAIL: $f differs from $CORE_PIN"; exit 1; }
  done
  echo "    identical to ${CORE_PIN:0:7}"
else
  echo "    (skipped: no core checkout with $CORE_PIN at $CORE_REPO)"
fi
echo "[1i] no float in the core or the layer"
# Core CA1.3: no float in the shared core, nor in the layer that calls it every frame. On this board
# one float expression links the compiler's soft-float library, about 1.3 KB of a 28 KB part, and
# a float in the beat maths is a rounding Studio's mirror cannot reproduce to the bit. Comments are
# stripped first; the word is fine in prose. Stage 9b checks the linked image as well.
for f in ../../src/contract/contract_core.h ../../src/contract/ContractPSI.h; do
  if sed 's://.*::' "$f" | grep -nE '\b(float|double)\b|[0-9]\.[0-9]+f\b|<math\.h>|<cmath>|\b(floorf?|ceilf?|roundf?|fabsf?|sqrtf?|powf?)[[:space:]]*\('; then
    echo "FAIL: $f uses floating point (the lines above). The core and the per-frame path are integer only."; exit 1
  fi
done
echo "OK  contract_core.h and ContractPSI.h are integer only"
echo "[1b/9] wdp_announce_core WDP device announce unit tests"
# Shared byte-identically with the RSeries and Flthy forks (src/wdp/wdp_announce_core.h and this
# test both). -std=c++11: avr-gcc builds the same header as gnu++11.
"$CXX_HOST" -std=c++11 -Wall -Wextra -Werror -O0 test_wdp_announce_core.cpp -o "$T/psi_wdp_test"
"$T/psi_wdp_test"
echo "[1c/9] wdp_fork PSI Front / PSI Rear identity"
# Fork-local. The header's own types and caps, with the longest fw git gives a build
# ("<sha>-dirty"), must build a valid line for both roles in the fork's WDP_LINE_MAX, which is
# sized to exactly the front line (78 B here) rather than the core's 128.
"$CXX_HOST" -std=c++11 -Wall -Wextra -Werror -O0 test_wdp_fork.cpp -o "$T/psi_wdp_fork_test"
"$T/psi_wdp_fork_test"
echo "[1d/9] WDP announce in the firmware (main.cpp + mocks)"
# src/main.cpp verbatim against the golden harness's mocks, Serial1 modelled as a 63-byte TX
# buffer draining at 9600 baud: pieces never exceed the room, a verb-Q ack mid-line waits for the
# line (ContractPSI.h calls wdpFinish()), the jumper picks the type at each announce, and the seed
# differs front vs rear. -std=gnu++17: the mocks use inline variables. -Wno-macro-redefined:
# config.h redefines preamble.h's DEBUG_PRINT/UNUSED/FALL_THROUGH (upstream; stage 6 shows them).
"$CXX_SAN" -std=gnu++17 -O1 -fsigned-char -fsanitize=address,undefined -fno-sanitize-recover=all \
    -Wno-macro-redefined -I native_mocks -I ../../include '-DWDP_FW="abcdef0-dirty"' \
    test_wdp_firmware.cpp -o "$T/psi_wdp_firmware"
"$T/psi_wdp_firmware"
echo "[2/9] psi_vm.h decode-walk test"
# Structural check of every VM program (test/host/test_psi_vm.cpp): ops are known,
# arities consume in-bounds bytes, every program reaches OP_END within its array. Built
# in PSI_VM_TABLES_ONLY mode (defined by the test file itself) — no firmware globals, no
# dispatch, so this passes independent of whether vmPlay() is wired into runPattern() yet.
"$CXX_SAN" -std=c++17 -O1 -fsigned-char -fsanitize=address,undefined \
        -I . -I native_mocks test_psi_vm.cpp -o "$T/psi_vm_decode"
"$T/psi_vm_decode"
echo "[4/9] command-buffer width guard"
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
# The timeline scenario an_i2c_contract_line_is_applied_by_loop (stage 6c, I2C build) proves the
# BEHAVIOUR of the queue, but it cannot see someone
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

echo "[5/9] parser fuzz + differential (ASan/UBSan, deterministic)"
# The parsers are the one place this project eats bytes off a shared, noisy serial bus, and to
# save 1,008 B of flash on the PSI they were hand-rolled instead of using strtol/strtoul. This
# stage is the differential that keeps them honest: ~1.55M checks against an INDEPENDENT model
# and a true 32-BIT strtol/strtoul oracle (the host's own is 64-bit and would disagree on
# overflow -- which is the whole trap), plus hostile/whole-byte-range lines through
# contractParse() under AddressSanitizer + UndefinedBehaviorSanitizer.
# Fixed seed: deterministic and reproducible, not a slot machine.
"$CXX_SAN" -std=c++17 -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
        fuzz_parsers.cpp -o "$T/psipro_fuzz"
"$T/psipro_fuzz"

echo "[6/9] golden-frame parity: mocks + Neil's animation code must reproduce"
echo "      the committed goldens bit-for-bit (test/host/golden/)."
# The FULL-variant binary (built via mk_full_config.sh's shadowed config.h — a
# historical CONTRACT_SLIM strip that is now a documented no-op, see that script) is
# the ground truth. golden_matrix.txt's first 30 rows are not 30 modes: 28 of them are captures of
# the 23 upstream modes' (0-21 and 92) native ground truth (several modes get more than one row —
# front/rear jumper, alwaysOn); one more (contract_sparkle) is the contract CE_SPARKLE look
# captured from the pre-shim native (DiscoBall at 9502cad), which the VM shim must reproduce.
# Its line is fed at 1000 ms, not at boot: a live look in the first 170 ms of uptime is held (S4),
# and the swipe that runs meanwhile draws from the same PRNG
# (contract scan is not a capture any more: v1.3 defines it by fxScanPos() in the core, and the
# timeline scenario scan_is_the_shared_ping_pong checks the panel against that function);
# the last, mode22_process, has "ground truth" from the
# VM program itself (see vmc_process's comment, include/psi_vm.h) rather than any native code.
# golden_matrix.txt drives the same capture
# command line that produced the committed .psig files; any regenerated capture
# that doesn't cmp byte-identical to its committed twin means the mocks or Neil's
# unmodified main.cpp no longer reproduce what we locked in as ground truth.
# A <cmd> holding '@' is a script, "<ms>@<line>;<ms>@<line>...": the seq_* and timing_* rows are
# sequences and |timing commands, each also a `same` row of the upstream differential (stage 6b).
# Matrix columns: <name> <cmd> <ms> <jumper> <eeprom> <pot> [at_ms] — the optional
# 7th column delays the command feed so buffer-transforming modes (FadeOut) get a
# painted default-swipe buffer to work on instead of dimming an all-black one.
BUILD="$T/psi-host"
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

echo "[6d] the I2C build's stand-in looks"
# The PSIPro-i2c environment builds without the six strip and hue looks (PSI_NO_STRIP_LOOKS). A
# line naming one must draw the nearest look that build has, frame for frame: comet, chase and wipe
# are scan, twinkle is sparkle, gradient and colorcycle are solid in c=.
"$CXX_SAN" -std=gnu++17 -O1 -fsigned-char -fsanitize=address,undefined -w -DPSI_ENABLE_I2C -DPSI_NO_STRIP_LOOKS \
    -I native_mocks -I "$BUILD/config_full" -I ../../include -o "$BUILD/golden_i2c" golden_native.cpp
frames() { python3 -c 'import sys; d=open(sys.argv[1],"rb").read(); sys.stdout.buffer.write(d[d.index(b"\nFRAMES\n"):])' "$1"; }
standin() {  # <look named on the wire> <look drawn> [committed golden of the look drawn]
  "$BUILD/golden_i2c" --cmd "!P*A:i=$1,c=ff0000" --ms 12000 --jumper front --eeprom 00,01,14 --pot 512 \
      --out "$GOLD/i2c_$1.psig" 2>/dev/null
  if [ -n "${3:-}" ]; then want="golden/$3.psig"; else
    want="$GOLD/i2c_$2.psig"
    "$BUILD/golden_i2c" --cmd "!P*A:i=$2,c=ff0000" --ms 12000 --jumper front --eeprom 00,01,14 --pot 512 \
        --out "$want" 2>/dev/null
  fi
  cmp -s <(frames "$want") <(frames "$GOLD/i2c_$1.psig") || { echo "FAIL: on the I2C build i=$1 is not i=$2"; exit 1; }
}
standin comet scan; standin chase scan; standin wipe scan
standin twinkle sparkle
standin gradient solid; standin colorcycle solid
# ...and the build still has the looks it keeps: rainbow is not solid there.
standin rainbow rainbow
if cmp -s <(frames "$GOLD/i2c_rainbow.psig") <(frames "$GOLD/i2c_solid.psig"); then echo "FAIL: rainbow drew as solid"; exit 1; fi
echo "      6 stand-ins identical to the looks they draw."

echo "[6b] upstream differential: Neil's PSIPro v1.7 and the fork on the same mocks"
# 77 scripted timelines (mode after mode, |timing, alwaysOn, rear jumper, brightness commands)
# through test/host/upstream-v1.7/PSIPro.ino and through src/main.cpp. Each row states whether the
# visible frame sequence must be identical or is a known, explained difference; either way a row
# that changes sides fails. The goldens above are captured from the fork itself; this is the stage
# that ties them to upstream.
bash diff_upstream.sh "$T" "$CXX_SAN"

echo "[6c] timeline scenarios: the firmware on the mocks, scripted wire timelines"
# src/main.cpp verbatim, a scripted timeline of wire lines, assertions on the frames show() latched
# and on the bytes written to Serial1. One scenario per process (the firmware keeps static state).
# Built twice, as the two environments are: serial-only, and I2C without the strip looks. The I2C
# run adds the scenarios that feed a line through the TWI receive callback.
"$CXX_SAN" -std=gnu++17 -O1 -fsigned-char -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    -I . -I native_mocks -I ../../include '-DWDP_FW="abcdef0"' test_timeline.cpp -o "$T/psi_timeline"
"$CXX_SAN" -std=gnu++17 -O1 -fsigned-char -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    -DPSI_ENABLE_I2C -DPSI_NO_STRIP_LOOKS \
    -I . -I native_mocks -I ../../include '-DWDP_FW="abcdef0"' test_timeline.cpp -o "$T/psi_timeline_i2c"
for bin in psi_timeline psi_timeline_i2c; do
  n=0
  for s in $("$T/$bin" --list); do
    "$T/$bin" "$s" > "$T/scenario.out" 2>&1 || { cat "$T/scenario.out"; echo "FAIL: $bin $s"; exit 1; }
    n=$((n + 1))
  done
  echo "      $bin: $n scenarios ok"
done

echo "[6e] contract S9 as a property: hostile and random line streams, the frames measured"
# test_property.cpp: 14 hostile profiles x 8 rounds and 1500 random streams, each in a fresh copy
# of the firmware, on both builds. Dark-to-lit onsets (at most 3 a second, never two inside
# 340 ms), nothing above the owner's brightness, dark after X, native after idle, and a native
# command keeps the panel.
for cfg in "" "-DPSI_ENABLE_I2C -DPSI_NO_STRIP_LOOKS"; do
  # shellcheck disable=SC2086
  "$CXX_SAN" -std=gnu++17 -O1 -fsigned-char -fsanitize=address,undefined -fno-sanitize-recover=all -w $cfg \
      -I . -I native_mocks -I ../../include '-DWDP_FW="abcdef0"' test_property.cpp -o "$T/psi_property"
  echo "      ${cfg:-serial-only}"
  "$T/psi_property" > "$T/property.out" 2>&1 || { cat "$T/property.out"; echo "FAIL: the S9 property does not hold"; exit 1; }
  sed 's/^/      /' "$T/property.out"
done

echo "[7/9] scale-math oracle: the clean-room driver's scalers == FastLED 3.5.0 == the mock"
# include/FastLED.h replaced FastLED with a from-scratch WS2812 driver. The 31 goldens
# gate the firmware's STAGING against native_mocks/FastLED.h, NOT against that driver, so
# on their own they say nothing about the swap. This oracle is the bridge: it holds the
# driver's scale8 / scale8_video / nscale8x3_video / qmul8 to BOTH references over all
# 256*256 inputs -- equal to the mock means every golden transfers to the driver, equal
# to FastLED 3.5.0 means the panel sees what stock FastLED drew. Self-contained.
"$CXX_HOST" -std=c++17 -O1 -fsigned-char -Wall -Wextra test_scale_oracle.cpp -o "$T/psi_scale_oracle"
"$T/psi_scale_oracle"

echo "[8/9] HSV oracle: CE_RAINBOW (CHSV->CRGB) == FastLED 3.5.0 hsv2rgb_rainbow"
# The firmware's one CHSV call site (CE_RAINBOW, ContractPSI.h:216) is covered by no
# golden. If the driver's rainbow drifted from FastLED's, CE_RAINBOW's hue mapping -- and
# the Studio visualiser's JS parity vectors -- would drift with it. Full 256^3 cube plus
# the s=v=255 call-site sweep; a nonzero diff fails the suite (set -e).
"$CXX_HOST" -std=c++17 -O2 -fsigned-char -Wall -Wextra test_hsv_oracle.cpp -o "$T/psi_hsv_oracle"
"$T/psi_hsv_oracle"

echo "[9/9] REAL cross-compile (ATmega32U4 (SparkFun Pro Micro)) -- optional"
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
  PIO=""
fi
if [ -n "$PIO" ]; then
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
echo "    animations are added. (as of the WDP device announce: PSIPro 25,320 B / 88.3%,"
echo "    PSIPro-i2c 26,758 B / 93.3% — re-measure with 'pio run && pio run -e PSIPro-i2c'"
echo "    rather than trusting this comment.) It fits because of the codegen flags in"
echo "    platformio.ini and the PSI_NOINLINE outlining in src/contract/ContractPSI.h —"
echo "    disable both and the I2C build fails. There used to be a third leg here,"
echo "    CONTRACT_SLIM, that dropped five native modes to make room; those five are"
echo "    now permanent PROGMEM bytecode (include/psi_vm.h), all 23 upstream modes"
echo "    ship, and CONTRACT_SLIM is gone."

echo "[9b] flash and stack gates"
# Flash: what the linker placed (text + data) against a ceiling below the board's 28,672 B, so the
# next change has room. Stack: the SOUND worst-case bound from stack_report.py (the linked ELF, not
# -fstack-usage) must leave this many bytes below the static data. A change that fails a gate needs
# a decision, not a smaller gate.
AVR_BIN="${AVR_BIN:-$HOME/.platformio/packages/toolchain-atmelavr/bin}"
gate() {  # <env> <max flash B> <min stack headroom B>
  elf="../../.pio/build/$1/firmware.elf"
  flash=$("$AVR_BIN/avr-size" "$elf" | awk 'NR==2 {print $1 + $2}')
  head=$(python3 stack_report.py --elf "$elf" --objdump "$AVR_BIN/avr-objdump" \
         | sed -n 's/^ *HEADROOM *\(-\{0,1\}[0-9]*\) B.*/\1/p')
  echo "    $1: flash $flash B (limit $2), sound stack headroom ${head:-none} B (floor $3)"
  if [ "$flash" -gt "$2" ] || [ -z "$head" ] || [ "$head" -lt "$3" ]; then echo "FAIL: $1 is over a gate"; exit 1; fi
  # No soft-float in the image (core CA1.3). One float expression anywhere in the firmware links
  # about 1.3 KB of it; this is the check on what was actually linked.
  if "$AVR_BIN/avr-nm" "$elf" | grep -E ' (__(add|sub|mul|div|cmp|ge|le|gt|lt|eq|ne|unord)[sd]f[23]x?|__float(un)?[sd]i[sd]f|__fix(uns)?[sd]f[sd]i|__fp_[a-zA-Z0-9]+|floorf?|ceilf?)$'; then
    echo "FAIL: $1 links soft-float routines (above): something in the firmware uses floating point"; exit 1
  fi
}
gate PSIPro 28416 256
gate PSIPro-i2c 28160 128
fi   # PlatformIO present

echo "[10] simavr: millis() rate per look, the show lifecycle, S9 on the wire, WS2812 waveform, CPU cost -- optional"
# The host mocks have an ideal clock and a free show(). These five probes run real AVR code in
# simavr (test/simavr/README.md): the firmware's millis() against Timer1 for each contract look,
# the firmware through a show, X, idle, a native take-over, a pulse and a watchdog expiry (what the panel returns to
# depends on when upstream's loop samples its pattern state, so it is checked on the real timer
# too), the firmware under two hostile line streams with every frame decoded off the LED pin and
# its dark-to-lit onsets counted (the S9 gate sits in the real driver's show(), which the host
# suite only has a mock of), the WS2812 waveform on PD4, and the cost of the per-frame math.
# Each probe halts the simulated CPU when it is done; the timeouts are only an upper bound.
SIMAVR="${SIMAVR:-$HOME/.platformio/packages/tool-simavr/bin/simavr}"
if [ -n "$PIO" ] && [ -x "$SIMAVR" ]; then
  simrun() {  # <project> <seconds> <log> [simavr args...]: build, run until the timeout, clean the log
    p=$1; secs=$2; log=$3; shift 3
    ( cd "../simavr/$p" && "$PIO" run -s ) > "$log.build" 2>&1 || { cat "$log.build"; echo "FAIL: the $p probe did not build"; exit 1; }
    timeout "$secs" "$SIMAVR" -m atmega32u4 -f 16000000 "$@" "../simavr/$p/.pio/build/$p/firmware.elf" \
      > "$log.raw" 2>&1 || true
    sed 's/\x1b\[[0-9;]*m//g; s/\.\.$//' "$log.raw" > "$log"
  }
  simrun millis 240 "$T/millis.txt"
  # 1000 ppm is a fifth of the contract's +/-0.5 % clock budget; the exact credit measures under 400.
  python3 ../simavr/check_millis.py "$T/millis.txt" --limit-ppm 1000
  simrun lifecycle 45 "$T/lifecycle.txt"
  python3 ../simavr/check_lifecycle.py "$T/lifecycle.txt"
  simrun gate 60 "$T/gate.txt" -o "$T/gate.vcd" -at "led=trace@0x2b/0x10"
  python3 ../simavr/check_gate.py "$T/gate.vcd" "$T/gate.txt"
  simrun wave 8 "$T/wave.txt" -o "$T/wave.vcd" -at "led=trace@0x2b/0x10"
  python3 ../simavr/check_wave.py "$T/wave.vcd" --min-gap-us 50
  simrun bench 30 "$T/bench.txt"
  grep ' us=' "$T/bench.txt" | sed 's/^/    /'
else
  echo "SKIP: needs PlatformIO and its simavr package:"
  echo "      pip install platformio && pio pkg install -g --tool platformio/tool-simavr"
fi
