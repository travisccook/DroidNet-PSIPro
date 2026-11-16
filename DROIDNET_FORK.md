# DroidNet fork notes — PSI Pro

Working notes for the fork itself. For attribution and the hardware warning, read
[README.md](README.md) first — none of that is repeated here.

## What this is

An additive fork of Neil Hutchison's PSI Pro firmware (<https://github.com/nhutchison/PSIPro>) that
implements the DroidNet **Driveable-Animation Contract**, so Cantina Studio can drive the 48-LED PSI
matrix with arbitrary RGB, beat-locked accents, a host-seeded beat clock, and autonomous board-side
section playback. Every native JawaLite `<addr>T` / `A` / `D` / `xPy` command still works; the
README lists the few places the board differs from upstream v1.7 (three defaults, the lightsaber
with a timing, a 200 µs latch gap), and the I2C intake is an opt-in build.

- **Role:** the PSI board of the three-board Cantina dome light-show family (RSeries Logics, PSI Pro,
  Flthy HPs). This is the tightest of the three and the pacing item.
- **MCU/toolchain:** ATmega32U4 (Sparkfun Pro Micro), clean-room WS2812 driver (`include/FastLED.h`).
  `platformio.ini` is checked in and builds out of the box. `pio run` (serial-only, shipped default)
  links at 27,494 B of 28,672 B (95.9%); `pio run -e PSIPro-i2c` links at 27,942 B (97.5%), WDP
  device announce and contract v1.3 (first release) included. `run.sh` stage 9b fails a build over
  28,416 B (serial-only) or 28,160 B (I2C), under 256 B or 128 B of worst-case stack headroom, or
  with any soft-float routine linked. The I2C build leaves out six contract looks
  (`PSI_NO_STRIP_LOOKS`: comet, chase, wipe, gradient, colorcycle, twinkle draw as scan, sparkle or
  solid; README, "The I2C build has six fewer contract looks"), which is 1,158 B of its flash, and
  holds 12 score sections where the serial-only build holds 16. Both totals also include mode 22
  ("processing sweep"), a fork-added demo animation that is not one of the 23 upstream modes — it
  exists to measure the marginal cost of a new one: 94 B, both configs. All 23 upstream modes (0-21
  and 92) ship; every upstream mode's animation except the four deliberate native holdouts
  (swipe/VUMeter/allON/allOFF) is PROGMEM bytecode played by `include/psi_vm.h`'s interpreter (the
  contract effects remain parametric C++ renderers; CE_SPARKLE via a hand-written shim in
  psi_vm.h), and that is what makes both fit. See README, "Building" and "The animation VM: modes
  are data now".
- **Seeded from:** the private C2B5 droid working collection @ `2fbd4023` (subdir `PSIPro`),
  2026-07-12, git-tracked files only. That collection is where the PlatformIO restructuring of Neil's
  flat Arduino sketch and the `visualizer/` came from.
- **GitHub remote:** <https://github.com/travisccook/DroidNet-PSIPro> (public).

## Status

**Code complete, host-verified, never run on hardware.**

- Live cues (verbs A/P/C/B/L/X/M/Q) — implemented in `src/contract/ContractPSI.h`, on the shared
  core's unit state and lifecycle helpers.
- Score push and autonomous board-side section playback, checked by verb V — implemented; the table
  holds 16 sections (12 in the I2C build) keyed to absolute beat numbers.
- Six parametric effects (comet, chase, wipe, gradient, colorcycle, twinkle) — implemented, rendered
  across the panel's 10 columns as a 1-D strand (drawn as stand-ins by the I2C build).
- Contract v1.3, first release: see "Contract v1.3 on the PSI" below.
- Shared beat-brightness envelope (`envBright`) — identical across all three forks.
- Pre-existing upstream `buildCommand()` out-of-bounds write — fixed in `src/main.cpp` (bounds-check
  before the write, terminator clamped). Offered back to Neil.

Fork head: `main` in this repository. The baseline import is commit `11d69a3`; every commit after it
is the DroidNet layer (`git log --oneline 11d69a3..HEAD`).

## Verification

```bash
bash test/host/run.sh
```

