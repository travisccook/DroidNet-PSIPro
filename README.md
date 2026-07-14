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

## This fork is SERIAL-ONLY by default

**Upstream listens on serial *and* I2C. This fork, out of the box, listens on serial only.** That is
the one place it is *less* capable than the firmware it forked, and it is a deliberate choice rather
than an oversight, so it belongs up here rather than in a footnote.

If anything on your droid talks to the PSI over I2C — a MarcDuino, a Teeces or STEALTH setup, any
JawaLite master on the bus at address 22 — **you want the I2C build**. It is one line:

```c
// include/config.h
#define PSI_ENABLE_I2C        // uncomment this
```

```bash
pio run                  # serial-only (default)   25,754 B  89.8% flash
pio run -e PSIPro-i2c    # + I2C intake            27,238 B  95.0% flash
```

Be aware of the failure mode if you get this wrong: with I2C compiled out, the board does not
complain, it simply **never answers** an I2C master. No error, no blink — it just sits there.

**Why serial-only is the default.** Upstream's I2C callback (`receiveEvent`) runs in **interrupt
context**, and it parses a command and renders LEDs from in there. FastLED's `show()` ends with an
unconditional `sei` — it disables interrupts to bit-bang the WS2812 data, then re-enables them — and
Arduino's `twi.c` re-arms the I2C slave *before* invoking the callback. So a render inside that
interrupt turns interrupts back on with the slave live, and further I2C traffic during the render
window **re-enters the same interrupt** on top of the frame already on the stack. An AVR stack
overflow does not trap: it walks down into `.bss` and silently corrupts globals. This board has only
~1 KB of stack to give.

Our own contract path is already deferred out of that interrupt (see below). This switch is about
*upstream's* native path — `receiveEvent → parseCommand → runPattern → allOFF → FastLED.show` — which
we are not going to rewrite in a fork that carries Neil's name. Compiling I2C out removes the TWI
vector from the image entirely, and with it the last interrupt that can reach a render at all.

What it buys, measured:

