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
  the build with the contract layer) links at 28,286 B of 28,672 B (98.7%). `pio run -e PSIPro-i2c`
  links at 16,998 B (59.3%): it has the I2C intake and **no contract layer** (`PSI_NO_CONTRACT`),
  because the two do not fit this part together (29,954 B). `run.sh` stage 9b fails a build over
  28,416 B (serial-only) or 28,160 B (I2C), under 256 B or 128 B of worst-case stack headroom, or
  with any soft-float routine linked. R1 is built without six contract looks
  (`PSI_NO_STRIP_LOOKS`: comet, chase, wipe, gradient, colorcycle, twinkle draw as scan, sparkle or
  solid), which are 1,204 B: with them the image is 29,490 B. The total also includes mode 22
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
- On the R2 line (`lighting-r2`): the shared core at revision CA2, contract v1.4, release 2:
  `vu`, the meter moved by the beat pump, and stand-ins for `logic` and `projector`. See "The R2
  line: core CA2" and "`vu` (Task F5)" below.
- Shared beat-brightness envelope (`envBright`) — identical across all three forks.
- Pre-existing upstream `buildCommand()` out-of-bounds write — fixed in `src/main.cpp` (bounds-check
  before the write, terminator clamped). Offered back to Neil.

Fork head: `main` in this repository. The baseline import is commit `11d69a3`; every commit after it
is the DroidNet layer (`git log --oneline 11d69a3..HEAD`).

## Verification

```bash
bash test/host/run.sh
```

What it runs, in order: the shared core's unit tests, its region test (every shared look by itself, no
part of a strand lit more than three times a second) and its fuzz model (and that the shared files
are the commit in `src/contract/CORE_PIN`); the WDP announce, for each build; the animation VM's
bytecode; 36 golden captures of the native modes (35 of them on the I2C build too); 102 timelines
against upstream v1.7 on the same mocks, on both builds, and 447 more on the I2C build with a
command from inside a `show()`; the contract scenarios
(`test_timeline.cpp`: the firmware's own `src/main.cpp`, scripted wire lines, assertions on the
frames it latches): 124 on the default build, 6 on the build without the contract layer, 126 on each
of two builds no environment links (every look; the contract with I2C); contract S9 and S11 as a
property of the default build under 59 hostile scripts and 1,500 random streams
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
- **Native take-over:** a native `T` or `D` command addressed to the PSI ends the show and runs (a
  `D` or an `A` for another board does not). A mode whose program has ended and holds its last
  frame (the lightsaber battle under `alwaysOn`) plays again when it gets the panel back.
- **Score:** 16 sections, `b=` and every-N accents carried, overflow is a bad load.
  `V:n=,k=` checks the load; `Q:r=<n>` is answered routed (`;S0!PFq:ver=1.3,rel=1,cap=,n=,ok=,bpm=`).
- **Limits (S9, S11, core CA1.7):** tempo 30 to 300 BPM; accents and the pump no faster than every
  340 ms. Every frame the contract draws is measured in `FastLED.show()` by brightest channel and
  by luminance, as the driver will scale it, and passes the core's field gate (`contractFieldAsk`,
  `contractFieldTell`): at most three dark-to-lit swings of the whole field a second in either
  measure, with a tenth of the ceiling for a step, and never above the ceiling. A frame the gate
  holds is not sent (the hook returns -1 and the panel keeps what it shows); it is not scaled
  down. A frame of another look or colour waits for the look gate, 170 ms after the field's last
  change: asked before the frame is drawn (`contractLookAllowed`), taken by the frame that is sent,
  by its brightest LED (`contractLookGateAt`: 4 levels or more). A section waits 170 ms after the
  last look change (`contractSectionMayApply`); the same look applied again does not restart, nor
  does the same look in another colour, which is recoloured in place (`contractLookContinues`).
  `L`, `B`, a live look's `b=` and a section's go through the core's holds (`contractLevelOffer`,
  `contractBrightOffer`; `contractHoldsDue`, called on a pass whose frame shows them, which with
  the meter up is a pass that redraws it): up at most once in 340 ms, in one window for the two,
  down at once. The meter with no `L` redraws every 170 ms at the fastest; a scan laps in a second
  or more (the core's `fxScanStepMs`: 56 ms a column at the fastest on 10 columns). The gate is
  the whole field's: a part of the panel can still light twice inside 340 ms (`docs/BENCH_R1.md`,
  the known limits).
