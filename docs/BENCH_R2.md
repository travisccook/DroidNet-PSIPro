# Bench record: R2 (contract v1.4, core CA2)

**Status: not run.** No image from this tree has been flashed to a PSI Pro. R1's bench (`docs/BENCH_R1.md`,
Gate 1) comes first and is unchanged; this file adds the PSI's rows of Gate 2 (the Signal Booster's plan
`docs/superpowers/plans/2026-11-21-lighting-wave-2.md`, "Gate 2"). Until every row has a result, every claim
about R2 is about the host, the cross-compiler and the simulator.

## What R2 is on the PSI

| | `PSIPro` (default) | `PSIPro-i2c` |
| --- | --- | --- |
| Contract | v1.4, shared core CA2, `rel=2` | none (unchanged from R1) |
| New looks | `vu`: the meter, its level moved by the beat pump (v1.4 N10 to N12) | none |
| Stand-ins | as R1, and `logic`, `projector` draw `sparkle` | none |
| Query reply, WDP caps | `ver=1.4,rel=2`; `droidnet.contract`, `contract.1.4s` | no reply; no caps |
| Flash | 28,330 B (98.8 %; gate 28,416 B) | 16,998 B (59.3 %) |

What to have in mind while watching `vu`:

- Pumped (a clock running, `am=` and `m=` not 0), the beat moves the bars and not the brightness: between beats
  the level rests at `255 - m`, on a beat it rides up to `envBright(255, m, amount)`. With `m=255` the bars go
  from empty to full; with `m=200` from one row of six to four.
- The level goes up at most once in 340 ms (the window `B` and `b=` share) and down at once, so a row lights
  three times a second at the most. At 176 BPM the beats are 341 ms apart, just over that window, and a beat's
  rise is often taken a frame or two late, when the level has fallen: the bars still rise on the beat, lower
  (host: the bottom row lit on 20 of 35 beats, the middle on 9).
- `s=` paces the redraw, as for the meter fed by `L`: `s=255` redraws every 25 ms pass, the default `s=128`
  every 210 ms, which lands late on most beats (host, 120 BPM, `m=255`: the middle of the bars lit on 16 of
  24 beats at `s=128`, on 24 of 24 at `s=255`). Rows 8 and 8b compare the two.
- An `L` sent while the pump feeds `vu` is not shown. Paused (`C:bpm=0`) the clock stops, the look is not
  pumped and `vu` is the meter: the pump counts as its level feed, so the bars hold the pump's last level
  for 2 s, as after a last `L`, and then walk. Resumed from the walk, the bars go dark and ride the beat
  again a third of a second later.
- **No accent over a pumped `vu` (a v1.4 rule on the PSI).** While the look is `vu` and the pump runs, a
  scored `ae=` does not fire and a live `P` is ignored. Over a `vu` that is not pumped (the meter) both
  work. Studio plans no accent over `vu`.

## Rows

Record for each row: pass or fail, what was seen, and a short phone video beside the stock look where there is
one. Brightness as `BENCH_R1.md` says (20 on USB power; a 3 A supply for anything brighter).

| # | Board | Step | Expect | Result |
| --- | --- | --- | --- | --- |
| 8 | PSI | `!**M:v=show`, `!**C:bpm=120,bpb=4,beat=0`, `!P*A:i=vu,am=2,m=255`, beside a second PSI on stock mode 92 (`0T92`) | The bars jump on the beat and fall between; no row flickers. Say how it compares with mode 92 | |
| 8b | PSI | Row 8 with `!P*A:i=vu,s=255,am=2,m=255` | The bars reach the top on the beat more often than in row 8 | |
| 9 | PSI | During row 8b: `!**C:bpm=120,bpb=4,beat=0` and `beat=2` by turns, as fast as they can be sent, for 5 s; then `!**C:bpm=0` for 5 s; then `!**C:bpm=120,bpb=4,beat=0` | Under the seeks the bars stay high and the top rows move about three times a second at the most, never a flicker. Paused: the bars hold for 2 s, then walk. Resumed: dark for a moment, then the bars ride the beat again | |
| 9b | PSI | During row 8b: `!P*P:c=ffffff,d=200`; then `!P*A:i=vu,s=255,am=2,m=255,at=0,ae=flash,ac=ffffff` (a scored section, with `!**M:v=show` and a `C` as in row 8); then `!**C:bpm=0` and `!P*P:c=ffffff,d=200` | No white flash over the pumped bars, live or scored. Paused (the meter), the `P` flashes white | |
| 10 | PSI | `BENCH_R1.md` in full, on the R2 image (`ver=1.4,rel=2` and `contract.1.4s` where it reads 1.3) | Every row as in R1: nothing else moved | |
| 15 | All | A full song from Studio at release 2, filmed, beside its filmstrip; the PSI's part | The PSI does what the filmstrip shows | |
| 16 | All | A dome with one board left on R1: the PSI on R1, the rest on R2 | Studio plans the PSI as R1 and the show plays | |
