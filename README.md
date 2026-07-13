# DroidNet-PSIPro

An additive fork of the **PSI Pro** firmware by **Neil Hutchison**, for MaxStang's PSI Pro board.

Nearly everything in this repository is Neil's work (and his contributors'). We added one small
layer on top of it. This README leads with the original project because that is where the credit
belongs.

---

## The original project

**PSI Pro firmware — by Neil Hutchison — <https://github.com/nhutchison/PSIPro>**

If you own a PSI Pro, this is the firmware you want. Go get it from Neil, and go star the repo.

It drives MaxStang's PSI Pro board (a 48-LED panel on a Sparkfun Pro Micro / ATmega32U4) and speaks
JawaLite over serial and I2C, so it drops straight into an existing MarcDuino/Teeces/STEALTH droid
without ceremony. Version 1.7, released 30 December 2020.

Some specific things in it that are worth admiring, having now read the source closely:

- **The v0.7 non-blocking rewrite.** There is no `delay()` in the sequence code. Every pattern is a
  millis-driven state machine, which means any sequence can be interrupted the instant a new command
  arrives — you never have to wait for the Imperial March to finish before the panel will listen to
  you again. That is a genuinely hard thing to get right across two dozen animations, and Neil did it
  on a part with about 2.5 KB of SRAM.
- **The sequence library.** Leia, the Imperial March, the Star Wars title crawl, Knight Rider,
  Lightsaber Battle, Short Circuit, a working VU meter — all on a small PSI panel. It is a delight,
  and none of it was necessary. Somebody just wanted it to be good.
- **The real-world engineering.** Brightness is capped to preserve LED life. There is an explicit,
  loud warning about the brightness pot when the board is running off USB power, because that
  combination can damage your Pro Micro. There is firmware averaging of the pot reading to work
  around the missing hardware capacitor on the board. That is somebody thinking hard about the
  person who will one day be soldering this into a droid.

Neil's own README is preserved here, unchanged, as [README_UPSTREAM.md](README_UPSTREAM.md) — the
full command reference, wiring notes, warnings and version history are all there, in his words. Read
that one for how the board actually works.

Neil credits these people in his own docs, and we carry that forward:

- **Krijn Schaap** — the main sequence transitions, based on his PSI sketch.
- **Malcolm "Maxstang" MacKenzie** — the PSI Pro boards, pattern timing tuning, support, testing and
  encouragement.
- **Skelmir** — bug fixes and Mini support.

---

## NOTICE — please read before you use, copy or redistribute this

**The upstream PSI Pro project carries no license.** There is no LICENSE file and no permission
statement anywhere in its source. Under default copyright law that means **all rights are reserved by
Neil Hutchison and his contributors**. We have no grant to redistribute their code, and we claim no
rights over it whatsoever.

We are publishing this fork anyway, in the open, in the hope that it is a welcome addition to the
community. To be plain about where we stand:

- The PSI Pro firmware is (c) Neil Hutchison and contributors. It is theirs. Not ours.
- This is a hobby project. It is entirely non-commercial. Nothing here is sold, and nothing here ever
  will be.
- **We have contacted Neil** to ask permission for this fork to exist, and to invite him to add a
  license of his choosing to the upstream project — whatever license he wants, or none at all. That
  is his call and nobody else's.
- **If Neil, or Krijn, or Malcolm, or Skelmir would rather this did not exist, we will take it down
  immediately and without argument.** Open an issue on this repo, or reach the owner any way you
  like, and it is gone. No discussion needed.
- If you want the PSI Pro firmware itself, **get it from Neil**: <https://github.com/nhutchison/PSIPro>.
  Not from us.

There is no LICENSE file in this repo that purports to license Neil's code, because we cannot license
what is not ours. The only license file here,
[LICENSE-DroidNet-Contract](LICENSE-DroidNet-Contract), covers *only* the handful of files we wrote
ourselves, and says so explicitly.

---

## STOP: this has never run on hardware

**Nothing in this fork has ever been flashed to a real PSI Pro. Not once.**

Verification here is host-side only:

- `bash test/host/run.sh` — 154 checks on the contract parser and effect math, plus a type-check of
  the PSI render layer against a *mock* of the board API. It passes. It proves the code compiles and
  the math is what we think it is. It proves nothing about a real panel.
- It has **never been compiled for AVR**. The PSI is the tightest board of the three for flash
  (roughly 28 KB usable on the ATmega32U4) and the contract layer adds an estimated 3-4 KB. There is a
  real chance the linker overflows. A `CONTRACT_SLIM` build flag exists in `include/config.h` as an
  escape hatch (it drops the heaviest novelty native modes to reclaim space), but that is a theory
  too — nobody has watched it link.
