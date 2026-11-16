# Bench record: R1 (contract v1.3, first release)

**Status: not run.** No image from this tree has been flashed to a PSI Pro. This file is the list to
work through at the bench and the place to record it. Until every line below has a result, every
claim about R1 is about the host, the cross-compiler and the simulator.

## What R1 is

| | `PSIPro` (default) | `PSIPro-i2c` |
| --- | --- | --- |
| Intake | serial header | serial header and I2C (address 22) |
| Contract layer (the light show) | yes, v1.3, shared core CA1.5 | **no**: a `!` line means nothing to it |
| Looks | solid, off, flash, pulse, scan, sparkle, rainbow, meter, `native:<n>` | native modes only |
| Stand-ins | comet, chase, wipe draw as scan; twinkle as sparkle; gradient, colorcycle as solid | none |
| WDP caps | `droidnet.contract`, `contract.1.3s` | none |
| Flash | 27,866 B (97.2 %) | 16,888 B (58.9 %) |

Both builds carry all 23 upstream modes and mode 22, the exact `millis()` credit, the 200 µs latch gap,
the bounded command buffer and the WDP announce. The images are built from a clean checkout with
`pio run -e PSIPro -e PSIPro-i2c`; they are not in `firmware/` yet.

Known limits of R1, to have in mind while watching:

- A pulse over the board's own look is on the panel for at least 170 ms, and for 340 ms when it made
  the panel brighter: the return to the board's look waits for the gate.
- A look or a colour that follows another within 170 ms is held until then. The look before it stays up.
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
| 12 | The gate (S9, S11): in a show, `!P*A:i=solid,c=0000ff` and `!P*A:i=solid,c=ffffff` alternating as fast as they can be sent; then white, `!P*X`, white again at once | The colour changes about six times a second at most, never a flicker. The second white comes a third of a second after the first | |
| 13 | Native looks in a show (S10): `!P*A:i=native:15`, `!P*P:c=ffffff,d=100`, `!**B:b=40`, `!P*L:v=0`, `!P*A:i=native:92`, `!P*L:v=255` | Knight Rider at the show's level; the pulse draws nothing; dimmer; the VU meter keeps its own bars whatever `L` says | |
| 14 | Stand-ins: `!P*A:i=comet,c=00ff00`, `!P*A:i=twinkle,c=00ff00`, `!P*A:i=gradient,c=00ff00` | A green scan; a green sparkle; solid green | |
| 15 | Serial under load: with `!P*A:i=meter` running, the 16-line load burst back to back at 9600 baud, ten times | `!PFQ:r=0` reads `ok=1` every time | |
| 16 | Scope, if available: PD4 | T0H 250 ns, T1H 750 ns, 1.25 µs bits, at least 280 µs low between frames | |

## The I2C image

| # | Step | Expect | Result |
| --- | --- | --- | --- |
| 17 | Flash `PSIPro-i2c`; watch the serial header | The swipe. `@WDP1 {"type":"PSI Front","fw":"<commit>"}` with no caps | |
| 18 | Step 2 again, over serial | As step 2 | |
| 19 | `0T6`, `0T15`, `0T0`, `0T1` from an I2C master at address 22 | Each mode runs | |
| 20 | `!P*A:i=solid,c=ff0000`, `!**M:v=show`, `!P*X`, `!PFQ:r=0` over serial | Nothing changes on the panel and nothing is answered | |
| 21 | `0T92` for 5 minutes beside a stopwatch, then `0T6\|5` | The timed mode ends after 5 s by the stopwatch | |

## When it is done

Only when every row has a result: copy both `.hex` to `firmware/` with the commit in the name, update
`firmware/manifest.json` (sha256, sizes, `contract: 1.3`), tag `v1.3-r1-bench`, and change the status lines
in `README.md` and `DROIDNET_FORK.md`.
