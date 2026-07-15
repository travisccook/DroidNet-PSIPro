# FastLED → clean-room WS2812 driver: verification report

Branch `feature/fastled-driver`. This records what was proven for the swap of
`fastled/FastLED @ 3.5.0` for the from-scratch driver `include/FastLED.h`.

**Status: cross-compiled + host-proven, NEVER FLASHED.** Every result below comes
from the host toolchain and the linked ELF. Nothing here has been on hardware; the
README's STOP section still governs the first flash.

## What changed

Two files only:

- `platformio.ini` — `fastled/FastLED @ 3.5.0` dropped from `lib_deps`.
- `include/FastLED.h` — new. Occupies the `<FastLED.h>` include name, so
  `src/main.cpp`, `include/functions.h`, `include/psi_vm.h` are **byte-identical**
  (they still `#include <FastLED.h>`). On AVR that name resolves to this file; the
  host golden harness puts `-I test/host/native_mocks` first, so `<FastLED.h>` still
  means the mock there. `src/contract/contract_core.h` is byte-identical
  (`2abb1ec9…`), as required across the three forks.

Plus the permanent test additions: `test/host/test_scale_oracle.cpp`,
`test/host/test_hsv_oracle.cpp`, wired into `test/host/run.sh` as stages 7 and 8.

## 1. Golden gate — UNCHANGED

All **31/31 goldens byte-identical** after the swap. The goldens compile against the
mock and capture raw `leds[]` + the scale byte at the `show()` boundary, so they gate
staging semantics, not the driver — which is exactly why the swap cannot perturb them,
and does not.

## 2 & 3. Byte-exactness vs FastLED 3.5.0 (exhaustive)

The driver's math was diffed against the **actual vendored FastLED 3.5.0 source**
(`lib8tion/scale8.h`, `math8.h`, and the verbatim-extracted `hsv2rgb_rainbow`), and
against the harness mock, over the full input domain. `-fsigned-char` matches
avr-g++'s signed `char`.

| Operation | Domain | vs FastLED 3.5.0 | vs mock |
| --- | --- | --- | --- |
| `scale8` (FIXED) | 65,536 | 0 diffs | (mock n/a) |
| `scale8_video` | 65,536 | 0 diffs | (mock n/a) |
| `qmul8` | 65,536 | 0 diffs | 0 diffs |
| `nscale8x3_video` | 65,536 | 0 diffs | 0 diffs |
| `hsv2rgb_rainbow` (CHSV→CRGB) | 16,777,216 (full cube) | 0 diffs | (mock wheel differs by design) |

`driver == mock` ⇒ every golden transfers to the driver. `driver == FastLED` ⇒ the
panel sees exactly what stock FastLED drew. The committed stages 7/8 hold this with
self-contained references (cited to FastLED file:line) that were verified identical to
the vendored source.

**Master brightness.** With the firmware's default (uncorrected) correction/temperature,
FastLED's per-channel adjustment reduces to exactly `scale`
(`computeAdjustment(scale, 0xFFFFFF, 0xFFFFFF) == scale`, controller.h:150), and its
temporal dither term is **forced to zero** because FastLED disables dithering whenever
the measured frame rate is < 100 fps (`FastLED.cpp:58`) and this firmware is gated to
~40 fps. So FastLED's wire value is `scale8(channel, scale)` — what `show()` computes.

**CE_RAINBOW decision: UNCHANGED.** `hsv2rgb_rainbow` is byte-identical to FastLED's
across the entire HSV cube, so CE_RAINBOW's hue mapping — and the Studio visualiser's
JS rainbow-parity vectors — are preserved. No divergence, no STOP.

## 4. WS2812 timing — objdump cycle-count over the linked ELF

simavr is not packaged for this host (no Homebrew formula), so the proof is the
**cycle-count over the real linked `firmware.elf`**, not the source. The emitter is
emitted verbatim (ATmega32U4 @ 16 MHz, 62.5 ns/cycle, PD4):

| Symbol | ELF cycles | Time | WS2812 spec window | In spec |
| --- | --- | --- | --- | --- |
| T0H (0-bit high) | 4 | 250 ns | 350 ±150 → [200, 500] | ✓ |
| T1H (1-bit high) | 12 | 750 ns | 700 ±150 → [550, 850] | ✓ |
| bit period (mid-byte) | 20 | 1250 ns | ~1250 ns | ✓ |
| bit period (byte boundary) | 23 | 1437.5 ns | LOW-tail stretch, < 50 µs latch | ✓ |

The disassembly (`sbi/cbi 0x0b,4`, `lsl`, `brcs`, `nop×`, `dec`, `breq`, `ld Z+`,
`rjmp/brne`) contains no compiler-inserted instructions inside the asm block. FastLED's
WS2812 targets T0H≈250 ns / **T1H≈875 ns** (`C_NS(250),C_NS(625),C_NS(375)`); the
driver's T1H is 750 ns — a different, also in-spec waveform carrying identical data.
**Whether the specific LEDs accept 750 ns T1H is a bench item.**

