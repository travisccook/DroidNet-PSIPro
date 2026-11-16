# simavr probes

Four small PlatformIO projects that run in simavr, a simulator of the ATmega32U4. `test/host/run.sh` builds and
runs them when PlatformIO and its simavr package are installed, and gates on three of them. The host mocks have an
ideal clock and a free `show()`; these are the tests that see real timer and interrupt behaviour.

```bash
pip install platformio
pio pkg install -g --tool platformio/tool-simavr      # ~/.platformio/packages/tool-simavr/bin/simavr
```

| Probe | What it runs | Gate |
| ----- | ------------ | ---- |
| `millis/` | The firmware, `src/main.cpp` verbatim, with one contract look at a time for 8 s each; prints `millis()` elapsed against Timer1, which a masked LED frame cannot disturb | `check_millis.py`: every look within the limit in ppm |
| `lifecycle/` | The firmware, `src/main.cpp` verbatim, through a show, `X`, `M:v=idle`, a native take-over and a pulse over the native look; prints what is on the panel at 13 checkpoints | `check_lifecycle.py`: each checkpoint shows the swipe, the look, dark or the native mode it should |
| `wave/` | Three frames from `include/FastLED.h`, the last two with nothing between them; simavr traces PD4 to a VCD | `check_wave.py`: T0H, T1H, bit period, the wire bytes, and the gap between frames |
| `bench/` | Microseconds for `beatPosAt`, the envelope, each effect's per-frame math and one `show()` | none: printed for the record |

simavr prints what a sketch writes to `Serial1`. The probes never exit; `run.sh` ends each with `timeout`. The
`millis` and `lifecycle` probes keep their tables in flash and set `board_upload.maximum_size = 32256`: the
simulator has no bootloader, and the firmware leaves too little flash and stack for a probe that does not.

What simavr does not model: the LEDs, the crystal's tolerance, and the bus. Those are bench items.