What it runs, in order: the shared core's unit tests and its fuzz model (and that the shared files
are the commit in `src/contract/CORE_PIN`); the WDP announce; the animation VM's bytecode; 36
golden captures of the native modes; 78 timelines against upstream v1.7 on the same mocks; 89
contract scenarios on each build (`test_timeline.cpp`: the firmware's own `src/main.cpp`, scripted
wire lines, assertions on the frames it latches); contract S9 as a property of the firmware under
23 hostile scripts and 1,500 random streams (`test_property.cpp`); the scale and HSV oracles; the
cross-compile of both builds with the flash, stack and no-float gates; and five simavr probes
(`test/simavr/README.md`). There is no mock of the board API.

**Status: never run on hardware.** No image from this tree has been flashed to a PSI Pro. The bench
checklist is in the firmware plan (DroidNet Signal Booster repository,
`docs/superpowers/plans/2026-11-16-lighting-firmware-psi.md`); until it is recorded, every claim
here is about the host, the cross-compiler and the simulator.

## Contract v1.3 on the PSI (first release)

- **Brightness:** the ceiling is the owner's brightness when a show arms; `b=`/`B` are fractions of
  it; `B` only in a show. A `3P` during a show is kept for the next arm and the give-back.
- **Lifecycle:** `M:v=show` arms and keeps the swipe until the first `C`, live `A` or `P`; `X` is
  dark until `M:v=idle` or a look, in a show or not; `M:v=idle`, the watchdog (`wd=`) and a failed
  `V` return to the swipe. A pulse with no show overlays the native look and returns to it.
  Every return to the board's own look waits until an onset is allowed (340 ms after the field
  last lit or got brighter), the panel keeping what it shows.
- **Native take-over:** a native `T` or `D` command addressed to the PSI ends the show and runs. In
  the I2C build the command may arrive in the TWI interrupt; the show ends at the next loop pass.
- **Score:** 16 sections (12 with I2C), `b=` and every-N accents carried, overflow is a bad load.
  `V:n=,k=` checks the load; `Q:r=<n>` is answered routed (`;S0!PFq:ver=1.3,rel=1,cap=,n=,ok=,bpm=`).
- **Limits:** tempo 30 to 300 BPM; accents and the pump no faster than every 340 ms; live looks
  170 ms apart; every frame the contract draws passes the core's field gate in `FastLED.show()`
  (at most three dark-to-lit swings of the whole field a second, never above the ceiling).
- **Native modes a show may select:** 0 to 22 and 92, without 2, 3 and 5. They run as stock: their
  frames are never altered. All of them draw at `brightness()`, so all of them dim with `b=`/`B`.
  What the contract holds is their edges: the start of a `native:<n>` look and the hand-over back
  to the board's own look both wait until an onset is allowed, and a rise of the brightness it
  gives a native look is held like any relight. `L` feeds the contract's `meter` look only, never
  a native VU mode.
- **WDP caps:** `droidnet.contract` and `contract.1.3` (`contract.1.3s` from the I2C build).

## Layout

| Path | Owner |
| --- | --- |
| `src/contract/contract_core.h` | DroidNet. Byte-identical across the RSeries/PSI/Flthy forks — **never edited here**. It is changed in the RSeries fork and copied, with its tests, at the commit `src/contract/CORE_PIN` names (`run.sh` stage 1h). |
| `src/contract/ContractPSI.h` | DroidNet. PSI render layer; calls only pre-existing render primitives. |
| `include/psi_vm.h`, `include/FastLED.h` | DroidNet. The animation interpreter with every upstream mode's bytecode, and the clean-room WS2812 driver. |
| `test/host/`, `test/simavr/` | DroidNet. The host suite and the simavr probes. |
| `src/main.cpp`, `include/config.h`, `include/matrices.h` | Neil Hutchison and contributors, plus the small additive hooks listed in the README. |
| `include/functions.h`, `include/preamble.h` | Downstream (C2B5 collection, not from Neil) — artifacts of the PlatformIO restructuring; upstream is a flat Arduino sketch. Contents are prototypes of Neil's functions. |
| `visualizer/` | Downstream browser preview tool (from the C2B5 collection, not from Neil). |

## Next steps

1. **Done:** contract v1.3, first release, in both builds, inside the flash and stack gates.
2. Bench-test on a bare PSI Pro, not in a droid, by the checklist in the firmware plan: native modes
   first (prove the fork really is additive), then a live `!P*A` cue, then a seeded clock, then a
   pushed score, then the limits (full-field white at the ceiling on the supply in use).
3. Only then anywhere near a droid.