| | serial-only (default) | with I2C |
| --- | --- | --- |
| flash | 25,754 B (89.8%) | 27,238 B (95.0%) |
| SRAM | 1,300 B (50.8%) | 1,581 B (61.8%) |
| worst-case stack | 396 B of 1,260 B (31%) | 478 B of 979 B (49%) |
| can an interrupt reach a render? | **no** | yes (upstream's native path) |

It does **not** give the five `CONTRACT_SLIM` modes back — serial-only with `CONTRACT_SLIM` off is
still 32,624 B (113.8%) and will not link.

Both configurations are built and type-checked by `test/host/run.sh` on every run, so neither can
quietly rot.

---

## STOP: this has never run on hardware

**Nothing in this fork has ever been flashed to a real PSI Pro. Not once.**

Be precise about what is and is not verified, because the two are easy to blur:

**What IS verified.** It cross-compiles for the real microcontroller. `pio run` builds a complete
firmware image with the real avr-gcc for the real ATmega32U4, and the real linker signs off on it.
That is a genuine step up from where this fork started — it used to be checked only by a type-check
against a hand-written mock, and that mock lied twice (see below). The host suite is 341 checks on
the contract parser, the beat clock and the effect math, plus render-layer guards, and
`bash test/host/run.sh` now finishes by doing the cross-compile itself.

**What is NOT verified.** Everything that only a physical board can tell you. **A successful link is
not a bench test.** No timing, no brightness, no power draw, no serial behaviour, no I2C behaviour
has ever been observed on real hardware. The code has never executed a single instruction outside a
host test.

**It fits — but only just, and not by default.** This is the tightest board of the three, and the
numbers are worth knowing before you build:

| Build | Flash | of 28,672 B |
| --- | --- | --- |
| Stock upstream PSI Pro (no contract layer) | 25,106 B | 87.6% |
| **This fork, as shipped (serial-only)** | **25,754 B** | **89.8%** |
| This fork with the I2C intake (`-e PSIPro-i2c`) | 27,238 B | 95.0% |
| …with `CONTRACT_SLIM` disabled | 32,624 B | 113.8% — **will not link** |
| …with the codegen flags removed | 28,896 B | 100.8% — **will not link** |

Neil's firmware already used 87.6% of this chip. Only ~3.5 KB was ever free, and the contract layer
costs ~13.7 KB. It fits only because of three things working *together* — `CONTRACT_SLIM` in
`include/config.h`, the AVR codegen flags in `platformio.ini`, and the `PSI_NOINLINE` outlining in
`src/contract/ContractPSI.h`. The last two rows above are what happens when you remove one of them:
**disable any single leg and the image overflows again.** (For scale: as originally published, before
this work, this fork linked at 38,790 B — 135.3%.)

### SRAM and the stack

SRAM sits at 1,300 B of 2,560 B (50.8%), leaving **1,260 B for the stack** in the default
serial-only build. That used to be an open question here. It has now been analysed statically
(`test/host/stack_report.py` regenerates the numbers from `-fstack-usage` plus the real
disassembly), and in the shipped configuration the answer is good:

- **Worst-case stack: 396 B of 1,260 B (31%), and the bound is SOUND** — with the TWI vector gone,
  **no interrupt in the image can reach a render at all**, so there is no nesting to worry about.
  (`stack_report.py` still prints a nesting warning against the USB interrupt; that is its
  conservative model of indirect calls charging a FastLED vtable entry to PluggableUSB's dispatch
  loop, which never executes because no PluggableUSB module is ever registered. Verified in the
  disassembly.)

- **In the I2C build (`-e PSIPro-i2c`) the picture is the one described below**, and the bound there
  is *not* sound. Worst case 478 B of 979 B (49%). Deepest path is
  `main → serialEventRun → parseContract (138 B — the largest frame in the image) → allOFF →
  FastLED::show → showPixels`, plus the worst single interrupt. No recursion anywhere, no
  `alloca`/VLA (every frame is a compile-time constant), and **no heap at all** — nothing links
  `malloc`, so the whole 979 B really is stack and the margin is real.

- **But that bound is still not sound, and you should know why.** FastLED's `showPixels()` executes
  an unconditional `sei` (it disables interrupts to bit-bang the WS2812 data, patches
  `timer0_millis`, then re-enables them). The PSI is an **I2C slave**, and its I2C interrupt runs the
  command parser and a full LED render — so interrupts come back on *while still inside the I2C ISR*,
  with the slave already re-armed. A second I2C message arriving during the render window re-enters
  that ISR on top of the frame already there. On AVR a stack overflow does not trap; it walks down
  into `.bss` and silently corrupts globals.

**This hazard is inherited, not introduced — and this fork no longer contributes to it.** Stock
upstream reaches that `sei` by its own route (`receiveEvent → parseCommand → runPattern → allOFF →
FastLED.show`), so an unmodified PSI Pro has it too. What this fork *used* to add was a second path
in, carrying `parseContract` — the single largest stack frame in the whole image (138 B) — inside the
interrupt handler.

That is now fixed. The contract path is **deferred out of interrupt context**: `receiveEvent()` calls
`contractQueueFromISR()`, which copies the line and returns, and `loop()` calls
`contractServicePending()`, which does the parsing and rendering in main context. Neil's native
command path is left exactly as it was. The results, all from `test/host/stack_report.py`:

| | before the deferral | now |
| --- | --- | --- |
| worst-case stack | 596 B of 1,076 B | **478 B of 979 B (49%)** |
| I2C ISR frame | 188 B | **129 B** |
| one level of ISR nesting | 827 B | **650 B** (329 B still spare) |

(The 979 B is lower than the old 1,076 B because the deferral queue costs 96 B of SRAM. Worth it.)

Two guards keep it that way: a behavioural one (`compile_contract.cpp` A11 — the ISR entry point must
not render or apply anything) and a **static** one in `run.sh`, because the behavioural test alone
cannot catch someone "simplifying" `receiveEvent()` back to a direct `parseContract()` call — the
queue would still pass its own test while the hazard quietly returned.

**What remains is upstream's, and we have not touched it.** `parseCommand → runPattern → FastLED.show`
still re-enables interrupts inside that ISR, so nesting is still *possible* on the native path. Fixing
that means changing Neil's design in a fork that carries his name, and it is not ours to do. Whether
re-entry actually happens depends on I2C traffic timing, which **cannot be determined without
hardware** — the static fact is proven, the frequency is not.

If you are bench-testing this, that is still the thing to watch for: unexplained corruption of
unrelated globals under heavy I2C traffic.

`CONTRACT_SLIM` is **not free**. It drops five native novelty modes: 7 (i_heart_u), 9 (red_heart,
front half), 11 (Imperial March), 19 (lightsaberBattle) and 20 (StarWarsIntro). If you want those
modes, do not build this fork — flash Neil's upstream PSI Pro. There is no configuration of this
fork that keeps them and fits.

If you are thinking of putting this near a droid: **bench-test it on a bare board and check the
current draw before you trust it.** Neil's warning about the brightness pot and USB power still
applies, and applies harder to code nobody has run.

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

Their code is around 3,200 lines of carefully-tuned firmware. Ours is about 775 lines of new code
plus a handful of hooks.

**Theirs** (Neil Hutchison and contributors):

| Path | What it is |
| --- | --- |
| `src/main.cpp` | The firmware. Every sequence, the command parser, the pot/EEPROM/I2C handling. |
| `include/config.h` | Board configuration, colors, timings. |
| `include/matrices.h` | The LED matrix bitmaps. |
| `README_UPSTREAM.md` | Neil's own README, preserved verbatim. |

**Earlier droid work by the owner of this repo** (made for one specific droid, C2B5, before
the contract layer existed; it came in with the seed — see Provenance). Not Neil's, so don't
credit him for it; not part of the contract layer either:

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

```bash
pio run                 # build the firmware (ATmega32U4)
pio run -t upload       # flash it (add --upload-port /dev/cu.usbmodemXXXX)
bash test/host/run.sh   # host checks + the same cross-compile
```

`platformio.ini` is in the tree and builds out of the box. (It did not used to be: the PlatformIO
layout — `src/` + `include/` — came from the droid-side working collection this was seeded from, not
from Neil, whose upstream is a flat Arduino sketch. The project file stayed behind in that collection
when this board was vendored out. It has now been written and used.)

Dependencies are derived from what `src/main.cpp` actually includes: FastLED, plus EEPROM and Wire,
which ship with the Arduino AVR core.

**The build flags in `platformio.ini` are load-bearing, not decoration.** `-mcall-prologues`,
`-mrelax`, `-fno-inline-small-functions`, `-fno-move-loop-invariants` and
`--param=max-inline-insns-auto=2` are worth ~1.5 KB together, and the `PSI_NOINLINE` outlining in
`src/contract/ContractPSI.h` is worth another ~1.4 KB. None of them change behaviour — they only
change where GCC emits code — but without them this firmware does not fit. If you rebuild these
sources in the Arduino IDE, or in your own project file, **you will not get those flags and the image
will overflow.** Use this project file.

Both the flags and the `PSI_NOINLINE` block carry comments explaining what was measured, and what was
tried and rejected (e.g. `-funsigned-char` saves 32 B and is a trap: `char` is signed on this target,
so it would change the meaning of every character comparison in the parser and desync the AVR build
from the host tests). Read those before "simplifying" anything there.

Host tests need only a C++17 compiler. If PlatformIO is not installed, `run.sh` skips the
cross-compile stage cleanly and tells you what you did not check.

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

---

## Credits

- **Neil Hutchison** — the PSI Pro firmware. All of it.
- **Krijn Schaap** — the main sequence transitions, based on his PSI sketch.
- **Malcolm "Maxstang" MacKenzie** — the PSI Pro board itself, pattern timing tuning, support and
  testing.
- **Skelmir** — bug fixes and Mini support.
- The FastLED project, on which the firmware depends.
- **Travis Cook** — the contract layer in `src/contract/` and `test/host/`, and nothing else.
