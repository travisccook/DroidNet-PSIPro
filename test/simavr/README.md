# simavr probes

Five small PlatformIO projects that run in simavr, a simulator of the ATmega32U4. `test/host/run.sh` builds and
runs them when PlatformIO and its simavr package are installed, and gates on four of them. The host mocks have an
ideal clock and a free `show()`; these are the tests that see real timer and interrupt behaviour.

```bash
pip install platformio
pio pkg install -g --tool platformio/tool-simavr      # ~/.platformio/packages/tool-simavr/bin/simavr
```

| Probe | What it runs | Gate |
| ----- | ------------ | ---- |
| `millis/` | The firmware, `src/main.cpp` verbatim, with one contract look at a time for 16 s each; prints `millis()` elapsed against Timer1, which a masked LED frame cannot disturb | `check_millis.py`: every look within the limit in ppm |
| `lifecycle/` | The firmware, `src/main.cpp` verbatim, through a show, `X`, `M:v=idle`, a native take-over, a pulse over the native look and a watchdog expiry; prints what is on the panel at 16 checkpoints | `check_lifecycle.py`: each checkpoint shows the swipe, the look, dark or the native mode it should |
| `gate/` | The firmware, `src/main.cpp` verbatim, fed two hostile line streams (a relight right after `X`; `X` and `M:v=idle` alternating) while simavr traces the LED pin | `check_gate.py`: decodes every frame off the wire and counts dark-to-lit onsets for three observers: never more than 3 a second, never two inside 340 ms, nothing above the owner's brightness |
| `wave/` | Three frames from `include/FastLED.h`, the last two with nothing between them; simavr traces PD4 to a VCD | `check_wave.py`: T0H, T1H, bit period, the wire bytes, and the gap between frames |
| `bench/` | Microseconds for `beatPosAt`, the envelope, each effect's per-frame math and one `show()` | none: printed for the record |

simavr prints what a sketch writes to `Serial1`. Each probe halts the simulated CPU when it is done (asleep with
interrupts off, which ends simavr); `run.sh` also gives each a `timeout`. The `millis`, `lifecycle` and `gate`
probes keep their tables in flash and set `board_upload.maximum_size = 32256`: the simulator has no bootloader,
and the firmware leaves too little flash and stack for a probe that does not.

What simavr does not model: the LEDs, the crystal's tolerance, and the bus. Those are bench items.