- **Native modes a show may select (S10):** 0 to 22 and 92, without 2, 3 and 5. They run as stock:
  their frames are never altered. All of them draw at `brightness()`, so all of them dim with
  `b=`/`B`. Their edges ask the gate (`contractNativeMayStart`): before a `native:<n>` starts,
  before the board's own look gets the panel back, and before a look that started dark is given
  brightness. What the gate is told is what the layer measures, since every frame passes its
  hook: for a mode whose field does not swing fast, each frame as it is (`contractFieldTell`;
  the level the core's recipe would tell, the brightness, is not what most modes put on the
  field: 3 % to 100 % of it); for Leia (6) and Knight Rider (15), once, at the first frame with an
  LED at 4 levels (`contractNativeStarted`, level and luminance), with their darkest frame told
  before whoever draws next; for a mode
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
| flash | 28,286 B of 28,672 (98.7%); gate 28,416 | 16,998 B (59.3%); gate 28,160 |
| SRAM | 1,632 B of 2,560 (63.8%) | 1,217 B (47.5%) |
| worst-case stack, sound bound | 444 B of 928 B | 312 B of 1,343 B |
| stack headroom | 484 B; floor 256 | 1,031 B; floor 128 |
| soft-float routines linked | none | none |
| shared core | CA1.7, RSeries `fe1c8ad` | not linked |

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

## The R2 line: core CA2 (contract v1.4; never flashed)

The shared core is at revision CA2 (RSeries `9991f5a`; see "Core revision CA2" in the RSeries fork's
`docs/CHANGING_THE_CORE.md`): contract v1.4, release 2. The query reply says `ver=1.4,rel=2`, the WDP
caps say `contract.1.4s` (`contract.1.4` for the all-looks build). The I2C build is unchanged and
links no contract layer.

The three new ids parse and draw stand-ins, as v1.4's table has them on the PSI:

- `logic` and `projector` draw `sparkle`, in every build (`_lookHere()`); as an overlay, solid.
- `vu` keeps its own id. Wherever the layer tests `CE_METER` it tests `CE_VU` too (the meter case of
  `_renderLook()`, and the pass that takes the level holds while the meter is up). Not pumped it
  draws exactly as `meter`; pumped, see "`vu` (Task F5)".
- None of them is an accent (`accentEffectAllowed()`): a `P` naming one overlays solid.

Stage 6d holds the three, not pumped, to `sparkle` and `meter` frame for frame; the timeline scenario
`v14_ids_draw_their_stand_ins` checks them in every build. The 36 native captures, the 549 upstream
timelines and the property stage's output are what they were in R1.

| | R1 (`0e87a91`, core CA1.7) | Core CA2 alone | Core CA2 and the stand-ins |
| --- | --- | --- | --- |
| PSIPro flash | 28,286 B | 28,306 B | 28,328 B (88 B under the gate of 28,416) |
| PSIPro SRAM | 1,632 B | 1,632 B | 1,632 B |
| PSIPro stack headroom (sound bound) | 484 B | 484 B | 484 B (floor 256) |
| PSIPro-i2c flash / SRAM / headroom | 16,998 B / 1,217 B / 1,031 B | the same | the same |