## 5. Interrupt budget

`FASTLED_ALLOW_INTERRUPTS == 0` on AVR (`led_sysdefs_avr.h:23`): stock FastLED already
masks interrupts for the **whole frame** on this board. So does the driver.

- Driver interrupt-off window: 48×3 bytes ≈ **1.467 ms** (≈23,473 cy from the ELF).
- FastLED window: 48×24×20 cy ≈ **1.44 ms**. Driver is +~27 µs (+1.9 %), same class,
  from the byte-boundary LOW-tail. The pre-scale staging runs with interrupts **on**
  (before `cli()`), so it is not in the window.
- Serial JEDI RX at 9600 baud: byte time = 10 bits / 9600 ≈ **1.04 ms**. The 32U4 USART
  is 2-deep (shift register + UDR), so a byte is lost only if a third byte begins before
  the ISR runs — a **~2.08 ms** threshold. 1.47 ms of masking < 2.08 ms ⇒ at most two
  bytes buffer during a frame, none lost. FastLED's 1.44 ms leaves the same margin.

**Bar "no worse than FastLED": met.** The driver also *improves* on FastLED here by
using an SREG save/restore instead of an unconditional `sei` (see §6).

## 6. Stack bound — both envs (headline)

`test/host/stack_report.py` over the linked ELF:

| Env | Stack worst case | Headroom | Soundness |
| --- | --- | --- | --- |
| PSIPro (serial-only) | 329 B | 857 B | SOUND — no ISR reaches a `sei` |
| PSIPro-i2c **(FastLED, before)** | 441 B nominal / **unbounded** | — | **NOT SOUND** — TWI + serial ISRs reach FastLED's unconditional `sei` inside `ClocklessController`; one nesting level = 596 B |
| PSIPro-i2c **(clean-room, after)** | **423 B** | 482 B | **SOUND** — no ISR reaches a `sei`, ISRs cannot nest |

This is the `psi-i2c-isr-nesting-hazard` closed **structurally**: the driver masks with
`SREG` save/restore, so a render reached from the TWI ISR keeps the I-bit clear and the
ISR cannot re-enter itself. The stack tool independently certifies the difference (FastLED
build fails its soundness check; clean-room build passes).

## 7. Sizes (real `pio run`, `avr-size`)

| Env | Flash before | Flash after | Δ | of 28,672 B | SRAM before | SRAM after | Δ |
| --- | --- | --- | --- | --- | --- | --- | --- |
| PSIPro | 26,760 | **24,598** | **−2,162** | 93.3 % → 85.8 % | 1,309 | 1,374 | +65 |
| PSIPro-i2c | 28,202 | **26,040** | **−2,162** | 98.4 % → 90.8 % | 1,590 | 1,655 | +65 |

The flash-tight i2c config gains 2,162 B (470 B → 2,632 B headroom). SRAM cost is +65 B
(the 145 B `sWire` pre-scale buffer, net of FastLED's own SRAM) — a good trade on a
flash-bound board. `contract_core.h` byte-identical at every commit.

## 8. Semantic differences vs FastLED (disclosed)

| Difference | Evidence | Effect |
| --- | --- | --- |
| No temporal dithering | FastLED disables it < 100 fps (`FastLED.cpp:58`); firmware ~40 fps | **None** — FastLED's dither is already inert here; output is identical |
| No min-refresh busy-wait | FastLED uses `CMinWait<10>` ≈ 10 µs (< WS2812 ~50 µs reset); firmware frame cadence is `millis()`-gated to ≥ tens of ms | **None at top level.** The only sub-reset show pairs are Neil's intentional transient-black frames (`clear();show();…fill;show()`), identical under FastLED |
| T1H 750 ns vs FastLED 875 ns | §4 | Different in-spec waveform, same data — **bench-confirm acceptance** |
| Interrupt window +~27 µs | §5 | 1.9 % longer mask than FastLED; no byte loss shown |
| SRAM +65 B | §7 | Within budget (max 64.6 % on i2c) |
| millis clock-correction approximate | mirrors FastLED (`clockless_trinket.h:130`); adds back masked time beyond one queued tick | Sub-ms; does not affect rendered frames |

## What only real hardware can confirm

- The WS2812 LEDs latch and display correctly at T0H 250 ns / T1H 750 ns.
- No visible glitch on the intentional transient-black show pairs.
- Serial JEDI commands are not dropped during a render on the physical bus.
- (i2c build) a real I2C master is still answered with interrupts masked per frame.

All host-provable claims above hold. The remaining gate is a bench flash + scope.
