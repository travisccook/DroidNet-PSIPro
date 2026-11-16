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
| `timeline/test_timeline.cpp` | The same idea as an assertion harness: one scenario per process, pass or fail. The plan's Task 3 moves it to `test/host/` |
| `timeline/timeline_scenarios.inc` | 27 v1.3 scenarios. On the reviewed firmware 25 fail (`timeline/result_current_firmware.txt`) |
| `updiff/Wire.h`, `updiff/diffdrv.cpp` | Upstream `PSIPro.ino` and the fork, each built on the fork's mocks, dumping every `show()` frame |
| `updiff/build.sh`, `updiff/rundiff.sh`, `updiff/matrix.sh` | Build both, compare one timeline, run the 71 timelines the review ran |
| `updiff/results.txt` | 67 same (51 frame for frame, 16 once repeated frames are collapsed), 4 different |
| `updiff/diff_upstream.cpp`, `updiff/diff_upstream.sh` | The same comparison as a pass/fail stage with an expectation per row (77 timelines), written for `test/host/` with upstream vendored at `test/host/upstream-v1.7/`; the plan's Task 2 moves them there |
| `simavr/millis_probe/` | The WS2812 driver alone behind the firmware's 26 ms gate: `millis()` against Timer1 for a range of compute times before `show()` |
| `simavr/fwprobe/` | The real firmware with one contract look at a time: `millis()` error per look |
| `simavr/check_millis.py` | Gate on the `fwprobe` log: `python3 simavr/check_millis.py <log> --limit-ppm 1000` |
| `simavr/bench/` | Microseconds for `beatPosAt`, the envelope, each effect's math and one `show()` |
| `simavr/wave/` | One frame traced on PD4 to a VCD; `check_wave.py` checks T0H, T1H, bit period, the bytes and the gap between frames |
| `proto/millis_exact.diff` | The exact `millis()` credit for `include/FastLED.h` (plan Task 6) |
| `proto/golden_script.diff` | `golden_native.cpp --cmd` takes a script `<ms>@<line>;<ms>@<line>` (plan Task 3) |
| `proto/psi-r1-prototype.diff` | An unreviewed prototype of the PSI-side R1 changes that need no core change. Applies to `abba59d` with `git apply` |
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

# timeline assertions (-I timeline finds timeline_scenarios.inc)
g++ -std=gnu++17 -O1 -fsigned-char -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    -I .. -I ../native_mocks -I timeline -I ../../../include '-DWDP_FW="abcdef0"' \
    timeline/test_timeline.cpp -o "$OUT/tl"
for s in $("$OUT/tl" --list); do "$OUT/tl" "$s"; done

# upstream against the fork
BIN=$(bash updiff/build.sh /tmp/PSIPro "$OUT/updiff")
BIN=$BIN bash updiff/matrix.sh
```

`updiff/build.sh` copies `PSIPro.ino` to `up_main.cpp` in `OUT`, puts the fork's prototype header
(`include/functions.h`) ahead of it (an Arduino sketch gets its prototypes generated; a plain C++ build does not) and
supplies a three-line `Wire` stub. Upstream's own `config.h` and `matrices.h` sit beside it, so the upstream build
uses upstream's settings.

The simavr probes are PlatformIO projects. Build in place, run, then remove `.pio/`:

```bash
SIM=~/.platformio/packages/tool-simavr/bin/simavr
for p in millis_probe fwprobe bench; do
  (cd simavr/$p && pio run && timeout 300 "$SIM" -m atmega32u4 -f 16000000 .pio/build/*/firmware.elf; rm -rf .pio)
done
(cd simavr/wave && pio run &&
  timeout 8 "$SIM" -m atmega32u4 -f 16000000 -o "$OUT/wave.vcd" -at "led=trace@0x2b/0x10" .pio/build/wave/firmware.elf;
  python3 check_wave.py "$OUT/wave.vcd"; rm -rf .pio)
```

simavr prints what the sketch writes to `Serial1`. The probes never exit, so `timeout` ends the run. The first
`fwprobe` line (`warmup`) is taken before Timer1 settles and is to be ignored.

## Results as recorded

- `simavr/fwprobe/result_current_firmware.txt`: `millis()` runs 1.6% fast for idle swipe, solid, comet, gradient,
  colorcycle and `native:1`, and 2.3% slow for twinkle, wipe and chase.
- `proto/result_fwprobe_exact_credit.txt`: with `proto/millis_exact.diff` every look is within 126 ppm, which is one
  millisecond in the 8 s window.
- `simavr/wave/result.txt`: T0H 250 ns, T1H 750 ns, 1250 ns bits (1437.5 ns at a byte boundary), the wire bytes are
  the frame's bytes in G, R, B order, and two `show()` calls with nothing between them are 107 µs apart.
- `simavr/bench/result.txt`: `show()` 1607 µs, twinkle 6784 µs a frame, wipe, chase and gradient about 800 µs.
- `proto/result_timeline_prototype.txt`: the prototype passes the 17 scenarios that need no core change.