- No timing, no brightness, no power draw, no serial behaviour has been observed on a physical board.

If you are thinking of putting this near a droid: **bench-compile it, bench-test it on a bare board,
and check the current draw before you trust it.** Neil's warning about the brightness pot and USB
power still applies, and applies harder to code nobody has run.

---

## What this fork adds

One thing: a small, additive **Driveable-Animation Contract** so an external tool (Cantina Studio, a
music-analysis and dome-lighting authoring tool) can drive the panel in time with a song.

It is a wire grammar plus a render layer. That's it.

```text
!<cls><unit><verb>[:k=v,...]

  cls    L = logic displays   P = PSI   H = holoprojectors   * = all
  unit   F = front   R = rear   T = top   * = all
  verb   A = animate (set the base look)      P = pulse (a beat accent)
         C = beat-clock seed                  B = brightness
         L = level (e.g. VU energy)           X = stop
         M = mode (show / idle)               Q = query

example: !PFA:i=comet,c=ff8800,s=200,m=64,am=1
         (front PSI, animate, comet in orange, fast, accent on the downbeat)
```

The `!` prefix was chosen for one specific reason: **every stock board parser already ignores it.**
That is what makes this layer additive rather than invasive. A board that has never heard of the
contract just drops the line on the floor.

What it buys:

- **Arbitrary RGB**, not just the built-in palette choices.
- **Beat-locked accents** — a pulse overlay that fires on the downbeat, on every beat, or as a build.
- **A host-seeded beat clock**, so the panel stays in phase with the music instead of free-running.
- **Autonomous board-side playback** — the host pushes a compact "score" (a handful of sections keyed
  to beat numbers) once, and then the board plays the rest of the song by itself.

Six new parametric effects render along the panel's 10 columns as a 1-D strand: **comet, chase, wipe,
gradient, colorcycle, twinkle**. Their math lives in the shared core and is host-tested, so the same
cue renders the same way on every board in the family.

**Every native command of the original firmware still works exactly as before.** The whole JawaLite
`<addr>T` / `A` / `D` / `xPy` grammar, all the modes 0-21 and 92, the I2C intake, the pot, the
EEPROM settings, the jumper — untouched. The contract layer only takes over the frame while it is
armed, and hands the panel straight back to `runPattern()` on stop. This is not a rewrite.

### One bug fix, offered back

While feeding longer command lines through `buildCommand()` we found a genuine out-of-bounds write in
the upstream version: it writes the character *before* the bounds check, so `pos` can reach 64 and the
function writes `cmdString[64]` — one byte past the end of a `char[64]`. On an ATmega32U4 that is
silent stack corruption. It is fixed here (bounds-check before the write, clamp the terminator), and
**the fix has been offered back to Neil.**

This is not a gotcha. A bug like that is invisible until somebody starts pushing 40-character command
strings at it, which nobody had a reason to do until now. The firmware is excellent; that is exactly
why we read it closely enough to find this.

---

## What is theirs, and what is ours

Be clear-eyed about the split. Their code is around 3,200 lines of carefully-tuned firmware. Ours is
about 775 lines of new code plus a handful of hooks.

**Theirs** (Neil Hutchison and contributors — do not attribute any of this to us):

| Path | What it is |
| --- | --- |
| `src/main.cpp` | The firmware. Every sequence, the command parser, the pot/EEPROM/I2C handling. |
| `include/config.h` | Board configuration, colors, timings. |
| `include/matrices.h` | The LED matrix bitmaps. |
| `README_UPSTREAM.md` | Neil's own README, preserved verbatim. |

**Earlier droid work by the owner of this repo** (made for one specific droid, C2B5, before
the contract layer existed; it came in with the seed — see Provenance). Not Neil's, so don't
credit him for it; not part of the contract layer either, so it is **not** MIT-licensed:

| Path | What it is |
| --- | --- |
| `visualizer/` | A browser preview of the PSI animations. It reimplements Neil's modes in JavaScript, so it is downstream of his firmware, but he did not write it and his README never mentions it. |
| `include/functions.h`, `include/preamble.h` | Prototypes — artifacts of the PlatformIO restructuring. Upstream is a flat Arduino sketch, so these two files do not exist there; their contents are prototypes of Neil's functions. |

**Ours** (the DroidNet contract layer — this is the entire extent of it):

