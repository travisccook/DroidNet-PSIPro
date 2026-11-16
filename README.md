# DroidNet-PSIPro

Part of [DroidNet](https://github.com/travisccook/DroidNetSignalBooster-releases), the Signal Booster for droids. This is one of its board firmwares.

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

**Two more defaults differ from upstream v1.7**, and both are in `include/config.h`:

- `alwaysOn = true` (upstream: `false`). With a blank EEPROM a `T` mode stays on until the next
  command instead of returning to the swipe by itself. `0P0` stores upstream's behaviour, `0P1`
  this one; a board whose EEPROM already holds the setting keeps it.
- 9600 baud on the serial header (`_9600BAUDSJEDI_` is defined; upstream ships 2400). The WCB the
  board is wired to speaks 9600.

If anything on your droid talks to the PSI over I2C — a MarcDuino, a Teeces or STEALTH setup, any
JawaLite master on the bus at address 22 — **you want the I2C build**. It is its own environment:

```bash
pio run                  # serial-only (default)
pio run -e PSIPro-i2c    # + I2C intake
```

**The I2C build has six fewer contract looks.** With the I2C intake linked, the whole contract does
not fit the board, so `PSIPro-i2c` also sets `PSI_NO_STRIP_LOOKS` and leaves out comet, chase, wipe,
gradient, colorcycle and twinkle. A show that names one still plays: comet, chase and wipe draw as
scan, twinkle as sparkle, gradient and colorcycle as a solid in the line's colour (as an accent, all
six draw as a solid). Every native mode and every other contract look is the same in both builds.
Uncommenting `PSI_ENABLE_I2C` in `include/config.h` instead of using the environment builds I2C
with every look, which is not a supported build. The six come back in the second firmware release.

Be aware of the failure mode if you get this wrong: with I2C compiled out, the board does not
complain, it simply **never answers** an I2C master. No error, no blink — it just sits there.

**Why serial-only is the default.** Upstream's I2C callback (`receiveEvent`) runs in **interrupt
context**, and it parses a command and renders LEDs from in there. FastLED's `show()` ends with an
unconditional `sei` — it disables interrupts to bit-bang the WS2812 data, then re-enables them — and
Arduino's `twi.c` re-arms the I2C slave *before* invoking the callback. So a render inside that
interrupt turns interrupts back on with the slave live, and further I2C traffic during the render
window **re-enters the same interrupt** on top of the frame already on the stack. An AVR stack
overflow does not trap: it walks down into `.bss` and silently corrupts globals. This board has only
~1 KB of stack to give. (That was stock FastLED. The clean-room driver that has since replaced it,
`include/FastLED.h`, masks its frame with an SREG save/restore instead of ending in `sei`, which
closes the re-entry route — see "SRAM and the stack", below. Serial-only stays the default.)

Our own contract path is already deferred out of that interrupt (see below). This switch is about
*upstream's* native path — `receiveEvent → parseCommand → runPattern → allOFF → FastLED.show` — which
we are not going to rewrite in a fork that carries Neil's name. Compiling I2C out removes the TWI
vector from the image entirely, and with it the last interrupt that can reach a render at all.

What it buys, measured:

