# DroidNet fork notes — PSI Pro

Working notes for the fork itself. For attribution, the license situation, and the hardware
warning, read [README.md](README.md) first — none of that is repeated here.

## What this is

An additive fork of Neil Hutchison's PSI Pro firmware (<https://github.com/nhutchison/PSIPro>) that
implements the DroidNet **Driveable-Animation Contract**, so Cantina Studio can drive the 48-LED PSI
matrix with arbitrary RGB, beat-locked accents, a host-seeded beat clock, and autonomous board-side
section playback. Every native JawaLite `<addr>T` / `A` / `D` / `xPy` command and the I2C intake keep
working exactly as they did.

- **Role:** the PSI board of the three-board Cantina dome light-show family (RSeries Logics, PSI Pro,
  Flthy HPs). This is the tightest of the three and the pacing item.
- **MCU/toolchain:** ATmega32U4 (Sparkfun Pro Micro), FastLED. `platformio.ini` is checked in and
  builds out of the box (`pio run`). It links at 26,946 B of 28,672 B (94.0%) — and only fits
  because of CONTRACT_SLIM + the codegen flags + PSI_NOINLINE together. See README, "Building".
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

That is the entire verification story. There has been **no AVR compile and no bench flash**. Flash
headroom on the 32U4 (~28 KB usable; the contract layer is estimated at 3-4 KB) is the open risk, and
`CONTRACT_SLIM` in `include/config.h` is the — also untested — escape hatch if the linker overflows.

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

1. Bench-compile for AVR and read the flash/SRAM numbers. Everything else waits on this.
2. If it overflows: enable `CONTRACT_SLIM` and re-measure.
3. Bench-test on a bare PSI Pro, not in a droid: native modes first (prove the fork really is
   additive), then a live `!P*A` cue, then a seeded clock, then a pushed score.
4. Only then anywhere near a droid.
