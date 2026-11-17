# Bench record: R1 (contract v1.3, first release)

**Status: not run.** No image from this tree has been flashed to a PSI Pro. This file is the list to
work through at the bench and the place to record it. Until every line below has a result, every
claim about R1 is about the host, the cross-compiler and the simulator.

## What R1 is

| | `PSIPro` (default) | `PSIPro-i2c` |
| --- | --- | --- |
| Intake | serial header | serial header and I2C (address 22) |
| Contract layer (the light show) | yes, v1.3, shared core CA1.7 | **no**: a `!` line means nothing to it |
| Looks | solid, off, flash, pulse, scan, sparkle, rainbow, meter, `native:<n>` | native modes only |
| Stand-ins | comet, chase, wipe draw as scan; twinkle as sparkle; gradient, colorcycle as solid | none |
| WDP caps | `droidnet.contract`, `contract.1.3s` | none |
| Flash | 28,286 B (98.7 %) | 16,998 B (59.3 %) |

Both builds carry all 23 upstream modes and mode 22, the exact `millis()` credit, the 200 µs latch gap,
the bounded command buffer and the WDP announce. The images are built from a clean checkout with
`pio run -e PSIPro -e PSIPro-i2c`; they are not in `firmware/` yet.

Known limits of R1, to have in mind while watching:

- A pulse over the board's own look is on the panel for at least 170 ms, and for 340 ms when it made
  the panel brighter: the return to the board's look waits for the gate.
- A look or a colour that follows another within 170 ms is held until then. The look before it stays up.
- A frame the gate holds is not sent at all: the panel keeps what it shows, for up to
  340 ms, and the look then appears at its level. Nothing is shown dimmed in between.
- **A part of the panel.** The gate measures the whole panel. What is held for a part of it: `L`,
  `B` and `b=` go up at most once in 340 ms (one window for all three); the meter with no `L`
  redraws every 170 ms at the fastest; a scan laps in a second or more; a change of colour alone
  does not restart a look. What is NOT held: a look that moves by itself followed by a blackout
  and a full look. The script: `A:i=meter` with no `L`, `X` half a second later, a solid 60 ms
  after that, again and again: a half or a band of the panel lit twice 182 ms apart, and four
  times in one second, in 4 of 8 runs. (Two looks by turns every 170 ms, a scan and a solid, the
  meter and a solid, a scan and the fed meter, hold here: this panel's scan is one column, under
  a quarter of it. On the logics displays those scripts do not hold.) Each look by itself, and
  the 50 scripts of the host's property test, keep every half, quarter and band of the panel to
  340 ms between two onsets; in its random streams (27 seeds, 3,000 streams each) one part of
  the panel lit twice 104 ms apart at the closest, and never more than three times in one
  second. Closing it takes a gate with a state for each region, which no board of the family has.
  Watch for it at steps 12, 22 and 33.
- **A pulse the gate never lets out** (it would light the panel inside 340 ms of the board's own
  look lighting it, and ends before it may) still stops the board's look for as long as the pulse
  lasts, and a look that builds up (the processing sweep) loses what it had built. Rare: 6 of
  2,112 single pulses in the host's scan.
- **The build with all the looks is not R1.** No environment links it and `run.sh` does not put
  it through the property test. In it, a colour observer sees a change of look under 170 ms in
  some random streams, each time with the comet or the chase on the panel. To settle before the
  six looks come back.
- A look is taken to be on the panel from its first frame with an LED at 4 levels or more (of
  255). At a brightness under 4 nothing the contract draws counts as a change of look.
- `L`, `B` and `b=` follow a raise with up to 340 ms of delay (the newest raise is the one taken),
  and a scan cannot be made faster than a second there and back: that is the price of the hold.
