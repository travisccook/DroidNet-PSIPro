# Review probes, 2026-11-15

These are the probes written during the lighting review of 2026-11-15 and while planning the fixes on
2026-11-16. They are **unreviewed review probes**, kept so the findings can be reproduced. They are not part of
`test/host/run.sh`. Part 0 of the plan
`DroidNetSignalBooster/docs/superpowers/plans/2026-11-16-lighting-firmware-psi.md` promotes them into the harness;
when that is done this directory can be deleted.

Nothing here is built by the firmware. Build products must stay out of the repository: pass an output directory
outside it, or delete `.pio/` afterwards.

## What needs installing

- Host probes: `g++` (C++17). The repo's own mocks in `test/host/native_mocks/` are used as they are.
- AVR probes: PlatformIO and its simavr package.

```bash
python3 -m venv ~/pio-venv && ~/pio-venv/bin/pip install platformio
~/pio-venv/bin/pio pkg install -g --tool platformio/tool-simavr
# simavr is then ~/.platformio/packages/tool-simavr/bin/simavr
```

- The differential needs Neil Hutchison's upstream sketch, which is not vendored here:

```bash
git clone --depth 1 https://github.com/nhutchison/PSIPro.git /tmp/PSIPro   # reviewed at 5e80597 (v1.7)
```

## Files

