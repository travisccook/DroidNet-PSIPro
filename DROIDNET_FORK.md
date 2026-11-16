# DroidNet fork notes — PSI Pro

Working notes for the fork itself. For attribution and the hardware warning, read
[README.md](README.md) first — none of that is repeated here.

## What this is

An additive fork of Neil Hutchison's PSI Pro firmware (<https://github.com/nhutchison/PSIPro>) that
implements the DroidNet **Driveable-Animation Contract**, so Cantina Studio can drive the 48-LED PSI
matrix with arbitrary RGB, beat-locked accents, a host-seeded beat clock, and autonomous board-side
section playback. Every native JawaLite `<addr>T` / `A` / `D` / `xPy` command still works; the
README lists the few places the board differs from upstream v1.7 (three defaults, the lightsaber
with a timing, a 200 µs latch gap). The I2C intake is an opt-in build, and in this release that
build has no contract layer.

- **Role:** the PSI board of the three-board Cantina dome light-show family (RSeries Logics, PSI Pro,
  Flthy HPs). This is the tightest of the three and the pacing item.
- **MCU/toolchain:** ATmega32U4 (Sparkfun Pro Micro), clean-room WS2812 driver (`include/FastLED.h`).
  `platformio.ini` is checked in and builds out of the box. `pio run` (serial-only, shipped default,
  the build with the contract layer) links at 27,866 B of 28,672 B (97.2%). `pio run -e PSIPro-i2c`
  links at 16,888 B (58.9%): it has the I2C intake and **no contract layer** (`PSI_NO_CONTRACT`),
  because the two do not fit this part together (29,536 B). `run.sh` stage 9b fails a build over
  28,416 B (serial-only) or 28,160 B (I2C), under 256 B or 128 B of worst-case stack headroom, or
  with any soft-float routine linked. R1 is built without six contract looks
  (`PSI_NO_STRIP_LOOKS`: comet, chase, wipe, gradient, colorcycle, twinkle draw as scan, sparkle or
  solid), which are 1,242 B: with them the image is 29,108 B. The total also includes mode 22
  ("processing sweep"), a fork-added demo animation that is not one of the 23 upstream modes — it
  exists to measure the marginal cost of a new one: 94 B. All 23 upstream modes (0-21
  and 92) ship in both builds; every upstream mode's animation except the four deliberate native
  holdouts (swipe/VUMeter/allON/allOFF) is PROGMEM bytecode played by `include/psi_vm.h`'s
  interpreter (the contract effects remain parametric C++ renderers; CE_SPARKLE via a hand-written
  shim in psi_vm.h). See README, "Building" and "The animation VM: modes are data now".
- **Seeded from:** the private C2B5 droid working collection @ `2fbd4023` (subdir `PSIPro`),
  2026-07-12, git-tracked files only. That collection is where the PlatformIO restructuring of Neil's
  flat Arduino sketch and the `visualizer/` came from.
- **GitHub remote:** <https://github.com/travisccook/DroidNet-PSIPro> (public).

## Status

**Code complete, host-verified, never run on hardware.**

- Live cues (verbs A/P/C/B/L/X/M/Q) — implemented in `src/contract/ContractPSI.h`, on the shared
  core's unit state and lifecycle helpers.
- Score push and autonomous board-side section playback, checked by verb V — implemented; the table
  holds 16 sections keyed to absolute beat numbers.
- Six parametric effects (comet, chase, wipe, gradient, colorcycle, twinkle) — implemented, rendered
  across the panel's 10 columns as a 1-D strand, and NOT in R1's image: no environment links them
  (they do not fit), a line naming one draws a stand-in, and the host suite keeps running them.
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
are the commit in `src/contract/CORE_PIN`); the WDP announce, for each build; the animation VM's
bytecode; 36 golden captures of the native modes (35 of them on the I2C build too); 78 timelines
against upstream v1.7 on the same mocks, on both builds; the contract scenarios
(`test_timeline.cpp`: the firmware's own `src/main.cpp`, scripted wire lines, assertions on the
frames it latches): 93 on the default build, 5 on the build without the contract layer, 95 on each
of two builds no environment links (every look; the contract with I2C); contract S9 and S11 as a
property of the default build under 36 hostile scripts and 1,500 random streams
(`test_property.cpp`); the scale and HSV oracles; the cross-compile of both builds with the flash,
stack and no-float gates; and five simavr probes (`test/simavr/README.md`). There is no mock of the
board API.

**Status: never run on hardware.** No image from this tree has been flashed to a PSI Pro. The bench
list, and the place to record it, is `docs/BENCH_R1.md`; until it is filled in, every claim here is
about the host, the cross-compiler and the simulator.

## Contract v1.3 on the PSI (first release)

The default, serial-only build. The I2C build has no contract layer.

- **Brightness:** the ceiling is the owner's brightness when a show arms; `b=`/`B` are fractions of
  it; `B` only in a show. A `3P` during a show is kept for the next arm and the give-back.
- **Lifecycle:** `M:v=show` arms and keeps the swipe until the first `C`, live `A` or `P`; `X` is
  dark until `M:v=idle` or a look, in a show or not; `M:v=idle`, the watchdog (`wd=`) and a failed
  `V` return to the swipe. A pulse with no show overlays the native look and returns to it.
  Every return to the board's own look waits until an onset and a change of look are allowed
  (340 ms after the field last lit or got brighter, 170 ms after its last change), the panel
  keeping what it shows.