- Against upstream v1.7 the native modes differ in three listed places only (README, "Every
  native command of the original firmware still works"): `0T19|n` plays the battle whole, the
  tail of short circuit under `alwaysOn`, and, on the I2C image, 70 of 441 pairs of modes when
  the second command lands inside the first mode's redraw (upstream's own new mode is then a
  step off; the fork's starts at its first step).
- The six strip and hue looks draw as stand-ins.
- A droid that drives its PSI over I2C has no light show on that board.

## Before anything else: brightness

A bare PSI Pro on the bench, not in the droid. On USB power the panel can draw more than the port
supplies. Neil's rule is 20 or less. Send `2P20` and `1P1` over serial (internal brightness 20),
power-cycle, and confirm the swipe is dim. With R1 a contract line cannot raise it; with any earlier
image it can, so never send a `!` line to a pre-R1 image on USB power. Use a 5 V supply rated for 3 A for
anything brighter.

Record for each step: pass or fail, what was seen, and a short phone video. Serial-only image first.

## The serial-only image

| # | Step | Expect | Result |
| --- | --- | --- | --- |
| 1 | Flash `PSIPro` through the DroidNet Flash tab; watch the serial header | The swipe runs: blue with red, front jumper open. `@WDP1 {"type":"PSI Front",...,"caps":["droidnet.contract","contract.1.3s"]}` within 3 s and every 25 to 30 s | |
| 2 | Native regression: `0T1` to `0T21`, `0T92`, `0T22`, one by one | Each mode as upstream draws it. Knight Rider and Leia visibly sweep (if they stay black the latch gap is too short: record it). The rebel symbol appears at once after `0T9`. `0T19\|3` plays the whole battle. `0T6\|5` returns to the swipe after 5 s | |
| 3 | `!P*A:i=solid,c=ff0000`, then the same with `,b=255` | Solid red at the swipe's brightness, not brighter (S6); no change with `b=255` | |
| 4 | `!**M:v=show`, `!P*A:i=solid,c=ff0000`, `!**B:b=5`, `!P*X`, `!**M:v=idle` | Red; dimmer; black, and it stays black; the swipe | |
| 5 | Pulse outside a show (L3): `!P*P:c=ffffff,d=300` | One white flash, then the swipe at its own brightness | |
| 6 | Seeded clock (S1, S2): `!**C:bpm=120,bpb=4,beat=0`, `!P*A:i=solid,c=0000ff,m=200,am=2`; then `!**C:bpm=600`; then `!**C:bpm=240,bpb=4,beat=0` | Two pulses a second; nothing changes; still two pulses a second (every second beat) | |
| 7 | Timebase (C4): `!**C:bpm=60,bpb=4,beat=0` with the same look, filmed beside a stopwatch for 5 minutes | Pulse 300 lands 300.0 s after pulse 0, within 0.3 s. (The old credit would put it at 295 s) | |
| 8 | Pushed score with V: `!**M:v=show,wd=15`, three scored lines, the matching `!P*V`, `!PFQ:r=0`; then `C`; then again with one scored line left out | The panel stays on the swipe until `C`; the reply reads `cap=16`, `n=3`, `ok=1`; sections change on their beats. With a line missing: the swipe returns and the reply reads `ok=2` | |
| 9 | Corrections (C1, C2): during the show, `!**C:ph=<ms>,beat=<n>` by hand a few times | No extra flash, no step back to the previous section | |
| 10 | Watchdog (L2): stop sending | After 15 s the swipe returns | |
| 11 | Idle restore (L1): `!**M:v=show`, `!P*A:i=native:15`, `!**M:v=idle`; then in a show `!P*X`; then in a show `0T1` | The swipe, not Knight Rider. Dark until `!**M:v=idle`. The swipe at once | |
| 12 | The gate (S9, S11): in a show, `!P*A:i=solid,c=0000ff` and `!P*A:i=solid,c=ffffff` alternating as fast as they can be sent; then white, `!P*X`, white again at once | The colour changes about six times a second at most, never a flicker. The second white comes a third of a second after the first, and the panel is black until then, not dim | |
| 13 | Native looks in a show (S10): `!P*A:i=native:15`, `!P*P:c=ffffff,d=100`, `!**B:b=40`, `!P*L:v=0`, `!P*A:i=native:92`, `!P*L:v=255` | Knight Rider at the show's level; the pulse draws nothing; dimmer; the VU meter keeps its own bars whatever `L` says | |
| 14 | Stand-ins: `!P*A:i=comet,c=00ff00`, `!P*A:i=twinkle,c=00ff00`, `!P*A:i=gradient,c=00ff00` | A green scan; a green sparkle; solid green | |
| 15 | Serial under load: with `!P*A:i=meter` running, the 16-line load burst back to back at 9600 baud, ten times | `!PFQ:r=0` reads `ok=1` every time | |
| 16 | Scope, if available: PD4 | T0H 250 ns, T1H 750 ns, 1.25 µs bits, at least 280 µs low between frames | |
| 22 | The meter and `L` (review finding 1): in a show, `!P*A:i=meter,s=255`, then `!P*L:v=255` and `!P*L:v=128` alternating every 50 ms for 10 s; then stop sending `L` and wait 3 s | The top half of the panel lights about three times a second at the most, never a flicker; it follows `L` (it is not stuck). With no `L` the bars walk by themselves, about six steps a second | |
| 23 | A pulse over a mode that has ended (review finding 2): blank EEPROM (`0P1`), `0T19`, wait until the battle has ended and its last frame stands, `!P*P:c=ffffff,d=170`; then the same over `0T4` once it has faded | One white pulse, then the battle plays again from its start (short circuit: the fade again). White never stays on the panel, and `!**M:v=idle` is not needed to get the board back | |
| 24 | The same mode again during a show: `0T19`, wait for its end, `!**M:v=show`, `!P*A:i=solid,c=ff0000`, `0T19` | The red goes and the battle plays | |
| 25 | A `D` for another board (review finding 4): in a show with a solid look, `3D`, then `9A`; then `0D` | Nothing changes for `3D` and `9A`; `0D` ends the show and the swipe returns | |
| 26 | A stale timing (review finding 5): `0T6\|1`, wait for the swipe, then `!**M:v=show`, `!P*A:i=native:22` | The processing sweep runs on, lap after lap; it does not restart every second | |
| 27 | A look that comes up slowly (CA1.6): in a show at brightness 200 (`3P200` first, on a 3 A supply), `!P*A:i=off`, `!P*A:i=pulse,c=ff0000,s=0,b=80`, and 171 ms later `!P*A:i=solid,c=00ff00,b=80` | Red comes up for about a quarter of a second before the green: the green does not cut in on a red that has only just become visible | |
| 28 | A timing sent to a running short circuit (review finding 7): `0T4`, and one second later `0T4\|20`; then `0T19` and `0T23\|2` during the battle | The fade goes on to its end as if nothing had been sent. The battle stops at its next step and the swipe returns, as upstream's does | |
| 31 | A pulse as the battle ends, `alwaysOn` off (second review, N4): `0P0`, `0T19`, and `!P*P:c=ffffff,d=170` in the half second its last frame stands (about 5.5 s after the command) | The swipe is back when it would have been without the pulse, give or take the pulse: the battle is not played again (it was: the swipe came back 5 s late). Then `0P1` again | |
| 33 | `B` under a meter (second review, N1; core CA1.7): in a show, `!P*A:i=meter,s=255`, `!P*L:v=86` once a second, and `!P*B:b=0` / `!P*B:b=200` alternating every 30 ms for 10 s | The two bottom rows light about three times a second at the most, never a flicker (they lit 17 times a second before). They do follow `B` | |
| 34 | The fastest scan (core CA1.7): `!P*A:i=scan,c=00ff00,s=255`, filmed for 10 s | The column goes there and back in one second: ten laps in the ten seconds, not eighteen | |
| 35 | A change of colour alone (core CA1.7): `!P*A:i=scan,c=00ff00,s=128`, then `!P*A:i=scan,c=ff0000,s=128` at any moment | The column turns red where it is and goes on; it does not jump back to the first column | |

## The I2C image

| # | Step | Expect | Result |
| --- | --- | --- | --- |
| 17 | Flash `PSIPro-i2c`; watch the serial header | The swipe. `@WDP1 {"type":"PSI Front","fw":"<commit>"}` with no caps | |
| 18 | Step 2 again, over serial | As step 2 | |
| 19 | `0T6`, `0T15`, `0T0`, `0T1` from an I2C master at address 22 | Each mode runs | |
| 20 | `!P*A:i=solid,c=ff0000`, `!**M:v=show`, `!P*X`, `!PFQ:r=0` over serial | Nothing changes on the panel and nothing is answered | |
| 21 | `0T92` for 5 minutes beside a stopwatch, then `0T6\|5` | The timed mode ends after 5 s by the stopwatch | |
| 29 | A mode command over I2C while another mode draws (review finding 3): `0T19` over serial, then `0T15` from the I2C master, twenty times, at random moments of the battle (start the battle again each time); the same with `0T6` then `0T15`, and `0T15` then `0T6` | Each time the new mode takes the panel and keeps it. Never one step of the new mode and then the old one again (that was the fault: the old mode went on under the new mode's number) | |
| 30 | Steps 23 (without the `!` line: `0T19`, wait, `0T19` again) and 28 on this image | The battle plays again; as step 28 | |
| 32 | The rebel symbol and an I2C command (second review, N2): `0T14` over serial, then `0T6` from the I2C master, a few times | Leia's message starts and runs at its own pace from its first row (its second row came 26 ms after the first when the command landed inside the symbol's redraw; by eye: no stutter at the start) | |

## When it is done

Only when every row has a result: copy both `.hex` to `firmware/` with the commit in the name, update
`firmware/manifest.json` (sha256, sizes, `contract: 1.3`), tag `v1.3-r1-bench`, and change the status lines
in `README.md` and `DROIDNET_FORK.md`.