| | serial-only (default) | with I2C |
| --- | --- | --- |
| flash | 27,494 B (95.9%) | 27,942 B (97.5%) |
| SRAM | 1,597 B (62.4%) | 1,802 B (70.4%) |
| worst-case stack | 435 B of 963 B (45%) | 533 B of 758 B (70%) |
| score sections a show may hold | 16 | 12 |
| contract looks | all | six drawn as stand-ins (above) |
| can an interrupt reach a render? | **no** | yes (upstream's native path) |

Both configurations ship all 23 upstream modes (0-21 and 92) — there is no "drop modes to fit" trade here (see
[What this fork adds](#what-this-fork-adds)/the animation VM below). What differs between the two
columns is the I2C intake, the six contract looks it leaves no room for, and four score sections.

Both configurations are built by `test/host/run.sh` on every run, gated on flash and stack, and run
through the same scenarios and the same hostile-stream test, so neither can quietly rot.

---

## Status

**Work in progress.** This DroidNet build is provided as is, without warranty of any kind. Use it at your own risk. To go back, flash the upstream firmware ([README_UPSTREAM.md](README_UPSTREAM.md)).

**It has never run on a PSI Pro board.** Everything below is the host, the cross-compiler and a
simulator of the ATmega32U4. Until the bench checklist in the firmware plan is recorded, treat any
image from this tree as untested on hardware: start with the brightness low and the board on its
own supply.

**Testing.** `bash test/host/run.sh` is the whole suite. It runs the firmware's own `src/main.cpp`
on host mocks of the hardware (no mock of the board API: that one lied twice and is retired) and
compares it, frame for frame, with captures of every native mode (36 goldens) and with upstream
v1.7 on the same mocks (78 scripted timelines); runs 89 scripted contract scenarios on each of the
two builds; feeds the firmware 23 hostile line scripts and 1,500 random streams and measures the
frames (at most three dark-to-lit swings a second, nothing above the owner's brightness); then
cross-compiles both builds with the real avr-gcc, gates their flash and worst-case stack, and runs
five probes in simavr: `millis()` against a hardware timer for each look, the show lifecycle,
the frames on the LED pin under hostile streams, the WS2812 waveform, and the cost of a frame.

**It fits, with real headroom in the default build, but not by much in the I2C build.** This is the
tightest board of the three, and the numbers are worth knowing before you build:

| Build | Flash | of 28,672 B |
| --- | --- | --- |
| Stock upstream PSI Pro (no contract layer) | 25,106 B | 87.6% |
| **This fork, as shipped (serial-only)** | **27,494 B** | **95.9%** |
| …serial-only, without `-mcall-prologues` | 28,492 B | 99.4% |
| …serial-only, with `PSI_NOINLINE` removed | 28,262 B | 98.6% |
| This fork with the I2C intake (`-e PSIPro-i2c`) | 27,942 B | 97.5% |
| …I2C, with `PSI_NOINLINE` removed | 28,424 B | 99.1% |
| …I2C, without `-mcall-prologues` | 28,942 B | 100.9% — **will not link** |

Neil's firmware already used 87.6% of this chip. All 23 upstream modes (0-21 and 92) ship, and every
upstream mode's animation except the four deliberate native holdouts
(`swipe`/`VUMeter`/`allON`/`allOFF`; see "What this fork adds" below) is PROGMEM bytecode played by
one small interpreter (`include/psi_vm.h`) — the contract effects remain parametric C++ renderers —
which is *why* there is headroom at all: it replaced the hand-written per-mode state machines with
one. That headroom is not evenly spread, though.
Since the contract's version 1.3 neither build has room to spare. `run.sh` fails the serial-only
build over 28,416 B and the I2C build over 28,160 B, so a change that would not leave room for a fix
is caught when it is made: the serial-only build is 922 B under its line, the I2C build 218 B. Both
need the AVR codegen flags in `platformio.ini` and the `PSI_NOINLINE` outlining in
`src/contract/ContractPSI.h` (the last four rows above). (For scale: as originally published,
before any of the flash or VM work, this fork linked at 38,790 B — 135.3%. There used to be a
`CONTRACT_SLIM` escape hatch that dropped five native novelty modes to claw back room. It is gone
— see "What this fork adds", below — because the VM made it unnecessary.)

The design target going into the VM conversion was roughly 26.3-26.4 KB serial / 27.7-27.8 KB I2C —
the probe's own byte-cost estimate plus about 100 B of slack. The VM-era totals (26,760 B / 28,202 B)
landed about 0.4 KB over that in both environments: the measured price of byte-exactness (a handful of
ops and program shapes the probe's seeds got wrong, forced by the golden captures) plus the +94 B
mode-22 demo. Since then the clean-room WS2812 driver that replaced FastLED gave back 2,162 B
(`docs/FASTLED_DRIVER_VERIFICATION.md`), collapsing `displayMatrixColor`'s per-row switches another
748 B, and the WDP device announce (below) costs about 1.47 KB of flash and 172 B of SRAM.
Contract v1.3 (the safety limits, the show lifecycle, load verification, the output gate) then took
most of what was left, after its shared core lost its floating point (1.4 KB of soft-float library
that one `float` expression had linked). `run.sh` fails a build that links any of it again.

### SRAM and the stack

SRAM sits at 1,597 B of 2,560 B (62.4%), leaving **963 B for the stack** in the default
serial-only build. That used to be an open question here. It has now been analysed statically
(`test/host/stack_report.py` computes the bound directly from the linked ELF — see the tool's own
`--help` for why `-fstack-usage` can't be trusted under `-flto`), and in the shipped configuration
the answer is good:

- **Worst-case stack: 435 B of 963 B (45%), and the bound is SOUND** — with the TWI vector gone,
  **no interrupt in the image can reach a render at all**, and no interrupt can nest.

- **In the I2C build (`-e PSIPro-i2c`) the worst case is 533 B of 758 B (70%), 225 B spare, and that
  bound is SOUND too** — 346 B of synchronous stack plus the worst single interrupt, 187 B. (That is
  a conservative charge: the tool bounds the USB interrupt's indirect calls by the largest
  address-taken function, the I2C `receiveEvent`.) No recursion anywhere, no `alloca`/VLA
  (every frame is a compile-time constant), and **no heap at all** — nothing links `malloc`, so the
  whole 758 B really is stack. It is the tightest margin on the board, which is why this build holds
  12 score sections and not 16. `run.sh` fails the build under 128 B of headroom (256 B serial-only).

- **It used to be unsound, and you should know why.** Stock FastLED's `showPixels()` executes an
  unconditional `sei` (it disables interrupts to bit-bang the WS2812 data, then re-enables them).
  The PSI is an **I2C slave**, and its I2C interrupt runs the command parser and a full LED render —
  so with FastLED, interrupts came back on *while still inside the I2C ISR*, with the slave already
  re-armed, and a second I2C message during the render window could re-enter that ISR on top of the
  frame already there. On AVR a stack overflow does not trap; it walks down into `.bss` and silently
  corrupts globals. The clean-room driver in `include/FastLED.h` masks the frame with an SREG
  save/restore instead, so the I-bit stays clear inside the ISR and it cannot re-enter
  (`docs/FASTLED_DRIVER_VERIFICATION.md` §6).

**This hazard is inherited, not introduced — and this fork no longer contributes to it.** Stock
upstream reaches that `sei` by its own route (`receiveEvent → parseCommand → runPattern → allOFF →
FastLED.show`), so an unmodified PSI Pro has it too. What this fork *used* to add was a second path
in, carrying `parseContract` — the single largest stack frame in the whole image (138 B) — inside the
interrupt handler.

That is now fixed. The contract path is **deferred out of interrupt context**: `receiveEvent()` calls
`contractQueueFromISR()`, which copies the line and returns, and `loop()` calls
`contractServicePending()`, which does the parsing and rendering in main context. Neil's native
command path is left exactly as it was. The results, all from `test/host/stack_report.py`:

| | before the deferral | right after it (historical) | `feature/animation-vm` (FastLED, historical) |
| --- | --- | --- | --- |
| worst-case stack | 596 B of 1,076 B | 478 B of 979 B (49%) | 441 B of 970 B (45%) |
| I2C ISR frame | 188 B | 129 B | 112 B |
| one level of ISR nesting | 827 B | 650 B (329 B spare) | 596 B (374 B spare) |

(979 B was lower than the original 1,076 B because the deferral queue costs 96 B of SRAM; the drop
to 970 B was SRAM drift from the animation-VM work. Nesting no longer applies since the clean-room
driver — today's figures are in the bullets above. Re-derive any of this yourself with
`python3 test/host/stack_report.py --elf .pio/build/PSIPro-i2c/firmware.elf` rather than trusting
this table as it ages.)

Two guards keep it that way: a behavioural one (the timeline scenario
`an_i2c_contract_line_is_applied_by_loop`, run on the I2C build — the ISR entry point must not
render or apply anything) and a **static** one in `run.sh`, because the behavioural test alone
cannot catch someone "simplifying" `receiveEvent()` back to a direct `parseContract()` call — the
queue would still pass its own test while the hazard quietly returned.

**What remains is upstream's design, and we have not touched it.** `parseCommand → runPattern →
FastLED.show` still parses and renders inside that ISR; it just can no longer re-enable interrupts
there, so it cannot nest. Moving that work out of the ISR means changing Neil's design in a fork that
carries his name, and it is not ours to do.

If you are bench-testing the I2C build, that is still the thing to watch: a render inside the I2C ISR
holds every other interrupt (serial receive included) off for the length of the frame.

This fork used to have a `CONTRACT_SLIM` escape hatch that dropped five native novelty modes — 7
(i_heart_u), 9 (red_heart, front half), 11 (Imperial March), 19 (lightsaberBattle) and 20
(StarWarsIntro) — to make room when flash ran short. **That trade-off no longer exists.** All 23
upstream modes are present in every configuration this fork builds; see "What this fork adds" below
for how.

Neil's warning about the brightness setting and USB power still applies: this PSI can draw more power
than a computer's USB port can supply, so keep the brightness low when it is powered over USB. See
[README_UPSTREAM.md](README_UPSTREAM.md).

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
         V = verify a pushed score

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
cue renders the same way on every board in the family. (The I2C build draws these six as stand-ins;
see "This fork is SERIAL-ONLY by default".)

**Every native command of the original firmware still works.** The whole JawaLite `<addr>T` / `A` /
`D` / `xPy` grammar, all the modes 0-21 and 92, the pot, the EEPROM settings, the jumper. With no
contract line on the wire the board draws what upstream v1.7 draws: `run.sh` runs both on the same
mocks through 78 scripted timelines and 75 are identical frame for frame. What differs, all of it:

- the three defaults above (serial-only, `alwaysOn`, 9600 baud);
- `0T19|n`: the lightsaber battle plays whole, as upstream's own comment says it should (upstream's
  code stops it at once);
- under `alwaysOn`, the tail of mode 4 (short circuit), when two pixels are left lit, repeats
  differently: upstream's 8-bit loop counter wraps there (two timelines);
- every frame is preceded by 200 µs of quiet line, so that two frames sent in the same tick latch
  on every WS2812 revision.

This is not a rewrite.

### What the contract does on this board (version 1.3, first release)

The contract's specification and its plan are in the DroidNet Signal Booster repository
(`docs/superpowers/specs/2026-11-16-driveable-animation-contract-v1.3-design.md`). On the PSI:

- **Brightness belongs to the owner.** A show's ceiling is the board's own brightness when the show
  arms (the pot, the EEPROM value or `3P`); `b=` and `B` are fractions of it. A line can dim the
  panel and can never make it brighter. A `3P` sent during a show is kept: the next show arms with
  it and the board is given back at it.
- **A show begins and ends cleanly.** `M:v=show` arms and keeps the swipe until the show's first
  `C`, `A` or `P`. `X` is dark until `M:v=idle` or a look. `M:v=idle` returns to the swipe, whatever
  the show selected. A `P` with no show overlays the board's own look and returns to it. The
  return waits until the panel may light again (340 ms after it last lit or got brighter), and
  the panel keeps what it shows until then: a short pulse can stay up that long.
- **The owner's controller always wins.** A native `T` or `D` command addressed to the PSI ends a
  show on it and then runs.
- **A dead host gives the board back.** `M:v=show,wd=<seconds>`: that long with no line addressed
  to the board, and it behaves as if it had received `M:v=idle`.
- **A pushed show is checked.** `V:n=,k=` compares what arrived with what was sent; on a mismatch
  the board goes back to the swipe and reports `ok=2`. `Q:r=<n>` is answered with
  `ver, rel, cap, n, ok, bpm`, routed back through the WCB.
- **Nothing a host sends can strobe the panel.** Tempo is taken from 30 to 300 BPM, beat accents
  recur no faster than every 340 ms, live looks are spaced 170 ms, and every frame the contract
  draws passes one gate on its way out: the whole field goes from dark to lit at most three times
  a second, whatever lines arrive.
- **Native modes run as stock.** A show may select modes 0 to 22 and 92, without 2, 3 and 5
  (Flash, Alarm, Scream: whole-field strobes). Their frames are never altered. Every one of them
  draws at the brightness the show gives it, so `b=` and `B` dim all of them. A mode starts only
  when the panel may light again, the look before it staying up until then (most modes clear the
  panel and draw at once). `L` feeds the contract's own `meter` look and nothing else: the board's
  VU modes (21 and 92) keep their own bars, from a button or in a show.

### The animation VM: modes are data now

All 23 upstream modes (0-21 and 92) ship, and every upstream mode's animation is PROGMEM bytecode
played by one small interpreter (`include/psi_vm.h`), except the four deliberate native holdouts:
`swipe` (the default idle pattern), `VUMeter`, and the two solid fills `allON`/`allOFF` stayed
native, by design. (The six contract effects above are not bytecode either — they remain parametric
C++ renderers; the contract's `scan` and `sparkle` now render through hand-written shims in
`psi_vm.h` that replaced the deleted natives, not through the interpreter.) "Animations are data,
the interpreter is the only animation code." Every other upstream mode — Flash, Alarm, Leia, I Heart
U, Radar, red_heart/Pulse, the Star Wars scan, Imperial March, Disco Ball (both variants), Short
Circuit, Rebel Symbol, Knight Rider, Lightsaber Battle and the Star Wars Intro — is a flat opcode
string, converted one mode at a time and golden-frame-gated to reproduce Neil's original C
byte-for-byte before that C was deleted.

This is why there is no `CONTRACT_SLIM` escape hatch anymore. That define used to buy back flash by
deleting five of the heaviest hand-written mode bodies — i_heart_u, red_heart, Imperial March,
lightsaberBattle, StarWarsIntro — because the only way to shrink a mode used to be to delete its
function. With every once-droppable mode now PROGMEM data instead of a dedicated C function, there is
nothing left to drop: a new animation costs tens of bytes of opcode data (plus 48 B/frame for any new
bitmap art it needs), not a new function, and all five once-dropped modes now ship in both build
configurations alongside the other eighteen — see "It fits", above, for current sizes. Proved, not just
asserted: mode 22 ("processing sweep", a fork addition — not one of the 23 upstream modes above) is a
new standalone animation added as pure `vmc_process` data plus one dispatch case, at a measured cost of
94 B in both build configurations.

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

## WDP device announce

The PSI tells the WCB it is wired to what it is. Greg Hulette's Wireless Communication Board
firmware (7.0.1 and later) learns each serial port's devices from a one-line announcement, and this
fork sends one 0.5-3 s after power-up and then every 25-30 s, on the serial header (`Serial1`, the
WCB's port, 9600 baud):

```text
@WDP1 {"type":"PSI Front","fw":"1a2b3c4","caps":["droidnet.contract","contract.1.3"]}
```

- **`type`** comes from the front/rear jumper, read at each announce with the contract's own test
  (the one that decides whether `!PF` or `!PR` addresses this board): jumper open (pin 14 high) is
  `PSI Front`, jumper fitted is `PSI Rear`. Front and rear PSIs may share one WCB port; the WCB keeps
  one record per port and type, which is why the role is part of the type.
- **`fw`** is the commit the firmware was built from: `git rev-parse --short=7 HEAD`, with `-dirty`
  for a tree with uncommitted changes, or `unknown` built without git. `scripts/wdp_fw.py` sets it
  at build time; the images in `firmware/` carry their source commit.
- **`caps`**: `droidnet.contract`, the Driveable-Animation Contract above, and the version the
  build speaks: `contract.1.3`, or `contract.1.3s` from the I2C build, which draws six of the
  contract's looks as stand-ins.

The WCB consumes the line; it is never forwarded to another port or board. The line goes out in
pieces no bigger than the free room in the serial TX buffer, so the render loop never waits on it,
and the contract's reply to `Q` is written only while no announce is in flight, so the two never
interleave.
Each board's timing is jittered from the chip's factory serial number and the jumper, so PSIs that
power up together, front and rear included, do not announce in step.

The WCB only hears it if the PSI's TX is wired to the WCB's RX. The Pro Micro is a 5 V part and the
WCB's serial pins are 3.3 V, and two PSIs on one WCB port need their TX lines combined in hardware;
both are wiring questions, outside this firmware (see "Wiring & baud" and "Questions" in Greg's
`docs/WDP_DEVICE_ANNOUNCE.md`). The line-building core, `src/wdp/wdp_announce_core.h`, is shared
byte-identically with the RSeries and Flthy forks; `src/wdp/wdp_fork.h` is the PSI's identity and
its two hooks.

---

## What is theirs, and what is ours

Their code is around 3,200 lines of carefully-tuned firmware. Ours is a contract layer of about
2,100 lines (1,285 of them a core shared with two other boards), an animation interpreter, a
WS2812 driver, and a handful of hooks.

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
| `src/contract/contract_core.h` | 1,285 | Pure, dependency-free, integer-only C++: the wire parser, the beat clock, the safety limits, the score, the effect math. Byte-identical across all three DroidNet forks; `src/contract/CORE_PIN` names the commit it was copied from. |
| `src/contract/ContractPSI.h` | 826 | The PSI render layer — maps the contract onto the board's existing `allON`/`fill_column`/`VUMeter` primitives and the `vmContractSparkle` animation-VM shim (`include/psi_vm.h`). Adds no new render primitives of its own. |
| `include/psi_vm.h` | 1,321 | The animation interpreter and the bytecode of every upstream mode it plays (see "The animation VM", above). |
| `include/FastLED.h` | 460 | The clean-room WS2812 driver that replaced the FastLED package (`docs/FASTLED_DRIVER_VERIFICATION.md`). |
| `test/host/`, `test/simavr/` | 7,600 | The host suite and the simavr probes. They run `src/main.cpp` itself; there is no mock of the board API. |

**Ours, inside the files above** (small hooks, all of them additive):

- `src/main.cpp` — one `#include`; in `loop()`, the `if (!contractLoopTick())` guard and the
  `contractServicePending()`, `contractPulseTick()` and `contractReplyTick()` calls; an
  `if (cmdString[0]=='!')` branch in `serialEvent()` and `receiveEvent()`; the `buildCommand()`
  bounds fix; `contractYieldToNative()` in `doTcommand()` and `doDcommand()`;
  `contractOwnerSet3P()` in `doPcommand()`; two `contractHoldsLevels()` guards in `VUMeter()`; and
  `#define PSI_FRAME_GATE`, which names the contract's gate to the LED driver.
- `include/functions.h` — prototypes for the native primitives the contract layer calls.

**Ours, the WDP device announce** (see "WDP device announce", above):

| Path | What it is |
| --- | --- |
| `src/wdp/wdp_announce_core.h` | Pure C++: builds, checks and paces the `@WDP1` line. Byte-identical across all three DroidNet forks (LGPL-2.1-only, as its header says). |
| `src/wdp/wdp_fork.h` | The PSI's identity (type from the jumper, `fw`, caps), its seed, and `wdpForkSetup()` / `wdpForkTick()`. |
| `scripts/wdp_fw.py` | PlatformIO `pre:` script: defines `WDP_FW` from git. |

Its hooks: in `src/main.cpp`, one `#include`, `wdpForkSetup()` in `setup()` and `wdpForkTick()` in
`loop()`. `src/contract/ContractPSI.h` reads the announcer's state so that its `Q` reply never
lands inside an announce.

Not part of the contract layer, but ours in the same "additive layer" sense: `include/psi_vm.h`, the
animation bytecode interpreter the upstream modes now play through (all but the four native
holdouts), plus the hand-written `vmContractSparkle` shim the contract's sparkle effect renders
through — see "The animation VM: modes are data now", above.

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

Dependencies are derived from what `src/main.cpp` actually includes: EEPROM and Wire, which ship with
the Arduino AVR core. `<FastLED.h>` is served by the in-tree clean-room driver `include/FastLED.h`,
not the FastLED package. `scripts/wdp_fw.py` runs `git` at build time for the WDP announce's `fw`.

**The build flags in `platformio.ini` are load-bearing, not decoration.** `-mcall-prologues`,
`-mrelax`, `-fno-inline-small-functions`, `-fno-move-loop-invariants` and
`--param=max-inline-insns-auto=2` and the `PSI_NOINLINE` outlining in
`src/contract/ContractPSI.h` change no behaviour — only where GCC emits code — and both builds
need them: without `-mcall-prologues` alone the serial-only build is 28,492 B (99.4%) and the I2C
build, at 28,942 B, does not link. If you rebuild these sources in the Arduino IDE, or in your own
project file, **you will not get those flags**. Use this project file, and build the I2C variant
as `-e PSIPro-i2c` (it also leaves out the six contract looks that do not fit).

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
- **Travis Cook** — the contract layer in `src/contract/`, the WDP device announce in `src/wdp/` and
  `scripts/wdp_fw.py`, and `test/host/`.
