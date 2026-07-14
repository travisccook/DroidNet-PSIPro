# PSI Animation VM Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the PSI Pro's hand-written animation C with PROGMEM bytecode played by one interpreter, restoring all five CONTRACT_SLIM-dropped modes byte-for-byte, gated by a golden-frame harness built first.

**Architecture:** Phase 1 builds a host-only golden-frame harness that compiles `src/main.cpp` unmodified against mock Arduino/FastLED/EEPROM headers and captures every `FastLED.show()` as a frame, with a disassembly-verified Park–Miller PRNG for determinism. Phase 2 lands the VM core (`include/psi_vm.h`). Phase 3 converts modes one family at a time — each conversion must regenerate its golden and `cmp` identical to the pre-conversion capture of Neil's original C before its native body is deleted. Phase 4 removes CONTRACT_SLIM and renegotiates guards/docs.

**Tech Stack:** avr-gcc via PlatformIO (`pio run`, envs `PSIPro` + `PSIPro-i2c`), host clang++ (`-std=gnu++17 -fsanitize=address,undefined -fsigned-char`), Python 3 for the golden differ, `test/host/run.sh` as the gate.

## Global Constraints

- Repo/worktree: work ONLY in `~/Repos/DroidNet-PSIPro/.claude/worktrees/animation-vm` (branch `feature/animation-vm`). Never push. Never touch `~/Repos/DroidNet-PSIPro`'s own checkout.
- `src/contract/contract_core.h` must not change by a single byte (verify `git diff --stat -- src/contract/contract_core.h` is empty before every commit).
- Both envs must build and fit at every commit: `pio run && pio run -e PSIPro-i2c` (`pio` = `$HOME/.platformio/penv/bin/pio`). Baseline: 25,754 B / 27,238 B. Final envelope (spec): serial-only ≤ ~26.4 KB, I2C ≤ ~27.8 KB.
- `test/host/run.sh` must stay green at every commit (existing stages 1–4, plus the new golden stage once it exists). Never weaken or delete a test; guard renegotiations must be explicit in the commit message.
- Byte-for-byte fidelity: a converted mode's regenerated golden must be `cmp`-identical to the committed golden captured from Neil's ORIGINAL C. If a mode cannot pass, it stays native (spec fallback rule) — do not ship an approximation.
- All new code is ours (MIT, per the repo's existing notice structure). No GPL code, no code copied from FastLED — semantics-matching re-implementations only.
- Sketch entropy: the ONLY PRNG is avr-libc `random()` (Park–Miller, default seed 1, never reseeded). The host mock must implement it exactly; never call host libc `random()`/`rand()`.
- Commit style: `test(host): …` / `feat(psi): …` / `docs: …`, ending with `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- Nothing here is ever flashed; never describe a build as bench-tested.

**Reference material (read-only):** the sizing probe at `~/Repos/DroidNet-PSIPro-probes/vm-extended` (branch `probe/vm-extended`) is the measured seed for `psi_vm.h` — re-derive from it, don't merge it. Known probe→production fixes: use `pgm_read_ptr` (not `pgm_read_word`) for pointer reads so the header compiles on 64-bit hosts; replace `FALL_THROUGH()` with `__attribute__((fallthrough))`; the probe's known fidelity gaps (FadeOut loop tail, sweep/radar blank frames, red_heart double-show) are resolved by the goldens in Phase 3.

---

## Phase 1 — Golden-frame harness (host-only; zero production changes)

### Task 1: Arduino + EEPROM mocks with PRNG self-test

**Files:**
- Create: `test/host/native_mocks/Arduino.h`
- Create: `test/host/native_mocks/EEPROM.h`
- Test: `test/host/test_native_mocks.cpp`

**Interfaces:**
- Produces: `g_mock_millis` (uint32_t), `g_mock_pins[32]`, `g_mock_analog`, `millis()`, `delay()`, `random(long)`, `random(long,long)`, `randomSeed()`, `digitalRead/pinMode/analogRead/map`, `sei/cli`, `byte`, `PROGMEM`/`pgm_read_byte`/`pgm_read_dword`(1-byte)/`pgm_read_ptr`, `Stream` with `feed()`, `Serial`, `Serial1`, `EEPROM.mem[1024]` (0xFF-filled), `EEPROM.read/write`. Later tasks include these via `-I test/host/native_mocks`.

- [ ] **Step 1: Write `test/host/native_mocks/Arduino.h`**

```cpp
// Host mock of the Arduino AVR surface the PSI Pro sketch touches.
// Golden-frame harness support — test instrument, not production code.
// NOTE: defines NOTHING that src/main.cpp defines (ODR audit: keep it that way).
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef uint8_t byte;
typedef bool boolean;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

// ---- clock: 1 kHz ideal millis, driven by the capture loop ----
inline uint32_t g_mock_millis = 0;
inline unsigned long millis() { return g_mock_millis; }
inline unsigned long micros() { return g_mock_millis * 1000UL; }
inline void delay(unsigned long ms) { g_mock_millis += (uint32_t)ms; }

// ---- exact avr-libc random(): Park–Miller minimal standard.
// Constants verified against the shipped firmware.elf disassembly
// (16807 / 127773 / -2836 / zero-seed 123459876, default seed 1).
// NEVER call host libc random()/rand() anywhere in the harness.
inline uint32_t g_avr_next = 1;
inline long avr_random_raw() {
  int32_t x = (int32_t)g_avr_next;
  if (x == 0) x = 123459876L;
  int32_t hi = x / 127773L;
  int32_t lo = x % 127773L;
  x = 16807L * lo - 2836L * hi;
  if (x < 0) x += 0x7FFFFFFFL;
  g_avr_next = (uint32_t)x;
  return (long)x;
}
inline void randomSeed(unsigned long s) { if (s) g_avr_next = (uint32_t)s; }
// Arduino WMath wrappers, verbatim semantics.
inline long random(long howbig) {
  if (howbig == 0) return 0;
  return avr_random_raw() % howbig;
}
inline long random(long howsmall, long howbig) {
  if (howsmall >= howbig) return howsmall;
  long diff = howbig - howsmall;
  return random(diff) + howsmall;
}

// ---- pins ----
inline uint8_t g_mock_pins[32] = {0};
inline void pinMode(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t pin) { return g_mock_pins[pin]; }
inline int g_mock_analog = 512;
inline int analogRead(uint8_t) { return g_mock_analog; }
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
inline void sei() {}
inline void cli() {}

// ---- PROGMEM ----
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t*)(a))
// Deliberately a ONE-BYTE read. The sketch calls pgm_read_dword on byte
// matrices and truncates into a byte; on AVR the observable value is the
// addressed byte, and a true 4-byte host read is an out-of-bounds ASan trap
// at row ends (main.cpp displayMatrixColor rows 2-6).
#define pgm_read_dword(a) ((uint32_t)(*(const uint8_t*)(a)))
// Pointer reads (VM program/bitmap tables) MUST use pgm_read_ptr, never
// pgm_read_word — host pointers are 64-bit.
#define pgm_read_ptr(a) (*(void* const*)(a))