- **Native take-over:** a native `T` or `D` command addressed to the PSI ends the show and runs.
- **Score:** 16 sections, `b=` and every-N accents carried, overflow is a bad load.
  `V:n=,k=` checks the load; `Q:r=<n>` is answered routed (`;S0!PFq:ver=1.3,rel=1,cap=,n=,ok=,bpm=`).
- **Limits (S9, S11, core CA1.5):** tempo 30 to 300 BPM; accents and the pump no faster than every
  340 ms. Every frame the contract draws is measured in `FastLED.show()` by brightest channel and
  by luminance and passes the core's field gate (`contractFieldAsk`, `contractFieldTell`): at
  most three dark-to-lit swings of the whole field a second in either measure, with a tenth of the
  ceiling for a step, and never above the ceiling. A lit frame of another look or colour waits
  for the look gate (`contractLookGate`, 170 ms after the field's last change); a section waits
  170 ms after the last look change (`contractSectionMayApply`); the same look applied again does
  not restart (`contractLookSame`).
- **Native modes a show may select (S10):** 0 to 22 and 92, without 2, 3 and 5. They run as stock:
  their frames are never altered. All of them draw at `brightness()`, so all of them dim with
  `b=`/`B`. Their edges ask the gate (`contractNativeMayStart`): before a `native:<n>` starts,
  before the board's own look gets the panel back, and before a look that started dark is given
  brightness. What the gate is told is what the layer measures, since every frame passes its
  hook: for a mode whose field does not swing fast, each frame as it is (`contractFieldTell`;
  the level the core's recipe would tell, the brightness, is not what most modes put on the
  field: 3 % to 100 % of it); for Leia (6) and Knight Rider (15), once, at their first lit frame
  (`contractNativeStarted`), with their darkest frame told before whoever draws next; for a mode
  off the list that a command started, nothing, and the field is taken as dark and just lit when
  the contract draws over it (`contractNativeEnded`, `known = false`). `L` feeds the contract's
  `meter` look only, never a native VU mode. The scenario
  `s10_native_looks_are_classified_by_their_stock_field_swing` measures every listed mode as
  stock (brightest channel and Rec.709 luminance): 6 and 15 move a lit area under a quarter of
  the field, the rest never light the field twice inside 340 ms, and 2, 3 and 5 measure as the
  fast full-field swings they are.
- **Looks:** solid, off, flash, pulse, scan, sparkle, rainbow, meter and `native:<n>`. Comet, chase
  and wipe draw as scan, twinkle as sparkle, gradient and colorcycle as solid (as an accent: all
  solid).
- **WDP caps:** `droidnet.contract` and `contract.1.3s`. The I2C build announces no caps.

## R1 in numbers (2026-11-16; never flashed)

| | PSIPro (serial-only, the contract) | PSIPro-i2c (I2C, no contract) |
| --- | --- | --- |
| flash | 27,866 B of 28,672 (97.2%); gate 28,416 | 16,888 B (58.9%); gate 28,160 |
| SRAM | 1,618 B of 2,560 (63.2%) | 1,216 B (47.5%) |
| worst-case stack, sound bound | 463 B of 942 B | 312 B of 1,344 B |
| stack headroom | 479 B; floor 256 | 1,032 B; floor 128 |
| soft-float routines linked | none | none |
| shared core | CA1.5, RSeries `cea4921` | not linked |

`millis()` against Timer1 in simavr, 16 s a look (1 ms of `millis()` is 62 ppm), and the longest
single pass of `loop()`:

| Default build: look | ppm | | I2C build: mode | ppm |
| --- | --- | --- | --- | --- |
| idle swipe | -1 | | idle swipe | 0 |
| solid | +62 | | `0T6` Leia | -63 |
| solid, pumped | -1 | | `0T15` Knight Rider | 0 |
| scored scan with an accent | -1 | | `0T7` I heart U | 0 |
| sparkle | 0 | | `0T10` Star Wars scan | 0 |
| flash, pumped | -62 | | `0T13` disco ball | 0 |
| pulse | +62 | | `0T92` VU meter | 0 |
| rainbow | 0 | | `0T22` processing | 0 |
| `native:22` | +61 | | `0T19` lightsaber | +62 |
| meter | +61 | | `0T16` white | 0 |
| scan | -1 | | | |
| `native:1` | +62 | | | |
| `native:15` | 0 | | | |
| longest pass | 7.6 ms (meter) | | longest pass | 4.2 ms (VU meter) |

The gate in `run.sh` is 300 ppm and 20 ms. None of this has been measured on a board: the bench
checklist is `docs/BENCH_R1.md`.

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

1. **Done:** contract v1.3, first release, in the default build, inside the flash and stack gates.
   The I2C build carries no contract layer in R1, and six looks draw as stand-ins.
2. Bench-test on a bare PSI Pro, not in a droid, by `docs/BENCH_R1.md`: native modes first (prove
   the fork really is additive), then a live `!P*A` cue, then a seeded clock, then a pushed score,
   then the limits (full-field white at the ceiling on the supply in use), then the I2C image.
3. Only then anywhere near a droid.
