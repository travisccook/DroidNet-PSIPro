# PSI Pro Animations as Data — Design Spec (Architecture B)

Date: 2026-07-14 · Status: **approved direction, pre-implementation** · Decision record:
Travis approved Architecture B (full bytecode VM). Escape-hatch levers (CDC-off, FastLED
swap) deferred; bootloader swap parked. This spec defines WHAT gets built; the
implementation plan (separate document) defines the step-by-step HOW.

All flash numbers in this spec are measured (`avr-size` on real `pio run` artifacts) from
the sizing probes under `~/Repos/DroidNet-PSIPro-probes/` (esp. `vm-extended`, commit
945d897). Probes are sizing instruments: the production VM is **re-derived under the
golden-frame harness, not merged from the probe**.

## 1. Goal

Animations on the PSI Pro become PROGMEM data played by one interpreter, so that:

- All five CONTRACT_SLIM-dropped modes (7 i_heart_u, 9-front red_heart, 11 Imperial
  March, 19 Lightsaber Battle, 20 Star Wars Intro) are restored **byte-for-byte**, in
  BOTH build envs (serial-only and I2C).
- A future animation costs tens of bytes of data (measured probe mean ~59 B + 48 B per
  new bitmap frame), not hundreds-to-thousands of bytes of C.
- Target envelope (probe-measured, +~100 B tolerance for byte-exactness fixes):
  serial-only ≈ 26,274 B (91.6%), I2C ≈ 27,716 B (96.7%). SRAM +~9 B.

## 2. Non-goals (explicitly deferred, all compatible later)

- **No CDC_DISABLED**, no UART bootloader-entry token (measured −924 B; add later).
- **No FastLED replacement** (measured −2,232 B; add later, after a simavr verification
  stage exists).
- **No bootloader change** (physical ISP — parked pending explicit approval).
- **No strand-effect trim** (effects stay; fences measured but not shipped).
- **No contract_core.h changes of any kind** — the file stays byte-identical across the
  three forks. Everything in this spec is board-local.
- No wire-format/grammar changes; no Studio-side work (follow-up in the
  DroidNetSignalBooster repo: surface restored modes in the Studio native-look pool).

## 3. Component 1 — golden-frame harness (built FIRST)

Host-only test instrument proving byte-exact fidelity. Zero production-file changes.

- **New files** under `test/host/`: `native_mocks/Arduino_host.h` (mocked millis via
  `g_mock_millis`, exact avr-libc Park–Miller `random()` reimplementation + Arduino
  WMath wrappers, digitalRead/pinMode/analogRead-const/map, Stream/Serial1 with
  scriptable RX FIFO, PROGMEM shims — `pgm_read_dword` deliberately defined as a 1-byte
  read to mirror observable AVR behavior and stay sanitizer-clean), `native_mocks/FastLED.h`
  (CRGB/CHSV with `%=`, `*=`, `nscale8_video`, u32 ctor, HTMLColorCodes incl.
  `Green=0x008000`; `CFastLED.show(scale)`/`show()`/`clear()`; startup exhaustive
  256×256 self-check of scale8/qmul8 against the FastLED 3.5.0 semantics),
  `native_mocks/EEPROM.h`, `golden_native.cpp` (does `#include "../../src/main.cpp"`
  unmodified; CLI: mode/cmd, duration, seed, jumper, pot, eeprom image, output path),
  and a comparison step (`cmp` + first-divergence pretty-print).
- **Tick model**: 1 kHz mocked clock; each iteration runs `loop(); serialEventRun();`
  exactly like the Arduino core. **A frame is a `show()` call**, captured as
  `{t_ms, scale, 48×(r,g,b)}` — raw staging buffer + scale byte (what crosses the
  `showLeds()` boundary). Mode selection by stuffing `0T<n>\r` into the mocked Serial1.
- **Golden capture matrix**: ~30 files — modes 0–21 + 92, front jumper; rear replicates
  where behavior differs (modes 1, 9); alwaysOn both ways for the restore-to-default
  transition; two canonical EEPROM images. A **config-shadow build** (CONTRACT_SLIM
  sed-stripped from a copied config.h, `-I` precedence) captures the five dropped modes
  from Neil's ORIGINAL C. Goldens are committed; a new run.sh stage regenerates and
  `cmp`s them on every run (mock drift breaks CI, not a future rewrite).
- **Determinism**: single PRNG (avr-libc `random()`, seed 1, never reseeded — verified
  by ELF disassembly), constant `analogRead`, recorded EEPROM image, header records all
  parameters. The golden contract is the harness tick model (1 kHz ideal millis,
  zero-cost show) — legitimate because the board can never be observed; the goal is
  re-implementation parity under a shared clock.
