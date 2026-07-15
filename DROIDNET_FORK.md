# DroidNet fork notes — PSI Pro

Working notes for the fork itself. For attribution and the hardware warning, read
[README.md](README.md) first — none of that is repeated here.

## What this is

An additive fork of Neil Hutchison's PSI Pro firmware (<https://github.com/nhutchison/PSIPro>) that
implements the DroidNet **Driveable-Animation Contract**, so Cantina Studio can drive the 48-LED PSI
matrix with arbitrary RGB, beat-locked accents, a host-seeded beat clock, and autonomous board-side
section playback. Every native JawaLite `<addr>T` / `A` / `D` / `xPy` command and the I2C intake keep
working exactly as they did.

- **Role:** the PSI board of the three-board Cantina dome light-show family (RSeries Logics, PSI Pro,
  Flthy HPs). This is the tightest of the three and the pacing item.
- **MCU/toolchain:** ATmega32U4 (Sparkfun Pro Micro), FastLED. `platformio.ini` is checked in and
  builds out of the box. `pio run` (serial-only, shipped default) links at 26,760 B of 28,672 B
  (93.3%); `pio run -e PSIPro-i2c` links at 28,202 B (98.4%). Both totals already include mode 22
  ("processing sweep"), a fork-added demo animation that is not one of the 23 upstream modes — it
  exists to measure the marginal cost of a new one: 94 B, both configs. All 23 upstream modes (0-21
  and 92) ship; every upstream mode's animation except the four deliberate native holdouts
  (swipe/VUMeter/allON/allOFF) is PROGMEM bytecode played by `include/psi_vm.h`'s interpreter (the
  contract effects remain parametric C++ renderers; CE_SCAN/CE_SPARKLE via hand-written shims in
  psi_vm.h), and that is what makes both fit. See README, "Building" and "The animation VM: modes
  are data now".
- **Seeded from:** the private C2B5 droid working collection @ `2fbd4023` (subdir `PSIPro`),
  2026-07-12, git-tracked files only. That collection is where the PlatformIO restructuring of Neil's
  flat Arduino sketch and the `visualizer/` came from.
- **GitHub remote:** <https://github.com/travisccook/DroidNet-PSIPro> (public).

## Status

**Code complete, host-verified, never run on hardware.**

- Phase-1 (live cues: verbs A/P/C/B/L/X/M/Q) — implemented in `src/contract/ContractPSI.h`.
- Phase-2 (score push + autonomous board-side section playback) — implemented; `g_score[8]` holds up
  to 8 sections keyed to absolute beat numbers.
- Six parametric effects (comet, chase, wipe, gradient, colorcycle, twinkle) — implemented, rendered
  across the panel's 10 columns as a 1-D strand.
- Shared beat-brightness envelope (`envBright`) — identical across all three forks.
- Pre-existing upstream `buildCommand()` out-of-bounds write — fixed in `src/main.cpp` (bounds-check
  before the write, terminator clamped). Offered back to Neil.

Fork head: `main` in this repository. The baseline import is commit `11d69a3`; every commit after it
is the DroidNet layer (`git log --oneline 11d69a3..HEAD`).

## Verification

```bash
bash test/host/run.sh
```

1. `contract_core.h` parser + effect-math unit tests — **341/341 checks**.
2. `ContractPSI.h` firmware-layer type-check against `test/host/mock_psi.h`, a mock of the MaxPSI
   board API (plus the score-native / frame-latch / score-clear / build-ramp / scale guards).

The list above predates most of the harness; `bash test/host/run.sh` has grown well past those two
checks (golden-frame parity against Neil's animation code, parser fuzzing, and a real AVR
cross-compile of both build configs — see README, "STOP: this has never run on hardware", for the
current picture). What has NOT changed: there has been **no bench flash**. Flash headroom on the
32U4 is no longer an open risk — it is measured on every `run.sh` — and there is no `CONTRACT_SLIM`
escape hatch anymore; see README, "It fits" and "The animation VM: modes are data now".

## Layout

| Path | Owner |
| --- | --- |
| `src/contract/contract_core.h` | DroidNet. Byte-identical across the RSeries/PSI/Flthy forks — **do not diverge**. Change it in one fork, copy it to the other two. |
| `src/contract/ContractPSI.h` | DroidNet. PSI render layer; calls only pre-existing render primitives. |
| `test/host/` | DroidNet. Host harness. |
| `src/main.cpp`, `include/config.h`, `include/matrices.h` | Neil Hutchison and contributors, plus the small additive hooks listed in the README. |
| `include/functions.h`, `include/preamble.h` | Downstream (C2B5 collection, not from Neil) — artifacts of the PlatformIO restructuring; upstream is a flat Arduino sketch. Contents are prototypes of Neil's functions. |
| `visualizer/` | Downstream browser preview tool (from the C2B5 collection, not from Neil). |

## Next steps

1. **Done:** bench-compile for AVR and read the flash/SRAM numbers — `test/host/run.sh` does this on
   every run for both configs; see README, "It fits". Nothing to re-measure by hand, and no
   `CONTRACT_SLIM`-style escape hatch to reach for if the numbers move — see "The animation VM: modes
   are data now" in README for why.
2. Bench-test on a bare PSI Pro, not in a droid: native modes first (prove the fork really is
   additive), then a live `!P*A` cue, then a seeded clock, then a pushed score.
3. Only then anywhere near a droid.