The core's new functions are inline and nothing here calls them; its names and version cost 20 B.
The stand-ins cost 22 B, all of it in `loop()` (the meter case's new label alone is 12 B). Measured
on clean trees with `pio run -e PSIPro` and `test/host/stack_report.py`; host-tested and
cross-compiled, not run on a board.

### `vu` (Task F5)

v1.4 N10 to N12 in `src/contract/ContractPSI.h`. While the unit is pumped (the clock runs, `am=` and
`m=` are not 0, no overlay is up) a `vu` look offers `envBright(255, m, amount)` (the core's vector
group `vuLevel`) to the core's level hold every pass, in `contractLoopTick()`; the hold takes it on
the next pass that redraws the meter (up at most once in 340 ms, in the window `B` and `b=` share;
down at once), and the meter draws `state.level` as it draws a level from `L`. The brightness stays
at the look's level. Not pumped, `vu` is `meter`. An `L` that arrives while the pump feeds `vu` is
dropped (N12). Since the R2 review (below): the pump is a level feed like `L`, no accent overlay
starts over a pumped `vu`, and the window also counts a frame whose meter level is up.

One thing the spec did not say, found by the property test: the level's window has to run from
the frame that shows a rise, not from the pass that took it. The look gate holds a look's first
frames (and a change of colour's), `VUMeter()` opens a look with a black frame, and the field gate
holds a frame it would count as an onset; in each case the bars light a pass or three after the
rise was taken, and the next beat's rise, 340 ms after the take, lit a band of the panel 312 ms
after it had lit (at the owner's brightness 20, every round). So `contractFrameScale()` moves the
window to a sent frame whose field level is above the last sent one, while a pumped `vu` is up.
The window only ever moves later. (`meter` fed by `L` has the same exposure in principle; no
script has shown it, and it is R1's behaviour, so it is left as it is.)

Readings to know: `s=` paces the redraw as it does for a fed meter (`s=255` every pass, `s=128`
every 210 ms, late on most beats at 120 BPM: the middle of the bars lit on 16 of 24 beats at
`s=128`, 24 of 24 at `s=255`); a pause (`C:bpm=0`) stops the clock, so `vu` is the meter: since the
R2 review it holds the pump's last level for 2 s, as after a last `L`, and then walks.
At 176 BPM (beats 341 ms apart, the window 340) a beat's rise is often taken a pass late, when the
level has fallen: the bars rise lower.

Host scenarios (stage 6c; owner brightness 200, `m=255`, `s=255`, 12 s; a row is lit at half its
LEDs and a scale of 100, dark at a tenth): no row lit more than three times in a second or twice
inside 340 ms in any of them.

| Scenario | Middle row (3) lit | Closest two onsets of any row | Most in a second |
| --- | --- | --- | --- |
| `a_pumped_vu_at_30_bpm` | 6 of 6 beats | 2,002 ms | 1 |
| `a_pumped_vu_at_120_bpm` | 24 of 24 | 494 ms | 3 |
| `a_pumped_vu_at_176_bpm` | 9 (bottom row 20) of 35 | 364 ms | 3 |
| `a_pumped_vu_at_177_bpm` (every second beat, S2) | 17 of 17 | 676 ms | 2 |
| `a_pumped_vu_at_300_bpm` (every second beat) | 29 of 30 | 364 ms | 3 |
| `a_pumped_vu_under_a_seek_every_50_ms` (6 s) | 1 (top rows 11 and 13) | 364 ms | 3 |
| `a_pumped_vu_under_b_between_0_and_200` (6 s) | 3 | 364 ms | 2 |

`a_pumped_vu_rests_at_255_minus_m`: at `m=200`, four rows of six lit 60 ms after a beat (211 of
255) and one 1,900 ms after it (55), three beats of three. `vu_not_pumped_is_the_meter`: the same
frames as `meter`, hash for hash, with `am=`/`m=` and no clock, with a clock and no `m=`, and fed by
`L` and then not. `l_during_a_pump_changes_nothing`: 291 frames, the same hash with and without an
`L` every 37 ms. The property test has nine profiles for `vu` (the five tempos, the seek, `B`, `L`
at 176 BPM, the slowest redraw with a scored accent) and turns half the random streams' `meter`
lines into `vu` by a value already drawn, so the streams are R1's: the profiles keep every region to
364 ms and three onsets a second, the random streams to 340 ms (both as R1). simavr, pumped `vu` at
120 BPM, `s=255`: 0 ppm, longest pass 5.4 ms (the longest of any look is still the meter, 7.8 ms).

Flash, found in `ContractPSI.h` with no change in behaviour, one commit each, measured on clean
trees (`pio run -e PSIPro`; a dirty tree adds 6 B of flash and 12 B of SRAM through `-dirty`):

| Commit | What | PSIPro flash |
| --- | --- | --- |
| CA2 and the stand-ins (Task F3) | | 28,328 B |
| a scored entry's `m=` and `am=` copied as parsed | they are 0 without their flags (`ContractParams`), so the two tests went | 28,270 B (-58) |
| `_measure()` scales the channels in a loop of three | the frame read as bytes, LED after LED | 28,226 B (-44) |
| `vu` | the pump on the level, the L guard, the window from the frame that shows a rise | 28,330 B (+104) |

| | R1 (`0e87a91`) | CA2, stand-ins (Task F3) | With `vu` |
| --- | --- | --- | --- |
| PSIPro flash | 28,286 B | 28,328 B | 28,330 B (86 B under the gate of 28,416) |
| PSIPro SRAM | 1,632 B | 1,632 B | 1,634 B |
| PSIPro stack headroom (sound bound) | 484 B | 484 B | 482 B (floor 256) |
| PSIPro-i2c flash / SRAM / headroom | 16,998 B / 1,217 B / 1,031 B | the same | the same |

Tried and not kept (each against the stand-ins' 28,328 B, measured on dirty trees less 6 B): the
pump with two `envBright()` calls, one for `vu` and one for the rest, +140 B, against +50 B for one
call with its base chosen (before the `L` guard, +8 B, and the window from the shown frame, +46 B);
the same with the offer made before the hold is taken, so that no `L` could get between, +178 B; a
second hold taken just before the meter draws, with the brightness worked out again, +304 B, and it
did not close the window problem above. `_speedToDelay()` in 16 bits (exact for every `s`) saves
2 B and was left out.
Host-tested, simulated and cross-compiled; not run on a board. The bench rows are
`docs/BENCH_R2.md`.

### The R2 review of `vu`, and its fix (2026-11-21)

An independent review found five defects in `vu`, as failing tests (`test/host/review_r2.inc`,
`review_r2_profiles.inc`, run by `run.sh`'s last stage, `[R2 review]`). Each is closed:

| # | Finding | Closed by |
| --- | --- | --- |
| 1 | A scored `vu` with an accent relit the bottom rows 208 ms after the accent, five times a second | The rule below: no accent over a pumped `vu` |
| 2 | A pause, a resume, `am=0` or `m=0` handed the rows between the pump and the walk with no window (286 and 208 ms relights) | The pump feeds the meter as `L` does (`_levelFeed()` every pumped pass): stopped, the meter holds the pump's last level for 2 s, then walks; started from the walk, the bars go dark and the level's window starts there |
| 3 | The window "from the frame that shows a rise" missed higher bars shown at a lower brightness | A sent frame is a rise when its field level or its meter level is up |
| 4 | An `L` just after `A:i=meter` (one burst) was dropped: the guard read the last tick's state | The guard reads the state as it is now (`_vuPump()`) |
| 5 | An `L` during an accent over a pumped `vu` was shown | The same guard, which holds under an overlay; and no overlay now starts over a pumped `vu` |

**A v1.4 rule for the PSI (a narrowing; for the spec and Studio):** *no accent overlay starts over
a pumped `vu`. While the unit's look is `vu` and it is pumped (the clock runs, `am=` and `m=` are
not 0), a scored entry's `ae=` does not fire and a live `P` is ignored. Over a `vu` that is not
pumped, which is the meter, both work as on the meter.* The entry keeps its `ae=` (verb `V` is
unchanged) and fires again once the pump stops. Why a rule and not a mending: the overlay lights
the bars, and its end hands them back to a pump whose level is still up. With fixes 2 to 5 and the
window rule but without this rule, 33 of 90 scored accents (the owner's 20 and 200; `m=` 128, 200,
255; 60, 90, 120, 150 and 176 BPM; `flash`, `solid`, `pulse`) relit a row inside 340 ms; R1's meter
under the same lines does in 26 of 90 (rows are a tenth of the panel, under S12's quarter). Studio
plans no accent over `vu`.

**A reading (for the spec):** N12's "not pumped, `vu` is `meter`" is kept with the pump counted as
a level feed: the meter after the pump is the meter after a last `L`.

What stays as it was, measured with the review's row observer (lit at half the owner's ceiling):
300 random streams of pauses, resumes, `am=`/`m=` toggles, `B`, `L` and colour lines at a pumped
`vu` relight no row inside 340 ms (none for R1's meter either). With `P` lines in the mix, 4 of
300 do, the same 4 of 300 as R1's meter; with switches between `vu` and `meter`, 21 of 300 (18
starting from the meter): R1's known limit for a part of the panel (`docs/BENCH_R1.md`).

Flash, each recovery its own commit, measured on clean trees, held to the code it replaced by a new
stage, 6f (`test/host/test_r2fix_recoveries.cpp`: the parser's side for every line, and `L` and
`M:v=show` end to end). Each drops the test of a flag whose value is the default without it.

| Commit | What | PSIPro flash |
| --- | --- | --- |
| Task F5, merged (`be6e9d2`) | | 28,330 B |
| verb `L`'s level taken as parsed | `pr.hasLevel ? pr.level : 0` is `pr.level` | 28,316 B (-14) |
| `M:v=show`'s watchdog taken as parsed | `pr.hasWd ? pr.wd : 0` is `pr.wd` | 28,298 B (-18) |
| the `i=` flag not tested where `i=` is | `hasEffect` is `effect != CE_NONE`; `CE_NONE` is neither native nor an accent | 28,274 B (-24) |
| the fix | the five findings, the rule above | 28,372 B (+98) |

| | Task F5 | After the review's fix |
| --- | --- | --- |
| PSIPro flash | 28,330 B | 28,372 B (44 B under the gate of 28,416) |
| PSIPro SRAM | 1,634 B | 1,635 B |
| PSIPro stack headroom (sound bound) | 482 B | 482 B (floor 256) |
| PSIPro-i2c flash / SRAM / headroom | 16,998 B / 1,217 B / 1,031 B | the same |

The review's own fix measured 28,454 B. What made room here: one `_levelFeed()` for `L` and the pump
(two inline copies of the 4-byte time cost 44 B more), the pump as a feed in place of a transition
detector (`set_delay()` on the way to the walk and a static "was pumped"), the overlay rule in
`_fireAccent()` (2 B), a `g_vuPumped` that holds under an overlay (no code: the review's overlay
clause in the window rule), and the three recoveries above. simavr, pumped `vu` at 120 BPM: 0 ppm, longest pass 5.5 ms. Host-tested,
simulated and cross-compiled; not run on a board.

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