- The existing `mock_psi.h` is NOT reused (it defines the very functions main.cpp
  defines and models latches, not pixels); it stays untouched for the contract suite.

## 4. Component 2 — the animation VM (board-local)

- **New header `include/psi_vm.h`** (ours, MIT): one interpreter (`vmPlay`-shaped,
  probe-measured ≈ 1.6 KB all-in) dispatched from `runPattern`, playing PROGMEM
  programs. Op vocabulary (from the probe; final op list may be adjusted during
  golden-gated implementation): SHOW_FRAME(bitmap, palette), FILL_COLS(mask, color),
  FILL_ROW(row, color, scale), PIXOFF_LIST, FILL_ALL, CLEAR, half-column group fill
  (radar), sweep steps, SPARKLE(n, color), PER_LED_SCALE_RAND / PER_LED_MUL_RAND
  (FadeOut), RAND_DELAY, per-step delay (10 ms units), loop/end. Color operands support
  role indirection (primary/secondary jumper colors) plus explicit palette entries.
- **Lifecycle parity**: the interpreter reuses Neil's own helpers (set_delay/checkDelay,
  set_global_timeout, loopsDone/globalTimerDone restore paths, brightness()) so
  loops/runtime/commandTiming/alwaysOn semantics are identical, verified by goldens —
  including quirks (FadeOut's loop tail, radar's one-tick blanks, the 26 ms gate).
- **Encoded as data (17 mode families)**: the five dropped modes + flash/alarm/scream
  (2/3/5), Leia (6), radar (8), SW-title (10), DiscoBall (12/13), rebel (14), Knight
  Rider (15), Pulse (9-rear), FadeOut (4). Their native bodies are deleted once their
  bytecode passes frame-exact replay. Bitmaps stay in `matrices.h` unchanged (the five
  dropped modes' art returns via reference, 48 B/frame).
- **Stays native by design**: `swipe` (mode 1 — weighted-random default pattern, a real
  program), `VUMeter` (21/92 — deliberately contract-entangled for Studio-driven
  levels), `allON`/`allOFF` (primitives). `displayMatrixColor` stays as the blitter
  (now called from ONE site).
- **Contract integration**: CE_SCAN / CE_SPARKLE re-point at VM-backed shims inside
  `ContractPSI.h` (probe showed `_renderLook` shrinks); restored modes become
  score-addressable via the existing `native:<n>` escape. No new verbs, no core edits.
- **CONTRACT_SLIM dies**: the config.h hatch and the four `#ifndef` fences are removed;
  config.h/README text replaced with the VM story (the "five modes dropped" divergence
  note becomes historical).
- **Fallback rule**: any mode that cannot pass frame-exact replay stays native (accept
  its native size). Probe evidence says none is expected to fail.

## 5. Testing & guardrails (nothing weakened — renegotiated honestly)

- New: VM decode-walk host test (op-stream structural check) + per-mode golden parity
  (`cmp`) as run.sh stages; goldens for both slim-parity and full sets.
- Updated honestly (each is a real behavior change to acknowledge, not a test dodge):
  contract latch guard learns the VM shims for CE_SCAN/CE_SPARKLE; host mocks for
  ContractPSI.h compile against the VM header; `stack_report.py` re-baselined (bound
  must remain SOUND on the serial-only env); run.sh stage-4 size banners and
  platformio.ini size-table comments updated to the new measured numbers.
- Both envs must build and fit on every run (run.sh already enforces this).
- `contract_core.h` diff must be empty vs all three sibling forks at every commit.
- Public-fork hygiene: all new code is ours (MIT, existing notice structure); Neil's
  animation *behavior* is preserved byte-for-byte — his code is replaced only where the
  goldens prove equivalence.

## 6. Acceptance criteria

1. All 22 upstream display modes present and selectable in BOTH envs (five restored).
2. Every VM-encoded mode's golden replay is `cmp`-identical to Neil's original C
   (captured pre-conversion via config-shadow), same seed/clock/config.
3. Full host suite green (341 checks + fuzz + new stages); both envs link with
   serial-only ≤ ~26.4 KB and I2C ≤ ~27.8 KB; stack bound re-verified sound.
4. A demonstration "new animation" added as pure data (no interpreter change) to prove
   the marginal-cost claim, then removed or kept per taste.
5. Nothing pushed until Travis says so; hardware flash remains a separate, explicit
   later step (nothing in this spec claims bench verification).

## 7. Follow-ups recorded, out of scope here

CDC-off + `0Z9999` re-flash token (incl. Signal Booster flash-flow step), clean-room
WS2812 driver + simavr verification stage, Studio native-look pool hookup, upstream
offer to Neil (VM concept + the `displayMatrixColor` `pgm_read_dword` OOB find) after
hardware validation, kp_boot_32u4 (needs physical-ISP approval).