// ---- serial ----
class Stream {
 public:
  uint8_t rx[512];
  int rxHead = 0, rxTail = 0;
  char txLog[1024];
  int txLen = 0;
  void begin(unsigned long) {}
  int available() { return rxTail - rxHead; }
  int read() { return (rxHead < rxTail) ? rx[rxHead++] : -1; }
  void feed(const char* s) { while (*s && rxTail < (int)sizeof rx) rx[rxTail++] = (uint8_t)*s++; }
  void print(const char* s) { while (*s && txLen < (int)sizeof txLog - 1) { txLog[txLen++] = *s++; } txLog[txLen] = 0; }
  void print(char c) { if (txLen < (int)sizeof txLog - 1) { txLog[txLen++] = c; txLog[txLen] = 0; } }
  void println(const char* s) { print(s); print('\n'); }
  void println() { print('\n'); }
};
using HardwareSerial = Stream;
inline Stream Serial;
inline Stream Serial1;
```

- [ ] **Step 2: Write `test/host/native_mocks/EEPROM.h`**

```cpp
// Host EEPROM mock: fresh-chip 0xFF image, seedable by the capture driver.
#pragma once
#include <stdint.h>
#include <string.h>
struct EEPROMMock {
  uint8_t mem[1024];
  EEPROMMock() { memset(mem, 0xFF, sizeof mem); }
  uint8_t read(int a) { return mem[a]; }
  void write(int a, uint8_t v) { mem[a] = v; }
};
inline EEPROMMock EEPROM;
```

- [ ] **Step 3: Write the failing test `test/host/test_native_mocks.cpp`**

```cpp
// Known-answer tests for the mock layer. The PRNG vector is the canonical
// minstd0 sequence from seed 1 — if this fails, every golden is a lie.
#include "native_mocks/Arduino.h"
#include "native_mocks/EEPROM.h"
#include <assert.h>
#include <stdio.h>

int main() {
  // Park–Miller from seed 1: canonical first five raw outputs.
  long expect[5] = {16807L, 282475249L, 1622650073L, 984943658L, 1144108930L};
  for (int i = 0; i < 5; i++) assert(avr_random_raw() == expect[i]);
  // Arduino wrapper semantics.
  g_avr_next = 1;
  assert(random(0) == 0);              // howbig==0 -> 0, consumes no draw
  assert(random(6) == 16807L % 6);     // modulo of the first raw draw
  assert(random(220, 250) == 220 + (282475249L % 30));
  // map() must use long math (AVR parity).
  assert(map(512, 0, 1023, 0, 255) == 127);
  // millis is externally driven.
  g_mock_millis = 42;
  assert(millis() == 42);
  // EEPROM fresh chip reads 0xFF.
  assert(EEPROM.read(0) == 0xFF);
  EEPROM.write(3, 7);
  assert(EEPROM.read(3) == 7);
  // pgm_read_dword is a one-byte read by design.
  static const uint8_t m[2] = {0x2A, 0xFF};
  assert(pgm_read_dword(&m[0]) == 0x2AUL);
  // Serial FIFO round-trip.
  Serial1.feed("0T2\r");
  assert(Serial1.available() == 4);
  assert(Serial1.read() == '0');
  puts("test_native_mocks: OK");
  return 0;
}
```

- [ ] **Step 4: Run test, expect PASS**

```bash
cd ~/Repos/DroidNet-PSIPro/.claude/worktrees/animation-vm
mkdir -p /tmp/psi-host && c++ -std=gnu++17 -O1 -fsigned-char -fsanitize=address,undefined \
  -I test/host -o /tmp/psi-host/test_native_mocks test/host/test_native_mocks.cpp \
  && /tmp/psi-host/test_native_mocks
```

Expected: `test_native_mocks: OK`. (There is no meaningful red phase for a
known-answer test of new code; if any assert fires, the mock is wrong — fix
the mock, never the vector.)

- [ ] **Step 5: Commit**

```bash
git add test/host/native_mocks/Arduino.h test/host/native_mocks/EEPROM.h test/host/test_native_mocks.cpp
git commit -m "test(host): Arduino/EEPROM mocks with the exact avr-libc PRNG

Park–Miller constants match the shipped ELF disassembly; pgm_read_dword is
deliberately a one-byte read (AVR-observable value, sanitizer-clean).

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

### Task 2: FastLED mock with frame-emit hook

**Files:**
- Create: `test/host/native_mocks/FastLED.h`
- Test: `test/host/test_fastled_mock.cpp`

**Interfaces:**
- Consumes: `native_mocks/Arduino.h` (types, PROGMEM).
- Produces: `CRGB` (u32/rgb ctors, `HTMLColorCode{Black,White,Red=0xFF0000,Green=0x008000,Grey=0x808080}`, `operator=(uint32_t)`, `operator%=`, `operator*=`, `.nscale8_video()`), `CHSV` + `CRGB(const CHSV&)`, `EOrder{RGB,GRB,…}` (the `RGB` enumerator MUST exist — the name-hiding lesson from run.sh:94-97), `template<uint8_t,EOrder> class WS2812`, `CFastLED` with `addLeds<CHIPSET,PIN,ORDER>`, `setBrightness`, `show(uint8_t)`, `show()`, `clear()`, `fill_solid`, and the capture hook `g_frame_hook(const CRGB*, int n, uint8_t scale)`.

- [ ] **Step 1: Write `test/host/native_mocks/FastLED.h`**

```cpp
// Host mock of the FastLED 3.5.0 surface the PSI Pro sketch uses.
// Pixel math matches FastLED 3.5.0 semantics (FASTLED_SCALE8_FIXED build as
// shipped); re-implemented from observed behavior, no FastLED code copied.
#pragma once
#include "Arduino.h"

// ---- 8-bit math ----
inline uint8_t qmul8(uint8_t i, uint8_t j) {
  unsigned p = (unsigned)i * (unsigned)j;
  return (p > 255) ? 255 : (uint8_t)p;
}
// nscale8x3_video semantics: zero stays zero; nonzero gains +1 iff scale != 0.
inline uint8_t nscale8_video_ch(uint8_t c, uint8_t scale) {
  uint8_t nonzeroscale = (scale != 0) ? 1 : 0;
  return (c == 0) ? 0 : (uint8_t)((((int)c * (int)scale) >> 8) + nonzeroscale);
}

struct CHSV {
  uint8_t h, s, v;
  CHSV(uint8_t H = 0, uint8_t S = 0, uint8_t V = 0) : h(H), s(S), v(V) {}
};

struct CRGB {
  uint8_t r, g, b;
  enum HTMLColorCode : uint32_t {
    Black = 0x000000, White = 0xFFFFFF, Red = 0xFF0000,
    Green = 0x008000,  // NOT 0x00FF00 — HTML green.
    Grey  = 0x808080,
  };
  CRGB() : r(0), g(0), b(0) {}
  CRGB(uint8_t R, uint8_t G, uint8_t B) : r(R), g(G), b(B) {}
  CRGB(uint32_t c) : r((c >> 16) & 0xFF), g((c >> 8) & 0xFF), b(c & 0xFF) {}
  CRGB(HTMLColorCode c) : CRGB((uint32_t)c) {}
  CRGB(const CHSV& hsv);  // below
  CRGB& operator=(uint32_t c) { r = (c >> 16) & 0xFF; g = (c >> 8) & 0xFF; b = c & 0xFF; return *this; }
  CRGB& nscale8_video(uint8_t s) {
    r = nscale8_video_ch(r, s); g = nscale8_video_ch(g, s); b = nscale8_video_ch(b, s);
    return *this;
  }
  CRGB& operator%=(uint8_t s) { return nscale8_video(s); }
  CRGB& operator*=(uint8_t d) { r = qmul8(r, d); g = qmul8(g, d); b = qmul8(b, d); return *this; }
};
static_assert(sizeof(CRGB) == 3, "golden format assumes packed 3-byte CRGB");

// Spectrum-style HSV->RGB. NEVER exercised by goldens (the contract layer is
// idle in every capture; only CE_RAINBOW uses CHSV). Present so ContractPSI.h
// compiles. NOT FastLED's hsv2rgb_rainbow — do not capture goldens through it.
inline CRGB::CRGB(const CHSV& hsv) {
  uint8_t region = hsv.h / 43;
  uint8_t rem = (hsv.h - region * 43) * 6;
  uint8_t p = (uint16_t)hsv.v * (255 - hsv.s) >> 8;
  uint8_t q = (uint16_t)hsv.v * (255 - ((uint16_t)hsv.s * rem >> 8)) >> 8;
  uint8_t t = (uint16_t)hsv.v * (255 - ((uint16_t)hsv.s * (255 - rem) >> 8)) >> 8;
  switch (region) {
    case 0: r = hsv.v; g = t; b = p; break;
    case 1: r = q; g = hsv.v; b = p; break;
    case 2: r = p; g = hsv.v; b = t; break;
    case 3: r = p; g = q; b = hsv.v; break;
    case 4: r = t; g = p; b = hsv.v; break;
    default: r = hsv.v; g = p; b = q; break;
  }
}

enum EOrder { RGB = 0012, RBG = 0021, GRB = 0102, GBR = 0120, BRG = 0201, BGR = 0210 };
template <uint8_t DATA_PIN, EOrder RGB_ORDER = RGB> class WS2812 {};

// Frame capture hook — the whole point of the harness. Installed by the
// golden driver; every show() (with or without explicit scale) emits a frame.
inline void (*g_frame_hook)(const CRGB* leds, int n, uint8_t scale) = nullptr;

struct CFastLED {
  CRGB* m_leds = nullptr;
  int m_n = 0;
  uint8_t m_Scale = 255;
  template <template <uint8_t, EOrder> class CHIPSET, uint8_t DATA_PIN, EOrder RGB_ORDER>
  void addLeds(CRGB* l, int n) { m_leds = l; m_n = n; }
  void setBrightness(uint8_t s) { m_Scale = s; }
  void clear() { if (m_leds) memset((void*)m_leds, 0, (size_t)m_n * sizeof(CRGB)); }
  void show(uint8_t scale) { if (g_frame_hook) g_frame_hook(m_leds, m_n, scale); }
  void show() { show(m_Scale); }  // no-arg show latches at the STORED scale (FastLED.h:502 semantics)
};
inline CFastLED FastLED;

inline void fill_solid(CRGB* l, int n, const CRGB& c) { for (int i = 0; i < n; i++) l[i] = c; }
```