| Path | What it demonstrates |
| ---- | -------------------- |
| `scenarios/scen.cpp` | The firmware (`src/main.cpp` verbatim) on the host mocks with a scripted command timeline; prints a frame log. Fourteen scenarios, one per review finding (see the table below) |
| `scenarios/results.txt` | Its output on the firmware as reviewed |
| `timeline/timeline_scenarios.inc` | 35 scenarios for contract v1.3 as amended on 2026-11-16, for `test/host/test_timeline.cpp` (the harness moved there in the plan's Task 3). On the firmware as reviewed 31 fail (`timeline/result_current_firmware.txt`) |
| `updiff/diffdrv.cpp` | Upstream `PSIPro.ino` and the fork, each built on the fork's mocks, dumping every `show()` frame (the `Wire` stub is now `test/host/native_mocks/Wire.h`) |
| `updiff/build.sh`, `updiff/rundiff.sh`, `updiff/matrix.sh` | Build both, compare one timeline, run the 71 timelines the review ran |
| `updiff/results.txt` | 67 same (51 frame for frame, 16 once repeated frames are collapsed), 4 different |
| `test/host/diff_upstream.cpp`, `test/host/diff_upstream.sh` | Moved out of here by the plan's Task 2: the same comparison as a `run.sh` stage with an expectation per row (77 timelines), against upstream vendored at `test/host/upstream-v1.7/` |
| `simavr/millis_probe/` | The WS2812 driver alone behind the firmware's 26 ms gate: `millis()` against Timer1 for a range of compute times before `show()` |
| `test/simavr/` | The firmware `millis()` probe, the waveform probe and the CPU bench moved there in the plan's Task 4 and run from `test/host/run.sh` |
| `proto/millis_exact.diff` | The exact `millis()` credit for `include/FastLED.h` (plan Task 6) |
| `proto/golden_script.diff` | `golden_native.cpp --cmd` takes a script `<ms>@<line>;<ms>@<line>` (plan Task 3) |
| `proto/psi-r1-prototype.diff` | An unreviewed prototype of the PSI-side R1 changes that need no core change. Applies to `abba59d` with `git apply` |
| `proto/ContractPSI.v13.h` | The render layer ported to the CA1 core's `ContractUnitState`: all of R1 except three later spec items (`B` only in show mode, the take-over when armed, the shared flash, pulse and scan timing). Compiled for the ATmega32U4 and run against the scenarios: all pass (`proto/result_timeline_v13_port.txt`) |
| `proto/result_*.txt` | The probes, the timeline scenarios and the differential re-run on the prototype |

`scenarios/scen.cpp` scenario numbers:

| n | Finding |
| -- | ------- |
| 1 | The first cue jumps brightness from the pot's 9 to 200 |
| 2 | A live `d=` deadline ends a later pushed show |
| 3 | A meter base look with a per-beat accent |
| 4 | `bpm=600` with `m=255` pumps the whole panel at 10 Hz |
| 5 | `M:v=show` after `X` replays the last look |
| 6 | Ten sections pushed, the table holds eight |
| 7 | Native `0T1`, `0D`, `0T6` cannot take an armed panel back |
| 8 | A pushed score mixes `native:` modes and contract effects (this works) |
| 9, 10 | `X` after `native:15` leaves Knight Rider running |
| 11 | A bare re-anchor `C` fires an accent mid-beat |
| 12 | `b=` on a scored line is dropped |
| 13 | `i=swipe` and `i=cylon` are not effect names |
| 14 | A resent show plays on the previous show's clock |

## Build and run

All commands run from this directory. `OUT` is any directory outside the repository.

```bash
OUT=$(mktemp -d)

# scenarios
g++ -std=gnu++17 -O1 -fsigned-char -w -I .. -I ../native_mocks -I ../../../include '-DWDP_FW="x"' \
    scenarios/scen.cpp -o "$OUT/scen"
for n in 1 2 3 4 5 6 7 8 9 10 11 12 13 14; do "$OUT/scen" $n; done

# the v1.3 scenarios, through the harness in test/host
g++ -std=gnu++17 -O1 -fsigned-char -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    -I .. -I ../native_mocks -I ../../../include '-DWDP_FW="abcdef0"' \
    '-DTIMELINE_SCENARIOS="review-2026-11-15/timeline/timeline_scenarios.inc"' \
    ../test_timeline.cpp -o "$OUT/tl"
for s in $("$OUT/tl" --list); do "$OUT/tl" "$s"; done

# upstream against the fork
BIN=$(bash updiff/build.sh /tmp/PSIPro "$OUT/updiff")
BIN=$BIN bash updiff/matrix.sh
```

`updiff/build.sh` copies `PSIPro.ino` to `up_main.cpp` in `OUT`, puts the fork's prototype header
(`include/functions.h`) ahead of it (an Arduino sketch gets its prototypes generated; a plain C++ build does not) and
supplies a three-line `Wire` stub. Upstream's own `config.h` and `matrices.h` sit beside it, so the upstream build
uses upstream's settings.

The driver-only probe is a PlatformIO project. Build in place, run, then remove `.pio/`:

```bash
SIM=~/.platformio/packages/tool-simavr/bin/simavr
(cd simavr/millis_probe && pio run && timeout 300 "$SIM" -m atmega32u4 -f 16000000 .pio/build/probe/firmware.elf;
  rm -rf .pio)
```

simavr prints what the sketch writes to `Serial1`. The probe never exits, so `timeout` ends the run. The other
probes are in `test/simavr/` (README there).

## Results as recorded

- As first recorded with the firmware probe (now `test/simavr/millis`): `millis()` runs 1.6% fast for idle swipe,
  solid, comet, gradient,
  colorcycle and `native:1`, and 2.3% slow for twinkle, wipe and chase.
- `proto/result_fwprobe_exact_credit.txt`: with `proto/millis_exact.diff` every look is within 126 ppm, which is one
  millisecond in the 8 s window.
- The waveform probe (now `test/simavr/wave`): T0H 250 ns, T1H 750 ns, 1250 ns bits (1437.5 ns at a byte boundary),
  the wire bytes are
  the frame's bytes in G, R, B order, and two `show()` calls with nothing between them are 107 µs apart.
- The bench (now `test/simavr/bench`): `show()` 1607 µs, twinkle 6784 µs a frame, wipe, chase and gradient about 800 µs.
- `proto/result_timeline_prototype.txt`: the first prototype (before the core existed) against the first draft
  of the scenarios; `proto/result_timeline_v13_port.txt`: the port on the CA1 core, every scenario passing.