| Path | Lines | What it is |
| --- | --- | --- |
| `src/contract/contract_core.h` | 362 | Pure, dependency-free C++: the wire parser, the beat clock, the effect math, the score table. Byte-identical across all three DroidNet forks. |
| `src/contract/ContractPSI.h` | 412 | The PSI render layer — maps the contract onto the board's existing `allON`/`fill_column`/`DiscoBall`/`VUMeter` primitives. Adds no new render primitives of its own. |
| `test/host/` | 724 | The host test harness (parser tests + a mock of the board API). |

**Ours, inside the files above** (small hooks, all of them additive):

- `src/main.cpp` — one `#include`, one `if (!contractLoopTick())` guard in `loop()`, one
  `contractPulseTick()` call, an `if (cmdString[0]=='!')` branch in `serialEvent()` and
  `receiveEvent()`, the `buildCommand()` bounds fix, two `g_contractArmed` guards in `VUMeter()`, and
  `#ifndef CONTRACT_SLIM` fences around five novelty modes.
- `include/config.h` — the commented-out `CONTRACT_SLIM` flag.
- `include/functions.h` — five prototypes for primitives the contract layer calls.

That is the whole diff. `git diff 11d69a3..HEAD -- src include` will show you exactly this.

---

## Building

Heads up: there is no `platformio.ini` in this tree. The PlatformIO layout (`src/` + `include/`) came
from the droid-side working collection this was seeded from, not from Neil — upstream is a flat
Arduino sketch (`PSIPro.ino` + `config.h` + `matrices.h`). To build you will need to supply your own
PlatformIO project file (board `sparkfun_promicro16`, framework `arduino`, deps FastLED) or copy the
sources back into an Arduino sketch folder. Either way, **watch the flash number** — see the warning
above.

Host tests need only a C++17 compiler:

```bash
bash test/host/run.sh
```

---

## Provenance

The chain, honestly:

1. **Neil Hutchison's PSI Pro firmware** (<https://github.com/nhutchison/PSIPro>), with sequence
   transitions from Krijn Schaap, timing tuning and hardware from Malcolm "Maxstang" MacKenzie, and
   fixes plus Mini support from Skelmir. This is the firmware.
2. **Customizations made for one specific droid (C2B5)**, kept in a private working collection. That
   is where the PlatformIO restructuring and the browser `visualizer/` came from. That collection is
   private and will stay private, so there is no link to give you — but it sits in the middle of the
   chain and it would be dishonest not to say so.
3. **This repository** — the same code, plus the additive DroidNet contract layer described above.

If any of this looks like it came from the contract layer and it isn't in the "Ours" table, it
didn't. And if it looks like it came from Neil and it's in the "Earlier droid work" table, it didn't.

---

## Credits

- **Neil Hutchison** — the PSI Pro firmware. All of it.
- **Krijn Schaap** — the main sequence transitions, based on his PSI sketch.
- **Malcolm "Maxstang" MacKenzie** — the PSI Pro board itself, pattern timing tuning, support and
  testing.
- **Skelmir** — bug fixes and Mini support.
- The FastLED project, on which the firmware depends.
- **Travis Cook** — the contract layer in `src/contract/` and `test/host/`, and nothing else.

---

## License

Read this together with the NOTICE at the top.

- **The original firmware** (`src/main.cpp`, `include/config.h`, `include/matrices.h`,
  `README_UPSTREAM.md`) is copyright Neil Hutchison and contributors. It carries **no license** —
  all rights reserved. We are redistributing it here without a grant, in good faith, pending Neil's
  word, and we will remove it on request.
- **The earlier droid work** (`visualizer/`, `include/functions.h`, `include/preamble.h`) is
  copyright Travis Cook, but it is derivative of Neil's firmware — the visualizer reimplements his
  animations, the two headers prototype his functions. It is **not** part of the contract layer and
  is **not** MIT-licensed. Treat it as all rights reserved too.
- **The contract layer only** (`src/contract/*` and `test/host/*`) is copyright 2026 Travis Cook and
  is offered under the **MIT license** — see [LICENSE-DroidNet-Contract](LICENSE-DroidNet-Contract).
  That file applies to those paths and to nothing else in this tree.

`src/contract/contract_core.h` is shared byte-identically with the DroidNet RSeries and Flthy forks.
Because the RSeries firmware is LGPL-2.1, that one file is dual-licensed by its author (LGPL-2.1-only
when distributed as part of the LGPL fork, MIT here). Its header says so.