- [ ] **Step 2: Write the test `test/host/test_fastled_mock.cpp`**

```cpp
#include "native_mocks/Arduino.h"
#include "native_mocks/FastLED.h"
#include <assert.h>
#include <stdio.h>

static int g_hookCalls = 0;
static uint8_t g_hookScale = 0;
static void hook(const CRGB*, int, uint8_t s) { g_hookCalls++; g_hookScale = s; }

int main() {
  // Color constants.
  CRGB green = CRGB::Green;
  assert(green.r == 0x00 && green.g == 0x80 && green.b == 0x00);
  // u32 assignment.
  CRGB c; c = 0xC8AA00UL;
  assert(c.r == 0xC8 && c.g == 0xAA && c.b == 0x00);
  // nscale8_video KATs: zero->zero; nonzero channel gains +1 when scale != 0.
  CRGB d(200, 0, 1); d %= 100;
  assert(d.r == (uint8_t)(((200 * 100) >> 8) + 1) && d.g == 0 && d.b == 1);
  CRGB z(10, 20, 30); z %= 0;
  assert(z.r == 0 && z.g == 0 && z.b == 0);
  // qmul8 saturates.
  CRGB m(100, 2, 255); m *= 5;
  assert(m.r == 255 && m.g == 10 && m.b == 255);
  // show plumbing: per-call scale, and no-arg show uses stored scale.
  static CRGB buf[4];
  g_frame_hook = hook;
  FastLED.addLeds<WS2812, 4, GRB>(buf, 4);
  FastLED.setBrightness(20);
  FastLED.show(77);
  assert(g_hookCalls == 1 && g_hookScale == 77);
  FastLED.show();
  assert(g_hookCalls == 2 && g_hookScale == 20);
  // clear zeroes but does not latch.
  buf[0] = CRGB(1, 2, 3);
  FastLED.clear();
  assert(buf[0].r == 0 && g_hookCalls == 2);
  puts("test_fastled_mock: OK");
  return 0;
}
```

- [ ] **Step 3: Run, expect `test_fastled_mock: OK`**

```bash
c++ -std=gnu++17 -O1 -fsigned-char -fsanitize=address,undefined \
  -I test/host -o /tmp/psi-host/test_fastled_mock test/host/test_fastled_mock.cpp \
  && /tmp/psi-host/test_fastled_mock
```

- [ ] **Step 4: Commit** (`test(host): FastLED mock with frame-emit hook`, same trailer)

### Task 3: Golden capture driver compiling `src/main.cpp` unmodified

**Files:**
- Create: `test/host/golden_native.cpp`
- Modify: none (production untouched — that is the acceptance)

**Interfaces:**
- Consumes: all Task 1–2 mocks; `src/main.cpp`'s `setup()`, `loop()`, `serialEventRun()`, `JUMP_FRONT_REAR`, EEPROM address constants from `include/config.h:349-352`.
- Produces: `golden_native` binary. CLI: `--cmd "0T12" --ms 20000 --jumper front|rear --pot N --eeprom A,B,C --seed N --out FILE`. Output format: text header line `PSIGOLD1 cmd=… seed=… clock=1ms dur_ms=… jumper=… pot=… eeprom=aa,bb,cc\n`, line `FRAMES\n`, then repeated binary records `[u32 t_ms][u8 scale][48 × (r,g,b)]` (149 B/frame).

- [ ] **Step 1: Write `test/host/golden_native.cpp`**

```cpp
// Golden-frame capture driver. Compiles Neil Hutchison's src/main.cpp
// UNMODIFIED against the host mocks and records every FastLED.show() frame.
// The golden contract: 1 kHz ideal millis, zero-cost show, cold-boot state,
// one process per capture (no cross-mode state leakage).
#include "native_mocks/Arduino.h"
#include "native_mocks/FastLED.h"
#include "native_mocks/EEPROM.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../src/main.cpp"  // the firmware, verbatim

static FILE* g_out = nullptr;
static uint32_t g_frameCount = 0;

static void frameHook(const CRGB* leds_, int n, uint8_t scale) {
  fwrite(&g_mock_millis, sizeof(uint32_t), 1, g_out);
  fputc(scale, g_out);
  fwrite(leds_, sizeof(CRGB), (size_t)n, g_out);
  g_frameCount++;
}

int main(int argc, char** argv) {
  const char* cmd = nullptr;
  const char* outPath = nullptr;
  const char* jumper = "front";
  unsigned long durMs = 10000, seed = 1;
  int pot = 512;
  unsigned ee0 = 0xFF, ee1 = 0xFF, ee2 = 0xFF;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--cmd")) cmd = argv[++i];
    else if (!strcmp(argv[i], "--ms")) durMs = strtoul(argv[++i], 0, 10);
    else if (!strcmp(argv[i], "--jumper")) jumper = argv[++i];
    else if (!strcmp(argv[i], "--pot")) pot = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--seed")) seed = strtoul(argv[++i], 0, 10);
    else if (!strcmp(argv[i], "--eeprom")) sscanf(argv[++i], "%x,%x,%x", &ee0, &ee1, &ee2);
    else if (!strcmp(argv[i], "--out")) outPath = argv[++i];
    else { fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
  }
  if (!cmd || !outPath) { fprintf(stderr, "need --cmd and --out\n"); return 2; }
  if (durMs >= 0x80000000UL) { fprintf(stderr, "duration too long\n"); return 2; }

  g_avr_next = (uint32_t)seed;
  g_mock_analog = pot;
  g_mock_pins[JUMP_FRONT_REAR] = strcmp(jumper, "rear") ? 1 : 0;
  // EEPROM image at the sketch's three config cells (include/config.h:349-352).
  EEPROM.mem[ALWAYS_ON_CONFIG_ADDRESS] = (uint8_t)ee0;
  EEPROM.mem[INTERNAL_BRIGHTNESS_CONFIG_ADDRESS] = (uint8_t)ee1;
  EEPROM.mem[GLOBAL_BRIGHTNESS_CONFIG_ADDRESS] = (uint8_t)ee2;

  g_out = fopen(outPath, "wb");
  if (!g_out) { perror(outPath); return 2; }
  fprintf(g_out, "PSIGOLD1 cmd=%s seed=%lu clock=1ms dur_ms=%lu jumper=%s pot=%d eeprom=%02x,%02x,%02x\nFRAMES\n",
          cmd, seed, durMs, jumper, pot, ee0, ee1, ee2);

  g_frame_hook = frameHook;  // capture setup()-time frames too
  setup();
  char line[32];
  snprintf(line, sizeof line, "%s\r", cmd);
  Serial1.feed(line);
  for (uint32_t t = 1; t <= durMs; t++) {
    g_mock_millis = t;
    loop();
    serialEventRun();  // the Arduino core calls this after every loop()
  }
  fclose(g_out);
  fprintf(stderr, "%s: %u frames\n", outPath, g_frameCount);
  return 0;
}
```

NOTE: the three `*_CONFIG_ADDRESS` names must match `include/config.h:349-352`
exactly — open that file and use its identifiers verbatim (adjust the three
lines above if the names differ; do NOT guess).

- [ ] **Step 2: Build it (slim variant, matching today's shipped config)**

```bash
cd ~/Repos/DroidNet-PSIPro/.claude/worktrees/animation-vm
c++ -std=gnu++17 -O1 -fsigned-char -fsanitize=address,undefined \
  -I test/host/native_mocks -I include \
  -o /tmp/psi-host/golden_slim test/host/golden_native.cpp
```

Expected: compiles clean. Likely first-pass errors and their fixes (fix the
MOCKS, never `src/main.cpp`): missing Arduino API → add to `Arduino.h` mock;
CRGB operator gap → add to `FastLED.h` mock with a KAT in Task 2's test.

- [ ] **Step 3: Prove capture + determinism on mode 2 (flash)**

```bash
/tmp/psi-host/golden_slim --cmd 0T2 --ms 8000 --eeprom 00,01,14 --out /tmp/psi-host/a.psig
/tmp/psi-host/golden_slim --cmd 0T2 --ms 8000 --eeprom 00,01,14 --out /tmp/psi-host/b.psig
cmp /tmp/psi-host/a.psig /tmp/psi-host/b.psig && echo DETERMINISTIC
```

Expected: stderr reports a nonzero frame count on both runs; `DETERMINISTIC`.
Sanity: `xxd /tmp/psi-host/a.psig | head -3` shows the PSIGOLD1 header.

- [ ] **Step 4: Commit** (`test(host): golden-frame capture driver over unmodified main.cpp`)

### Task 4: Config-shadow FULL build (Neil's five dropped modes, original C)

**Files:**
- Create: `test/host/mk_full_config.sh`

**Interfaces:**
- Produces: `/tmp/psi-host/config_full/config.h` (CONTRACT_SLIM line deleted) and the `golden_full` binary — the ground-truth generator for ALL 22 modes.

- [ ] **Step 1: Write `test/host/mk_full_config.sh`**

```bash
#!/usr/bin/env bash
# Build the FULL-variant golden binary: identical source, CONTRACT_SLIM
# removed via a shadowed config.h so Neil's five dropped modes compile in.
# Quoted-include search for "config.h" falls through src/ (no config.h there)
# into the -I chain; the shadow dir comes first.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT="${1:-/tmp/psi-host}"
mkdir -p "$OUT/config_full"
sed '/#define CONTRACT_SLIM/d' include/config.h > "$OUT/config_full/config.h"
c++ -std=gnu++17 -O1 -fsigned-char -fsanitize=address,undefined \
  -I test/host/native_mocks -I "$OUT/config_full" -I include \
  -o "$OUT/golden_full" test/host/golden_native.cpp
echo "built $OUT/golden_full"
```

- [ ] **Step 2: Build and smoke mode 19 (lightsaber, exists only in FULL)**

```bash
chmod +x test/host/mk_full_config.sh && test/host/mk_full_config.sh
/tmp/psi-host/golden_full --cmd 0T19 --ms 12000 --eeprom 00,01,14 --out /tmp/psi-host/saber.psig
```

Expected: a frame count consistent with the 23-step film (~25+ frames
including the entry blackout). Also verify the slim binary treats 0T19 as a
no-op mode (falls through to default) — different frame stream — confirming
the shadow actually flips the fences.

- [ ] **Step 3: Commit** (`test(host): config-shadow FULL golden build (all 22 native modes)`)

### Task 5: Capture matrix, committed goldens, differ, run.sh stage

**Files:**
- Create: `test/host/golden_compare.py`
- Create: `test/host/golden_matrix.txt`
- Create: `test/host/golden/` (committed .psig files)
- Modify: `test/host/run.sh` (new stage between fuzz and cross-compile)

**Interfaces:**
- Produces: committed ground-truth goldens in `test/host/golden/<name>.psig`; `golden_matrix.txt` lines: `<name> <cmd> <ms> <jumper> <eeprom> <pot>`; run.sh regenerates every line with `golden_full` and `cmp`s. Later tasks rely on: **run.sh fails if any regenerated golden differs from the committed one.**

- [ ] **Step 1: Write `test/host/golden_compare.py`**

```python
#!/usr/bin/env python3
"""Byte-compare two .psig files; on mismatch, print the first divergent
frame (index, t_ms, LED, expected vs actual) instead of a bare cmp fail."""
import sys

REC = 4 + 1 + 48 * 3

def load(p):
    with open(p, 'rb') as f:
        data = f.read()
    i = data.index(b'FRAMES\n') + len(b'FRAMES\n')
    return data[:i], data[i:]

def main(a, b):
    ha, fa = load(a)
    hb, fb = load(b)
    if ha != hb:
        print(f'HEADER differs:\n  {ha!r}\n  {hb!r}'); return 1
    if fa == fb:
        return 0
    n = min(len(fa), len(fb))
    for off in range(0, n, REC):
        ra, rb = fa[off:off+REC], fb[off:off+REC]
        if ra != rb:
            idx = off // REC
            t = int.from_bytes(ra[0:4], 'little')
            for j in range(REC):
                if j < len(ra) and j < len(rb) and ra[j] != rb[j]:
                    what = 't_ms' if j < 4 else 'scale' if j == 4 else f'led[{(j-5)//3}].{"rgb"[(j-5)%3]}'
                    print(f'frame {idx} (t={t}ms): {what} expected {ra[j]} got {rb[j]}')
                    return 1
    print(f'frame count differs: {len(fa)//REC} vs {len(fb)//REC}')
    return 1

if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
```

- [ ] **Step 2: Write `test/host/golden_matrix.txt`**

Durations cover each mode's full lifecycle INCLUDING the end-of-pattern
restore to the default swipe. EEPROM cell semantics (verified against
`src/main.cpp:407-429` during Task 3's review — trust these): cell 0
`alwaysOnAddress`: 00→alwaysOn false, 01→true, any other byte→compiled
default (true); cell 1 `externalPOTAddress`: STRICT gate, 00→brightness from
the mocked POT (30-tick warm-up ramp in the scale bytes), 01→fixed internal
brightness from cell 2, any other byte behaves like 00; cell 2: the fixed
brightness value, inert unless cell 1 == 01. The matrix therefore uses
`00,01,14` (alwaysOn off, FIXED brightness 0x14) as the canonical image, and
fresh-chip `ff,ff,ff` (alwaysOn true, POT-ramp brightness — still fully
deterministic under the constant mocked POT) for the always-on rows.

```text
mode00_off        0T0  4000  front 00,01,14 512
mode01_swipe_f    0T1  15000 front 00,01,14 512
mode01_swipe_r    0T1  15000 rear  00,01,14 512
mode02_flash      0T2  8000  front 00,01,14 512
mode03_alarm      0T3  8000  front 00,01,14 512
mode04_shortcirc  0T4  8000  front 00,01,14 512
mode05_scream     0T5  8000  front 00,01,14 512
mode06_leia       0T6  40000 front 00,01,14 512
mode07_iheartu    0T7  12000 front 00,01,14 512
mode08_radar      0T8  10000 front 00,01,14 512
mode09_heart_f    0T9  10000 front 00,01,14 512
mode09_pulse_r    0T9  10000 rear  00,01,14 512
mode10_swtitle    0T10 20000 front 00,01,14 512
mode11_march      0T11 52000 front 00,01,14 512
mode12_disco4     0T12 8000  front 00,01,14 512
mode13_discoinf   0T13 8000  front 00,01,14 512
mode14_rebel      0T14 9000  front 00,01,14 512
mode15_knight     0T15 12000 front 00,01,14 512
mode16_white      0T16 4000  front 00,01,14 512
mode17_red        0T17 4000  front 00,01,14 512
mode18_green      0T18 4000  front 00,01,14 512
mode19_saber      0T19 12000 front 00,01,14 512
mode20_swintro    0T20 25000 front 00,01,14 512
mode21_vu4        0T21 8000  front 00,01,14 512
mode92_vuinf      0T92 8000  front 00,01,14 512
mode02_alwayson   0T2  8000  front ff,ff,ff 512
mode19_alwayson   0T19 12000 front ff,ff,ff 512
```

- [ ] **Step 3: Capture and commit the goldens**

```bash
mkdir -p test/host/golden
test/host/mk_full_config.sh
while read -r name cmd ms jumper ee pot; do
  [ -z "$name" ] && continue
  /tmp/psi-host/golden_full --cmd "$cmd" --ms "$ms" --jumper "$jumper" \
    --eeprom "$ee" --pot "$pot" --out "test/host/golden/$name.psig"
done < test/host/golden_matrix.txt
ls -la test/host/golden | head
du -sh test/host/golden
```

Expected: 27 files, every one with a nonzero frame count on stderr. Inspect
sizes — the random-heavy modes (1, 4, 12, 13, 21) should be the largest.
Sanity-check `mode09_heart_f` differs from `mode09_pulse_r`, and
`mode19_alwayson` holds its last frame (no restore) while `mode19_saber`
ends with swipe frames (the restore).

- [ ] **Step 4: Add the run.sh stage**

In `test/host/run.sh`, after the fuzz stage (ends ~line 86) and before the
cross-compile stage, insert (match the file's existing echo/banner style —
read the neighboring stages first and renumber the stage banners honestly):

```bash
echo "[5/6] golden-frame parity: mocks + Neil's animation code must reproduce"
echo "      the committed goldens bit-for-bit (test/host/golden/)."
GOLD="$BUILD/golden"
mkdir -p "$GOLD"
test/host/mk_full_config.sh "$BUILD"
while read -r name cmd ms jumper ee pot; do
  [ -z "$name" ] && continue
  "$BUILD/golden_full" --cmd "$cmd" --ms "$ms" --jumper "$jumper" \
    --eeprom "$ee" --pot "$pot" --out "$GOLD/$name.psig" 2>/dev/null
  if ! cmp -s "$GOLD/$name.psig" "test/host/golden/$name.psig"; then
    echo "GOLDEN MISMATCH: $name"
    python3 test/host/golden_compare.py "test/host/golden/$name.psig" "$GOLD/$name.psig" || true
    exit 1
  fi
done < test/host/golden_matrix.txt
echo "      $(wc -l < test/host/golden_matrix.txt | tr -d ' ') captures identical."
```

(Adapt `$BUILD` to the variable run.sh already uses for its scratch dir, and
make `mk_full_config.sh` accept that dir — it already takes `$1`.)

- [ ] **Step 5: Run the full suite, expect green end-to-end**

```bash
test/host/run.sh
```

Expected: all pre-existing stages pass unchanged; new stage reports 27
captures identical. Run it TWICE to prove regeneration stability.

- [ ] **Step 6: Commit** (`test(host): commit golden frames for all 22 native modes + parity stage`)

**CHECKPOINT: Phase 1 done — production source untouched (`git diff cd27e88 -- src include platformio.ini` must be empty except docs/). The ground truth is locked. Everything after this is judged against these goldens.**

---

## Phase 2 — VM core

### Task 6: `include/psi_vm.h` core + decode-walk test (no dispatch changes yet)

**Files:**
- Create: `include/psi_vm.h`
- Modify: `src/main.cpp` (ONE line: the include, placed after the global state block at `src/main.cpp:~355`, before the ContractPSI.h include — exactly where the probe put it)
- Test: `test/host/test_psi_vm.cpp`

**Interfaces:**
- Consumes: main.cpp globals (`firstTime`, `patternRunning`, `globalPatternLoops`, `timingReceived`, `commandTiming`, `alwaysOn`, `lastPSIeventCode`, `defaultPattern`, `leds`, `ledMatrix`), helpers (`set_delay`, `checkDelay`, `set_global_timeout`, `loopsDonedoRestoreDefault`, `globalTimerDonedoRestoreDefault`, `brightness`, `fill_row`, `fill_column`, `fill_half_column`, `displayMatrixColor`, `primary_color`, `secondary_color`, `secondary_off_color`), FastLED.
- Produces (used by every conversion task): opcodes `OP_END/OP_SHOW/OP_SHOWR/OP_SHOWNOW/OP_CLEAR/OP_FILL_ALL/OP_FILL_ROW/OP_FILL_COLR/OP_HALFCOLR/OP_PIX/OP_FRAME/OP_SPARKLE/OP_SCALE_RAND/OP_MUL_RAND/OP_LOOPSTART`; macros `VB16/V_SHOW/V_SHOWR/V_FROW/V_FCOLS/V_HCOLS/V_PIX/V_FRAME/V_SPARK`; color ids `VC_*`; `struct VmProg{const uint8_t* code; uint8_t loops, runtime, flags;}`; flags `VMF_CLEAR/VMF_ONESHOT/VMF_IGNORE_TIMING`; `void vmPlay(uint8_t progId)`; tables `vmPalette`, `vmProgs[]`, `vmBitmaps[]`, `vmFramePals[]`.

- [ ] **Step 1: Write `include/psi_vm.h`**

Start from the probe (`~/Repos/DroidNet-PSIPro-probes/vm-extended/include/psi_vm.h`,
591 lines — read it in full first) with these REQUIRED changes:

1. New header comment: production framing (no "SIZING PROBE"), MIT/fork notice
   matching the repo's existing style (see the top of `src/contract/ContractPSI.h`).
2. **Portability**: every pointer read from PROGMEM tables uses `pgm_read_ptr`
   — the probe's `(const uint8_t*)pgm_read_word(&pd->code)` and
   `(const byte*)pgm_read_word(&vmBitmaps[b])` truncate 64-bit host pointers.
   On AVR, `pgm_read_ptr` is provided by avr-libc; the host mock already
   defines it (Task 1).
3. Replace `FALL_THROUGH()` with `__attribute__((fallthrough));`.
4. **New opcode `OP_SHOWNOW`** (emit a frame and CONTINUE executing, no
   set_delay, no return): several native modes latch a black/setup frame and
   the real frame in the SAME tick (scanRow's embedded `allOFF(true)`,
   red_heart's displayMe double-show). Implementation inside `vmStep()`:

```cpp
      case OP_SHOWNOW:
        FastLED.show(brightness());
        break;
```

   (`OP_SHOWNOW` between `OP_SHOWRND` and `OP_CLEAR` in the enum — append new
   ops at the END of the existing enum order so program bytes stay readable
   in diffs.)
5. Ship the core with ONLY the two flash programs (`vmc_flash60`,
   `vmc_flash125`) and descriptors `VMP_FLASH`, `VMP_ALARM` in `vmProgs[]`.
   Programs/ops for other modes arrive with their conversion tasks. Include
   `vmColor` role support and the palette table as in the probe, but trim
   `vmBitmaps`/`vmFramePals`/`BM_*`/`FP_*` — they arrive in Task 10
   (introducing them now would revive PROGMEM bitmaps before any native code
   is deleted and squeeze the I2C env).
6. Keep the probe's `vmStep`/`vmPlay` control flow (frame-cursor + rep-count +
   loop-pointer + descriptor lifecycle) — it is the measured shape — but you
   own every line: re-derive, don't paste blindly.
7. Do NOT include the contract shims (`vmContractScan`/`vmContractSparkle`)
   yet — Task 14.

- [ ] **Step 2: Wire the include (only change to main.cpp in this task)**

```cpp
// Animation VM: PROGMEM bytecode + the one interpreter that replaces the
// hand-written mode bodies. Included after the global state block (needs
// firstTime/patternRunning/leds/ledMatrix/...) and before ContractPSI.h.
#include "psi_vm.h"
```

- [ ] **Step 3: Write the decode-walk test `test/host/test_psi_vm.cpp`**

```cpp
// Structural check of every VM program: ops are known, arities consume
// in-bounds bytes, every program reaches OP_END within its array, and every
// frame has a SHOW-class terminator. Compiles psi_vm.h in tables-only mode —
// no firmware globals needed.
#define PSI_VM_TABLES_ONLY 1
#include "native_mocks/Arduino.h"
#include "native_mocks/FastLED.h"
#include "../../include/psi_vm.h"
#include <assert.h>
#include <stdio.h>

struct ProgRef { const char* name; const uint8_t* code; size_t len; };
#define P(x) {#x, x, sizeof(x)}
static const ProgRef progs[] = {
    P(vmc_flash60), P(vmc_flash125),
    // conversion tasks append their programs here
};

static int arity(uint8_t op) {
  switch (op) {
    case OP_END: case OP_CLEAR: case OP_LOOPSTART: case OP_SHOWNOW: return 0;
    case OP_FILL_ALL: case OP_MUL_RAND: return 1;
    case OP_SHOW: case OP_PIX: case OP_FRAME: case OP_SPARKLE: case OP_SCALE_RAND: return 2;
    case OP_SHOWR: case OP_FILL_ROW: case OP_FILL_COLR: return 3;
    case OP_SHOWRND: case OP_HALFCOLR: return 4;
    default: return -1;
  }
}

int main() {
  for (const auto& pr : progs) {
    size_t i = 0;
    bool ended = false;
    while (i < pr.len) {
      uint8_t op = pr.code[i++];
      int a = arity(op);
      if (a < 0) { printf("%s: unknown op %u at %zu\n", pr.name, op, i - 1); return 1; }
      i += (size_t)a;
      if (op == OP_END) { ended = true; break; }
    }
    assert(ended && i <= pr.len && "program must OP_END in bounds");
  }
  printf("test_psi_vm: %zu programs OK\n", sizeof(progs) / sizeof(progs[0]));
  return 0;
}
```

In `psi_vm.h`, fence the interpreter/state/shims (everything that touches
firmware globals) behind `#ifndef PSI_VM_TABLES_ONLY … #endif`; opcodes,
macros, palette, program arrays, and descriptors stay outside the fence.

- [ ] **Step 4: Run everything**

```bash
c++ -std=gnu++17 -O1 -fsigned-char -fsanitize=address,undefined \
  -I test/host -I test/host/native_mocks -o /tmp/psi-host/test_psi_vm test/host/test_psi_vm.cpp \
  && /tmp/psi-host/test_psi_vm
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run && pio run -e PSIPro-i2c
test/host/run.sh
```

Expected: decode test OK; both envs still fit (the unreferenced VM costs a
few hundred bytes at most — `vmPlay` is not yet called; record the exact
sizes in the commit message); run.sh green including golden parity (dispatch
untouched → goldens unchanged). Add the decode test to run.sh's host-test
stage alongside the existing test binaries.

- [ ] **Step 5: Commit** (`feat(psi): animation VM core (interpreter + flash programs), not yet dispatched`)

---

## Phase 3 — conversions, golden-gated (each task: encode → wire → golden cmp → delete native → sizes → commit)

**The conversion protocol, used verbatim by every task in this phase:**

1. Wire the mode's `runPattern` case(s) to `vmPlay(VMP_*)` (delete the native call).
2. Rebuild the FULL golden binary and regenerate ONLY this mode's matrix lines;
   `cmp` against the committed goldens. Iterate on the PROGRAM (op stream,
   descriptor, rep counts) until identical — `golden_compare.py` names the
   first divergent frame/LED. Never edit the golden, never edit the harness to
   make a conversion pass (harness changes require re-proving ALL goldens).
3. Delete the native bodies this task obsoletes (only when this task converts
   their LAST caller — noted per task). Deletion means deletion, not an #ifdef.
4. `test/host/run.sh` (full suite, all goldens) — green.
5. `pio run && pio run -e PSIPro-i2c` — both fit; record sizes in the commit.

### Task 7: Modes 2/3/5 — flash

**Files:** Modify `include/psi_vm.h` (programs exist since Task 6), `src/main.cpp:2171-2185` (dispatch) and `src/main.cpp:1141-1191` (delete `flash()`); append `P(...)` refs already present in `test/host/test_psi_vm.cpp`.

**Interfaces:** Consumes `vmPlay`, `VMP_FLASH`, `VMP_ALARM`.

- [ ] Step 1: dispatch — replace the three cases with (keep the comment style the probe used, citing the original call):

```cpp
    case 2:              //  2 = Flash Panel (4s) — was flash(0xffffff, 60, 24, 4)
      vmPlay(VMP_FLASH);
      break;
    case 3:              //  3 = Alarm (4s) — was flash(0xffffff, 125, 15, 4)
      vmPlay(VMP_ALARM);
      break;
    case 5:              //  5 = Scream — same as Alarm
      vmPlay(VMP_ALARM);
      break;
```

- [ ] Step 2: regenerate + cmp `mode02_flash`, `mode03_alarm`, `mode05_scream`, `mode02_alwayson`. Expect: iterate on the program until identical (watch the first frame: native `flash()` had no entry blackout — `VMF_CLEAR` must be OFF, as the Task 6 descriptors already say; watch the final restore frames).
- [ ] Step 3: delete `flash()` (`src/main.cpp:1141-1191`).
- [ ] Step 4: run.sh green; both envs build; sizes recorded (expect roughly −220 B net on the serial env vs Task 6).
- [ ] Step 5: commit `feat(psi): modes 2/3/5 are bytecode; native flash() deleted (golden-exact)`.

### Task 8: Modes 6/10/15 — the sweeps (Leia, SW-title, Knight Rider)

**Files:** Modify `include/psi_vm.h` (add `vmc_leia`, `vmc_swscan`, `vmc_knight` + `VMP_LEIA/VMP_SWSCAN/VMP_KNIGHT`; ops `OP_FILL_ROW`/`OP_FILL_COLR` land here if Task 6 shipped them unused — they're in the core enum already), `src/main.cpp` dispatch cases 6/10/15; delete `scanRow`, `scanRowDownUp`, `scanColLeftRight`, `Cylon_Row`, `Cylon_Col` (defs at `src/main.cpp:572, 650, 859, 1193, 1236`). **Do NOT delete `scanCol` (main.cpp:754) — the contract's CE_SCAN still calls it until Task 14.**

Program seeds (from the probe — expect golden-driven correction for the
native blank frames: `scanRow` latches black via its embedded `allOFF(true)`
then the row in the same tick, so each step likely becomes
`OP_CLEAR, OP_SHOWNOW, V_FROW(...), V_SHOW(d)`; the golden decides):

```cpp
// Mode 6: Leia — Cylon_Row(0xcccccc, 74, type 3 = scan down), 57 loops, 34 s.
const uint8_t vmc_leia[] PROGMEM = {
  OP_CLEAR, OP_SHOWNOW, V_FROW(0, VC_GREYCC, 0), V_SHOW(74),
  OP_CLEAR, OP_SHOWNOW, V_FROW(1, VC_GREYCC, 0), V_SHOW(74),
  OP_CLEAR, OP_SHOWNOW, V_FROW(2, VC_GREYCC, 0), V_SHOW(74),
  OP_CLEAR, OP_SHOWNOW, V_FROW(3, VC_GREYCC, 0), V_SHOW(74),
  OP_CLEAR, OP_SHOWNOW, V_FROW(4, VC_GREYCC, 0), V_SHOW(74),
  OP_CLEAR, OP_SHOWNOW, V_FROW(5, VC_GREYCC, 0), V_SHOW(74),
  OP_END,
};
// Mode 10: gold row scan up, 500 ms, 5 loops — same shape, rows 5..0, VC_GOLD.
// Mode 15: Knight Rider — Cylon_Col type 1 bounce: columns 0..9 then 8..1,
// V_FCOLS(col,1,VC_RED), V_SHOW(250) per step (18 steps; see the probe's
// vmc_knight for the full listing).
```

Descriptors: `{vmc_leia, 57, 34, 0}`, `{vmc_swscan, 5, 0, 0}`, `{vmc_knight, 5, 0, 0}`.

- [ ] Steps: protocol 1–5 for goldens `mode06_leia`, `mode10_swtitle`, `mode15_knight`. Commit `feat(psi): sweep modes 6/10/15 are bytecode; scanRow/scanColLeftRight/Cylon_* deleted`.

### Task 9: Mode 8 — radar

**Files:** Modify `include/psi_vm.h` (`vmc_radar` + `VMP_RADAR`, op `OP_HALFCOLR`), `src/main.cpp` case 8; delete `radar()` (`src/main.cpp:1453-1541`). Keep `fill_half_column` (the op calls it).

Program seed: the probe's `vmc_radar` (4 quadrant steps × `V_HCOLS(start,5,half,VC_RED)`, 250 ms, 6 loops, `VMF_CLEAR`) — golden-correct the entry blackout and any inter-step blank frames.

- [ ] Steps: protocol 1–5 for `mode08_radar`. Commit.

### Task 10: Modes 14 + 9-rear — first bitmap modes (rebel, Pulse)

**Files:** Modify `include/psi_vm.h`: introduce `vmBitmaps`/`vmFramePals` with ONLY `{rebel, pulse}` and `FP_HEART`(rebel red/greybg)/`FP_PULSE` palettes, `BM_REBEL`/`BM_PULSE`; add `OP_FRAME`/`OP_PIX` cases; add `vmc_rebel`, `vmc_pulse` + descriptors. Modify `src/main.cpp`: case 14 and the case-9 rear branch; delete `Pulse()` (`src/main.cpp:1922-1978`). Also move `displayMatrixColor`'s default arguments to a forward declaration in `psi_vm.h` exactly as the probe did (defaults may live on only ONE declaration; the definition at `src/main.cpp:1011` loses them — copy the probe's hunk).

Program seeds: probe's `vmc_rebel` (`V_FRAME(BM_REBEL, FP_HEART), V_SHOW(5000)`, loops 1, runtime 5) and `vmc_pulse` (13 pulse frames, `V_PIX` red trace, 100 ms — full listing in the probe). Golden watch-outs: native mode 14 calls `displayMatrixColor(..., displayMe=true, 5)` which shows ONCE and runs its own runtime path — the golden shows the exact show cadence; `OP_SHOWNOW` may be needed instead of `V_SHOW(5000)`.

- [ ] Steps: protocol 1–5 for `mode14_rebel`, `mode09_pulse_r`. Commit.

### Task 11: Modes 12/13 — DiscoBall (random)

**Files:** Modify `include/psi_vm.h` (`OP_SPARKLE` + `vmSparkleDraw` + `vmc_disco` + `VMP_DISCO4/VMP_DISCOINF`), `src/main.cpp` cases 12/13. **Do NOT delete `DiscoBall()` — CE_SPARKLE still calls it until Task 14.**

This is the first random-consuming conversion: the golden passes ONLY if the
VM issues `random()` calls in exactly the native order (native DiscoBall:
`random(0, LEDS_PER_COLUMN)` then `random(0, COLUMNS)` per sparkle — check
`src/main.cpp:1820-1821` and match `vmSparkleDraw`'s draw order to it).

- [ ] Steps: protocol 1–5 for `mode12_disco4`, `mode13_discoinf`. Commit.

### Task 12: Mode 4 — FadeOut (random per-LED + the loop-tail quirk)

**Files:** Modify `include/psi_vm.h` (`OP_SCALE_RAND`, `OP_MUL_RAND`, `OP_SHOWR`, `vmc_fadeout`, `VMP_FADEOUT` with `VMF_IGNORE_TIMING`), `src/main.cpp` case 4; delete `FadeOut()` (`src/main.cpp:1633-1702`).

**Known quirk (measured in the probe):** the native loop accounting runs 12
extra dim passes at loops=3 (`totalLoopCount` keyed to magic totals,
`src/main.cpp:1687-1690`). The probe encoded a clean (4+8)×3 and would FAIL
the golden. Read the native counting, then mirror it in the program — e.g.
adjust the `V_SHOWR` rep counts or add a trailing dim-pass block until
`golden_compare.py` reports identical. Byte-for-byte includes bugs.

- [ ] Steps: protocol 1–5 for `mode04_shortcirc`. Commit message must name the mirrored quirk.

### Task 13: The dropped five — 7, 9-front, 11, then 19, 20

**Files:** Modify `include/psi_vm.h`: extend `vmBitmaps` with `LetterI, Heart, LetterU` (+`BM_I/BM_HEART/BM_U`) for part 1, then `lightsaber0..21` (+`BM_LS0..BM_LS21`, `FP_SABER`/`FP_CLASH` palettes) for part 2; add `OP_LOOPSTART` use + `VMF_ONESHOT` handling (already in core); programs `vmc_iheartu`, `vmc_redheart`, `vmc_march`, `vmc_saber`, `vmc_swintro` + descriptors (probe listings are the seeds — march via `V_FCOLS`, SWIntro's 9-case crawl with `V_FROW` scales `{0,100,20,12}` and `V_PIX` knockouts). Modify `src/main.cpp`: replace the four `#ifndef CONTRACT_SLIM` fenced dispatch blocks (`2189-2193, 2200-2204, 2213-2217, 2241-2248`) with unconditional `vmPlay` cases (case 9 becomes `vmPlay(digitalRead(JUMP_FRONT_REAR) ? VMP_REDHEART : VMP_PULSE);`); delete `i_heart_u`, `red_heart`, `march`, `lightsaberBattle`, `StarWarsIntro` bodies (`src/main.cpp:1277, 1332, 1386, 1858, 1980`).

Golden watch-outs: `mode09_heart_f`'s native double-show per step
(displayMe=true then updateLed) → `V_FRAME + OP_SHOWNOW + V_SHOW(500)`
shape; saber's state-99 hold (`VMF_ONESHOT`: `mode19_alwayson` must hold the
final frame forever, `mode19_saber` must restore to swipe); SWIntro's
prologue-then-`OP_LOOPSTART` structure and its 10 s runtime cap.

Do this as two commits (7/9f/11, then 19/20) so the I2C env size step is
visible: the saber bitmaps (+1,056 B) land only after ~3 KB of native code
is already gone.

- [ ] Steps: protocol 1–5 for `mode07_iheartu`, `mode09_heart_f`, `mode11_march`; commit. Then `mode19_saber`, `mode19_alwayson`, `mode20_swintro`; commit. **These goldens passing = the five dropped modes are restored byte-for-byte. Both envs must fit (probe reference: 26,274 / 27,716 B).**

### Task 14: Contract shims — CE_SCAN / CE_SPARKLE, last natives deleted

**Files:** Modify `include/psi_vm.h` (add `vmContractScan`, `vmContractSparkle` — probe listings, they mirror `scanCol(d,0,col,true)` and `DiscoBall(d,0,3,col,0)` per-tick semantics), `src/contract/ContractPSI.h:306,315` (two lines, exactly the probe's hunk: `case CE_SCAN: vmContractScan(d, col); break;` / `case CE_SPARKLE: vmContractSparkle(d, col); break;`), `src/main.cpp` — delete `scanCol` (`:754`) and `DiscoBall` (`:1792`). Modify `test/host/compile_contract.cpp` — the latch guard that asserts CE_SCAN/CE_SPARKLE latch via the native stubs: repoint its expectations at the shims honestly (the guard's INTENT — every effect latches — is unchanged; only the plumbing named in it moves). `contract_core.h` untouched.

- [ ] Steps: protocol steps 2–5 (no runPattern change) — the contract has no goldens; its gate is the existing host behavioral guards (C1 latch list etc.) staying green with the renegotiated stubs, plus ALL mode goldens still green (the shims share helpers with the VM), plus both env sizes. Commit `feat(psi): CE_SCAN/CE_SPARKLE render via VM shims; scanCol/DiscoBall deleted` with the guard renegotiation named in the body.

---

## Phase 4 — CONTRACT_SLIM removal, docs, acceptance

### Task 15: Kill CONTRACT_SLIM + refresh every stale size claim

**Files:** Modify `include/config.h` (delete the `#define CONTRACT_SLIM` and rewrite its 40-line WHAT-IT-COSTS block, `:42-79`, as a short epitaph pointing at the VM: the five modes are back as data), `src/main.cpp` (grep `CONTRACT_SLIM` — all remaining references must go), `test/host/run.sh` (stage-4 banner sizes `:113` and any echoed 25,754/27,238 claims → new measured numbers), `platformio.ini` (the size table comment in the I2C env block), `README.md` + `DROIDNET_FORK.md` (the dropped-modes divergence note becomes: all upstream modes present; animations are PROGMEM bytecode; new-animation cost ~tens of bytes).

- [ ] Step 1: `grep -rn CONTRACT_SLIM include src test *.md platformio.ini` — fix every hit (the harness's `mk_full_config.sh` sed becomes a no-op; simplify it to a plain build and rename is NOT needed — keep the script, note the sed now matches nothing).
- [ ] Step 2: full gate: `test/host/run.sh` green; `pio run && pio run -e PSIPro-i2c` — record final sizes; verify against the spec envelope (serial ≤ ~26.4 KB, I2C ≤ ~27.8 KB).
- [ ] Step 3: re-run the stack bound: `python3 test/host/stack_report.py` (per run.sh's existing invocation) — the serial-only bound must still be reported SOUND; record the new worst-case number in the commit.
- [ ] Step 4: commit `feat(psi)!: CONTRACT_SLIM is dead — all five modes restored as VM data`.

### Task 16: Prove the marginal cost — one new animation as pure data

**Files:** Modify `include/psi_vm.h` (one new program + descriptor), `src/main.cpp` (one dispatch case — use mode 22, currently unused), `test/host/golden_matrix.txt` + `test/host/golden/` (capture ITS golden from the VM build — for a new animation the VM IS the reference; the golden locks it against regression), `test/host/test_psi_vm.cpp` (add its `P(...)` ref).

Suggested demo (droid-appropriate, exercises no new ops): "processing sweep" —
a gold column wipe left-to-right then a white full-panel blink:

```cpp
// Mode 22 (fork addition): processing sweep — the demo that new animations
// are data. Cost = this array + 5 descriptor bytes.
const uint8_t vmc_process[] PROGMEM = {
  OP_CLEAR, V_FCOLS(0, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(1, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(2, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(3, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(4, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(5, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(6, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(7, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(8, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(9, 1, VC_GOLD), V_SHOW(120),
  OP_FILL_ALL, VC_W, V_SHOW(150), OP_CLEAR, V_SHOW(150),
  OP_END,
};
```

- [ ] Steps: add + wire + capture its golden + run.sh + both envs. **Measure and record the marginal cost in the commit message: flash delta of this commit = the price of one new animation.** (Expect ≈ 50–60 B.) Commit `feat(psi): mode 22 processing sweep — a new animation is ~50 B of data`.

### Task 17: Final acceptance + review gate

- [ ] Step 1: the full matrix, one more time from clean: `test/host/run.sh` twice (regeneration stability), `pio run && pio run -e PSIPro-i2c`.
- [ ] Step 2: verify the acceptance list from the spec §6 item by item; `git diff cd27e88 --stat -- src/contract/contract_core.h` empty; `git -C ~/Repos/DroidNet-PSIPro status --porcelain` clean (main checkout untouched).
- [ ] Step 3: use superpowers:requesting-code-review on the branch diff (`cd27e88..HEAD`), fix findings, re-gate.
- [ ] Step 4: STOP. Present final sizes, golden inventory, and guard renegotiations to Travis. Merging `feature/animation-vm`, pushing, flashing hardware, the CDC/FastLED levers, Studio hookup, and the upstream offer to Neil are ALL separate decisions that need his word.

---

## Self-review notes (already applied)

- Spec coverage: harness (§3→Tasks 1–5), VM (§4→Tasks 6–13), shims + guard renegotiation (§4/§5→Task 14), CONTRACT_SLIM death + docs + stack re-baseline (§5→Task 15), demo animation (§6.4→Task 16), acceptance (§6→Task 17). Deferred levers (§2) intentionally absent.
- The probe's three disclosed fidelity gaps are owned by named tasks: FadeOut loop tail (Task 12), sweep/radar blank frames (Tasks 8–9, via OP_SHOWNOW), red_heart/rebel double-show (Tasks 10, 13).
- Type consistency: op names, `VmProg` fields, `VMP_*`/`VC_*`/`BM_*`/`FP_*` ids and `vmPlay(uint8_t)` are defined in Task 6 and used identically in Tasks 7–16; `pgm_read_ptr` is defined in Task 1 (host) and by avr-libc (target).
- Known unknowns called out in-task rather than guessed: EEPROM cell identifiers (Task 3 note), EEPROM byte sense for alwaysOn (Task 5 note), exact native show cadences (per-task golden watch-outs).
