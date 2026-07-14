// Part of the DroidNet Driveable-Animation Contract — an additive layer bolted onto
// Neil Hutchison's PSI Pro firmware. Copyright (c) 2026 Travis Cook, MIT-licensed
// (see LICENSE-DroidNet-Contract). The firmware this layer drives — every render
// primitive it calls — is the work of Neil Hutchison and contributors and is NOT
// covered by that license; see the NOTICE in README.md.
//
// src/contract/ContractPSI.h — PSI Pro (MaxPSI) firmware layer for the
// Driveable-Animation Contract. ADDITIVE: the native JawaLite T/A/D/P grammar and
// the I2C intake are untouched; this header only adds the '!'-prefixed contract
// family. ZERO edits to the render primitives — every effect reuses an existing
// CRGB-parametric primitive with a live g_contractColor.
//
// INCLUDE ORDER (see main.cpp ~line 351): this header is included AFTER config.h /
// matrices.h / FastLED and AFTER the global state block (leds[], level[],
// useTempInternalBrightness/tempGlobalBrightnessValue, firstTime, lastPSIeventCode,
// serialPort) is declared, and AFTER functions.h has prototyped the render
// primitives it calls (allON/allOFF/flash/scanCol/Cylon_Col/DiscoBall/VUMeter/
// runPattern/brightness). It is included BEFORE loop()/serialEvent()/receiveEvent()
// so those can call the contract entry points.
//
// DESIGN (verified against src/main.cpp + include/config.h on 2026-07-12):
//   * Color is TRACTABLE: allON(CRGB,...) main.cpp:461, flash(CRGB,...) main.cpp:1114,
//     scanCol(...,CRGB,...) main.cpp:727, Cylon_Col(CRGB,...) main.cpp:1209,
//     DiscoBall(...,CRGB,...) main.cpp:1759, VUMeter(...) main.cpp:1677,
//     fill_column/fill_row main.cpp:501/531, displayMatrixColor main.cpp:984 — all
//     already take a live CRGB. runContractAnim() is a parallel dispatcher (vs
//     runPattern main.cpp:2116) driven by g_effect/g_contractColor.
//   * Brightness rides the VOLATILE 3P path only (useTempInternalBrightness +
//     tempGlobalBrightnessValue, main.cpp:2562-2585 / brightness() main.cpp:2595) —
//     NEVER the 2P EEPROM path (main.cpp:2557) — so B/L/envelope never wear EEPROM.
//   * Verb P is a dedicated millis overlay that ALWAYS retriggers, bypassing the
//     runPattern no-op guard (main.cpp:2121-2129). v1.2: that overlay is now the ONE
//     accent primitive — it renders an EFFECT (not just a solid fill), and it is armed
//     from two triggers: verb P (live/Phase-1) and the board's own per-beat score edge
//     (autonomous/Phase-2, from a scored A's ae=/ac=/ad=). Same code path, same look.
//   * Verb L feeds level[10] (main.cpp:327) honoring the VU inversion
//     (main.cpp:1714: level[c] > i => pixel OFF, so high energy => LOW level).
//   * Addressing = JUMP_FRONT_REAR jumper (config.h:101-105, main.cpp:358):
//     HIGH=front (!P F), LOW=rear (!P R).
#pragma once
// NO <stdio.h>. The verb-Q ack (CV_QUERY) was the one and only stdio call site in this
// firmware, and that single snprintf() linked in avr-libc's vfprintf (918 B) +
// __ultoa_invert (188 B) + fputc (96 B) + snprintf (90 B) + strnlen/strnlen_P (44 B) —
// ~1.3 KB of a 28 KB image — to emit one status line. The stock upstream firmware links
// none of it. The ack is now built by the _fmtStr/_fmtU16/_fmtHex8 helpers below, whose
// output is byte-identical (pinned by host guard A10). Do not re-add this include.
#include "contract_core.h"

// ---- forward declaration: fill_column ---------------------------------------
// Every OTHER board primitive this layer calls (allON/allOFF/scanCol/DiscoBall/VUMeter/
// runPattern/brightness) is declared in include/functions.h, which main.cpp pulls in via
// preamble.h at line 286 — well above our include point at main.cpp:357. fill_column() is
// the one exception: it is DEFINED at main.cpp:517 but declared in no header at all, so at
// our include point the name is not yet in scope and the real avr-gcc build fails with
// "'fill_column' was not declared in this scope". The host mock DEFINED it, which is
// precisely why the mock type-check could never see this.
// Declared WITHOUT the default argument (main.cpp:517 already supplies `= 0`; repeating it
// here would be an illegal redefinition of a default argument). Our call sites pass all
// three arguments explicitly.
// Upstream's own signature, verbatim — do not "improve" it:
void fill_column(uint8_t column, CRGB color, uint8_t scale_brightness);

// ---- PSI_NOINLINE: a FLASH-SIZE knob, not a behaviour knob -------------------
// Zero semantic effect — it only tells GCC where to EMIT a function, never what it
// computes. On the ATmega32U4 the 28 KB flash budget is the binding constraint, and the
// leaf helpers below are what blew it.
//
// WHY (measured, not guessed): every helper here is marked `inline`. To GCC that is not a
// hint but nearly a directive — a function DECLARED inline is exempt from the size
// heuristics that hold ordinary functions back, so neither -Os nor even
// -fno-inline-small-functions will stop it being copied into every call site. (That flag,
// by definition, only applies to functions NOT declared inline — which is exactly why it
// did not catch these.) _scaled()/_clampBright() have ~15 call sites between them inside
// applyContract(), which itself inlines into parseContract(), so each tiny body was being
// stamped out fifteen times — and a CRGB construction or a 3-way clamp is not tiny once it
// is in AVR registers. Forcing these four leaves out of line emits ONE copy and calls it:
// -1,360 B on its own. parseContract() went 4,294 -> 2,010 B.
//
// The opposite hypothesis — that the BIG parse/apply functions were being duplicated — was
// tested and is FALSE: they are each called exactly once, and outlining them
// (-fno-inline-functions-called-once) COSTS 1,364 B.
//
// Portability: contract_core.h stays untouched (it must remain byte-identical across the
// three fork repos) and the attribute is confined to __AVR__, so host clang and the ESP32
// builds see an empty macro and are bit-for-bit unaffected. Do not "clean this up" back to
// a plain `inline` — that is the regression this exists to prevent.
#if defined(__AVR__)
  #define PSI_NOINLINE __attribute__((noinline))
#else
  #define PSI_NOINLINE
#endif

// ---- safety caps (fork spec §11) --------------------------------------------
static const uint8_t  SAFE_MAX_BRIGHTNESS = 200;   // USB-power/heat clamp (config: cap 200)
static const uint32_t STROBE_MIN_STATE_MS = 170;   // >=170 ms/state => < ~3 Hz (photosensitive-safe)

// ---- contract animation state -----------------------------------------------
static ContractEffect g_effect        = CE_SOLID;
static ContractEffect g_lastEffect    = CE_NONE;   // forces firstTime on an effect switch
static CRGB           g_contractColor = CRGB(0, 0, 255);
static uint8_t        g_speed         = 128;
static uint8_t        g_bright        = SAFE_MAX_BRIGHTNESS;
static uint8_t        g_beatMod       = 0;
static int            g_nativeCode    = 1;          // native:<n> escape hatch target
static uint32_t       g_animDeadline  = 0;          // 0 == hold; else millis() when the look expires
static uint32_t       g_effectStartMs = 0;
static bool           g_contractArmed = false;      // gates runContractAnim() vs runPattern()

// The accent overlay — own deadline, always retriggers. Armed by verb P (live) and by
// the board's own beat edge (scored, v1.2). g_pulseFx is the EFFECT it renders:
// CE_NONE == the v1.1 behaviour (a solid fill of g_pulseColor), any other effect is an
// effect-SWAP accent drawn by the same _renderLook() the base look uses.
static bool           g_pulseActive   = false;
static ContractEffect g_pulseFx       = CE_NONE;   // CE_NONE => solid fill (exactly v1.1)
static CRGB           g_pulseColor    = CRGB(255, 255, 255);
static uint32_t       g_pulseDeadline = 0;
static uint32_t       g_pulseStartMs  = 0;

// v1.2 beat-edge guard for the board's autonomous accent. BEAT_NONE == "no beat accented
// yet"; every show boundary (X / M:v=idle / M:v=show / clock re-seed / score load) resets
// it, or a stale edge from the last show swallows the next show's first accent.
static int32_t        g_lastAccentBeat = BEAT_NONE;

// beat-clock + score (shared core)
static BeatClock  g_clock;
// default 2 = pump on every beat (downbeat-emphasized) so a live A with m>0 but no
// am= still breathes; an explicit am= (incl. a section's am=0 calm) overrides it.
static uint8_t    g_activeAccentMode = 2;
static ScoreEntry g_score[8];
static int        g_scoreCount = 0;
static int        g_scoreIndex = -1;
// beat span of the active score section, for the am=3 ("build") ramp. Set on every
// section switch; only read while g_scoreIndex >= 0 (parity w/ ContractLogics.h:80-81).
static int32_t    g_sectionStart = 0;
static int32_t    g_sectionEnd   = 0;

// ---- small local helpers (no Arduino map/min dependency) --------------------
static PSI_NOINLINE inline uint8_t _clampBright(int v) {
  return (uint8_t)(v < 0 ? 0 : (v > SAFE_MAX_BRIGHTNESS ? SAFE_MAX_BRIGHTNESS : v));
}
// contract speed (0..255, higher=faster) -> primitive time_delay ms (inverse map).
static PSI_NOINLINE inline uint16_t _speedToDelay(uint8_t s) {
  // s=0 -> 400 ms (slow), s=255 -> 20 ms (fast)
  return (uint16_t)(400 - ((uint32_t)s * (400 - 20)) / 255);
}
static PSI_NOINLINE inline CRGB _scaled(const ContractRGB& c) { return CRGB(c.r, c.g, c.b); }
// Scale a color by a 0..255 factor. The uint16_t widening is REQUIRED, not cosmetic:
// `uint8_t * uint8_t` promotes to `int`, which is 16-bit on AVR, so a full-bright
// channel (255 * 255 = 65025) overflows a signed 16-bit int — undefined behavior on
// this board, and in practice a wrapped-negative channel (a bright pixel rendering
// dark/garbage). Widen the left operand first, exactly as the Logics/HPs forks do
// (ContractLogics.h:56-57, ContractFlthy.h:109-111).
static PSI_NOINLINE inline CRGB _scaleC(const CRGB& c, uint8_t v) {
  return CRGB((uint16_t)c.r * v / 255, (uint16_t)c.g * v / 255, (uint16_t)c.b * v / 255);
}

// ---- tiny formatters for the verb-Q ack (see CV_QUERY) -----------------------
// These exist to keep <stdio.h> OUT of this firmware. The verb-Q ack was the ONLY
// stdio call site in the whole image, and that one snprintf() dragged in avr-libc's
// vfprintf + __ultoa_invert + fputc + strnlen — ~1.3 KB of a 28 KB flash budget — to
// print one status line. The stock upstream firmware links no stdio at all.
// Each helper writes at `p` and returns the new write cursor. No bounds checking: the
// one caller sizes its buffer for the widest possible line (see CV_QUERY).
//
// NOTE — no signed-decimal helper: the only %d in the old format string was
// (int)g_effect, and ContractEffect is `enum : uint8_t` (contract_core.h:31), so it is
// ALWAYS 0..255 and can never carry a sign. An unsigned conversion is therefore
// byte-identical to the old %d, and a signed helper would be dead flash.
static char* _fmtStr(char* p, const char* s) {          // literal, no NUL written
  while (*s) *p++ = *s++;
  return p;
}
static char* _fmtU16(char* p, uint16_t v) {             // unsigned decimal, unpadded ("0" for 0)
  char tmp[5];                                          // uint16_t max = 65535 => 5 digits
  uint8_t n = 0;
  do { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } while (v);
  while (n) *p++ = tmp[--n];                            // emit most-significant first
  return p;
}
static char* _fmtHex8(char* p, uint8_t v) {             // 2-digit ZERO-PADDED LOWER-CASE hex
  // computed, not a lookup table: on AVR a `static const char[]` lands in .data (RAM),
  // and this board has 2.5 KB total. Two compares cost less than 17 bytes of RAM.
  const uint8_t hi = (uint8_t)(v >> 4), lo = (uint8_t)(v & 0x0f);
  *p++ = (char)(hi < 10 ? '0' + hi : 'a' + (hi - 10));
  *p++ = (char)(lo < 10 ? '0' + lo : 'a' + (lo - 10));
  return p;
}

// ---- addressing (fork spec §4) ----------------------------------------------
// class 'P'/'*' AND jumper: HIGH=front(!P F), LOW=rear(!P R). Fail-silent otherwise.
inline bool contractAddressed(char cls, char unit) {
  if (cls != 'P' && cls != '*') return false;         // not a PSI class
  if (unit == '*') return true;                        // any PSI
  bool isFront = (digitalRead(JUMP_FRONT_REAR) != 0);  // HIGH (pulled up) => front
  if (unit == 'F') return isFront;
  if (unit == 'R') return !isFront;
  return false;                                        // 'T' (HP) never matches PSI
}

// ---- rainbow (newly authored; ignores color per contract §6) ----------------
static uint8_t g_rainbowHue = 0;
static inline void _renderRainbow(uint16_t delayMs) {
  static uint32_t lastStep = 0;
  uint32_t now = millis();
  if (now - lastStep >= delayMs) { lastStep = now; g_rainbowHue += 3; }
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV((uint8_t)(g_rainbowHue + i * 4), 255, 255);
  FastLED.show(brightness());
}

// ---- comet (fx spatial helpers; brightness rides the global 3P path only) ---
// NOTE (all four column-built looks below): fill_column() only STAGES into leds[]
// (main.cpp:517-524) — it does not latch. loop() has no global FastLED.show(), so the
// frame must be pushed here or the panel freezes on the last shown frame. Every other
// primitive (allON/allOFF/scanCol/DiscoBall/VUMeter) shows internally; these must not
// forget to. Enforced by the latch guard in test/host/compile_contract.cpp.
// v1.2: every time-driven look takes its COLOR and its TIME ORIGIN as parameters rather
// than reading g_contractColor/g_effectStartMs directly, so _renderLook() can draw either
// the base look (g_contractColor, g_effectStartMs) or the accent overlay (g_pulseColor,
// g_pulseStartMs) through the SAME switch. The overlay owns its own timeline (t0), so a
// 180 ms ae=comet accent starts its head at position 0 instead of joining the base look's
// phase mid-flight — and the base look's own timeline is never disturbed by the accent.
inline void _renderComet(const CRGB& col, uint32_t t0) {
  uint32_t elapsed = millis() - t0;
  int head = fxHead(elapsed, g_speed, COLUMNS);
  for (int p = 0; p < COLUMNS; p++) {
    uint8_t cb = fxCometBright(p, head, COLUMNS);
    fill_column((uint8_t)p, _scaleC(col, cb), 0);
  }
  FastLED.show(brightness());
}

// ---- chase (fx spatial helper; brightness rides the global 3P path only) ----
inline void _renderChase(const CRGB& col, uint32_t t0) {
  uint32_t elapsed = millis() - t0;
  CRGB off(0, 0, 0);
  for (int p = 0; p < COLUMNS; p++) {
    bool lit = fxChaseLit(p, elapsed, g_speed);
    fill_column((uint8_t)p, lit ? col : off, 0);
  }
  FastLED.show(brightness());
}

// ---- wipe (fx spatial helper; brightness rides the global 3P path only) -----
inline void _renderWipe(const CRGB& col, uint32_t t0) {
  uint32_t elapsed = millis() - t0;
  CRGB off(0, 0, 0);
  for (int p = 0; p < COLUMNS; p++) {
    bool lit = fxWipeLit(p, elapsed, g_speed, COLUMNS);
    fill_column((uint8_t)p, lit ? col : off, 0);
  }
  FastLED.show(brightness());
}

// ---- gradient (fx hue helper; brightness rides the global 3P path only) -----
inline void _renderGradient(uint32_t t0) {
  uint32_t elapsed = millis() - t0;
  for (int p = 0; p < COLUMNS; p++) {
    ContractRGB c = fxHsv2rgb(fxGradientHue(p, COLUMNS, 0, elapsed, g_speed), 255, 255);
    fill_column((uint8_t)p, CRGB(c.r, c.g, c.b), 0);
  }
  FastLED.show(brightness());
}

// ---- colorcycle (fx hue helper; brightness rides the global 3P path only) ---
inline void _renderColorcycle(uint32_t t0) {
  uint32_t elapsed = millis() - t0;
  ContractRGB c = fxHsv2rgb(fxCycleHue(0, elapsed, g_speed), 255, 255);
  allON(CRGB(c.r, c.g, c.b), true, 0);
}

// ---- twinkle (fx per-LED helper; brightness rides the global 3P path only) --
inline void _renderTwinkle(const CRGB& col) {
  uint32_t now = millis();
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t tb = fxTwinkleBright(i, now, g_speed);
    leds[i] = _scaleC(col, tb);
  }
  FastLED.show(brightness());
}

// ---- strobe-capped flash toggle (reuses allON/allOFF; enforces < ~3 Hz) -----
static inline void _renderFlash(const CRGB& c, uint16_t delayMs, uint32_t t0) {
  uint32_t half = STROBE_MIN_STATE_MS;
  if (delayMs > half) half = delayMs;                  // never faster than the cap
  bool on = ((millis() - t0) % (2 * half)) < half;
  if (on) allON(c, true, 0); else allOFF(true, 0);
}

// ---- the ONE look renderer: draw `eff` in `col` on a timeline that starts at t0 -------
// Called twice (base look / accent overlay) — the switch body itself is emitted once, so
// the effect-swap accent costs ZERO flash for the effect vocabulary.
// Brightness is NOT a parameter: on this board every primitive latches through
// brightness() (the volatile 3P path), which contractLoopTick() sets to the beat-pumped
// envelope for the base look and to the un-pumped ceiling while the overlay is up.
inline void _renderLook(ContractEffect eff, const CRGB& col, uint32_t t0) {
  uint16_t d = _speedToDelay(g_speed);
  switch (eff) {
    case CE_OFF:     allOFF(true, 0);                               break;
    case CE_SOLID:   allON(col, true, 0);                           break;
    case CE_FLASH:   _renderFlash(col, d, t0);                      break;
    case CE_PULSE:   allON(col, true, 0);                           break;  // envelope pumps brightness
    case CE_RAINBOW: _renderRainbow(d);                             break;
    case CE_SCAN:    scanCol(d, 0, col, true);                      break;
    case CE_COMET:   _renderComet(col, t0);                         break;
    case CE_CHASE:   _renderChase(col, t0);                         break;
    case CE_WIPE:    _renderWipe(col, t0);                          break;
    case CE_GRADIENT:_renderGradient(t0);                           break;
    case CE_COLORCYCLE: _renderColorcycle(t0);                      break;
    case CE_TWINKLE: _renderTwinkle(col);                           break;
    case CE_SPARKLE: DiscoBall(d, 0, 3, col, 0);                    break;
    case CE_METER:   VUMeter(d, 0, 0);                              break;  // level[] fed by verb L
    case CE_NATIVE:  runPattern(g_nativeCode);                      break;
    default:         allON(col, true, 0);                           break;
  }
}

// ---- the parallel dispatcher: render each 25 ms tick -------------------------
inline void runContractAnim() {
  // The accent overlay wins while it is up. g_pulseFx == CE_NONE reproduces v1.1 exactly
  // (a solid fill of g_pulseColor); a v1.2 accent swaps in a real effect for ~180 ms.
  // The base look's g_effectStartMs / g_lastEffect bookkeeping is deliberately SKIPPED
  // here, so the base look re-inits when it actually resumes, not mid-accent.
  if (g_pulseActive) {
    _renderLook(g_pulseFx == CE_NONE ? CE_SOLID : g_pulseFx, g_pulseColor, g_pulseStartMs);
    return;
  }
  if (g_effect != g_lastEffect) {                      // effect switch => re-init native state machines
    firstTime = true;
    g_lastEffect = g_effect;
    g_effectStartMs = millis();
  }
  _renderLook(g_effect, g_contractColor, g_effectStartMs);
}

// ---- the ONE accent path: verb P (live) and the board's beat edge (scored) both land here.
// Returns false when the strobe cool-down coalesces this accent away.
// Photosensitivity: an accent may not START more often than every 2 * STROBE_MIN_STATE_MS
// (340 ms => <= 2.94 flashes/sec), and may not be SHORTER than one min-state.
// The cool-down is anchored UNCONDITIONALLY on the last fire (g_pulseStartMs is stamped
// here and nowhere else, so it IS the last-fire time — the same role Flthy's pulseLastMs
// plays). It used to also require !g_pulseActive, which meant a re-arm while the overlay
// was still up SKIPPED THE CAP ENTIRELY: a burst of accents inside one accent's window
// (a fast Pi mirroring verb P, or an every-beat score above ~176 BPM) could restart the
// overlay every few ms and strobe the panel far past ~3 Hz. Re-arming early is exactly
// the case the cap exists for, so it must be gated, not exempted.
static inline bool _fireAccent(ContractEffect fx, const CRGB& c, uint32_t durMs, uint32_t now) {
  if (!strobeCoolDownExpired(g_pulseStartMs, now, 2 * STROBE_MIN_STATE_MS)) return false;
  if (durMs < STROBE_MIN_STATE_MS) durMs = STROBE_MIN_STATE_MS;
  if (durMs > 2550u) durMs = 2550u;
  g_pulseFx       = fx;
  g_pulseColor    = c;
  g_pulseStartMs  = now;
  g_pulseDeadline = now + durMs;
  g_pulseActive   = true;
  g_contractArmed = true;
  return true;
}

// ---- verb dispatch ----------------------------------------------------------
// PSI_NOINLINE: called once, so this duplicates nothing — but folded into applyContract()
// (which folds into parseContract()) it made that one function big enough to spill the
// register allocator. Emitting it separately is -106 B. See the PSI_NOINLINE note above.
PSI_NOINLINE inline void _applyAnimate(const ContractParams& pr, uint32_t now) {
  // Phase-2: an A carrying at= schedules a section instead of applying now.
  if (pr.hasAt) {
    ScoreEntry e;
    e.atBeat     = pr.atBeat;
    e.effect     = pr.hasEffect ? pr.effect : CE_SOLID;
    e.color      = pr.hasColor ? pr.color : ContractRGB{ g_contractColor.r, g_contractColor.g, g_contractColor.b };
    e.speed      = pr.hasSpeed ? pr.speed : g_speed;
    e.beatMod    = pr.hasBeatMod ? pr.beatMod : g_beatMod;
    e.accentMode = pr.hasAm ? pr.accentMode : 0;
    e.nativeCode = pr.hasEffect ? pr.nativeCode : -1;  // thread native code into the score (parity w/ RSeries)
    // v1.2 accent overlay. No ae= => accentFx stays CE_NONE => the entry NEVER arms the
    // overlay => byte-for-byte v1.1 behaviour. ac= is RESOLVED here (else the entry's own
    // colour) so the score carries no "has-colour" flag; ad= is stored in 10 ms units.
    if (pr.hasAccentFx) {
      e.accentFx    = pr.accentFx;
      e.accentColor = pr.hasAccentColor ? pr.accentColor : e.color;
      e.accentDur10 = pr.hasAccentDur ? (uint8_t)(pr.accentDurMs / 10u) : 18;   // 18 => 180 ms
    }
    g_scoreCount = scoreInsert(g_score, g_scoreCount, 8, e);
    g_lastAccentBeat = BEAT_NONE;                      // a score load starts a NEW show: no stale edge
    return;
  }
  if (pr.hasColor)   g_contractColor = _scaled(pr.color);
  if (pr.hasSpeed)   g_speed   = pr.speed;
  if (pr.hasBright)  g_bright  = _clampBright(pr.bright);
  if (pr.hasBeatMod) g_beatMod = pr.beatMod;
  if (pr.hasAm)      g_activeAccentMode = pr.accentMode;
  if (pr.hasEffect) {
    g_effect = pr.effect;
    if (pr.effect == CE_NATIVE && pr.nativeCode >= 0) g_nativeCode = pr.nativeCode;
    g_lastEffect = CE_NONE;                            // force retrigger on next tick
  }
  g_animDeadline = (pr.hasDur && pr.durMs) ? now + pr.durMs : 0;
  g_effectStartMs = now;
  g_contractArmed = true;
  g_pulseActive = false;
}

inline void applyContract(const ParsedContract& p) {
  if (!contractAddressed(p.cls, p.unit)) return;       // fail-silent for other boards/units
  const ContractParams& pr = p.params;
  uint32_t now = millis();
  switch (p.verb) {
    case CV_ANIMATE:
      _applyAnimate(pr, now);
      break;

    case CV_PULSE: {                                   // one-shot accent, ALWAYS retriggers
      // v1.2: verb P now honours i= — the live (Phase-1) accent is the SAME effect-swap
      // overlay the scored (Phase-2) accent fires, so a mirrored show and a delivered one
      // look identical. No i= (everything Studio emits today) => CE_SOLID => exactly the
      // v1.1 solid fill. A stateful/native i= is refused by accentEffectAllowed (it would
      // corrupt the base look's state machine or latch the board), and falls back to solid.
      ContractEffect fx = (pr.hasEffect && accentEffectAllowed(pr.effect)) ? pr.effect : CE_SOLID;
      // && ordering matters: b= must apply only on an accent that actually FIRED, exactly
      // as it did in v1.1 (a cool-down-coalesced P changed nothing at all).
      if (_fireAccent(fx, pr.hasColor ? _scaled(pr.color) : g_contractColor,
                      pr.hasDur ? pr.durMs : 120, now) && pr.hasBright) {
        g_bright = _clampBright(pr.bright);
      }
      break;
    }

    case CV_CLOCK:                                     // seed/adjust the beat-clock (§9)
      beatClockSeed(g_clock, pr, now);
      // Studio re-anchors on every Play/seek: the beat ORIGIN moves, so a beat index from
      // the old timeline must not gate the new one (a seek back to beat 0 would otherwise
      // find lastAccentBeat == 0 already consumed and silently swallow the first accent).
      g_lastAccentBeat = BEAT_NONE;
      break;

    case CV_BRIGHT: {                                  // VOLATILE ride only (never 2P/EEPROM)
      uint8_t b = _clampBright(pr.hasBright ? pr.bright : (pr.hasLevel ? pr.level : g_bright));
      g_bright = b;
      useTempInternalBrightness = true;                // 3P volatile path (main.cpp:2562-2585)
      tempGlobalBrightnessValue = b;
      break;
    }

    case CV_LEVEL: {                                   // feed the VU meter from Studio energy
      // honor the inversion (main.cpp:1714): high energy => LOW level => tall bottom bar
      uint8_t v = pr.hasLevel ? pr.level : 0;
      int lvl = LEDS_PER_COLUMN - ((int)v * LEDS_PER_COLUMN) / 255;   // v=255 -> 0 (full), v=0 -> 6 (empty)
      if (lvl < 0) lvl = 0; if (lvl > LEDS_PER_COLUMN) lvl = LEDS_PER_COLUMN;
      for (int c = 0; c < COLUMNS; c++) level[c] = lvl;
      break;
    }

    case CV_STOP:                                      // blackout + de-energize + idle
      g_contractArmed = false;
      g_pulseActive = false;
      g_clock.running = false;
      scoreClear(g_scoreCount, g_scoreIndex);          // a show's sections must NOT leak into the next
      g_lastAccentBeat = BEAT_NONE;                    // ...nor a beat edge (see BEAT_NONE)
      useTempInternalBrightness = false;               // release volatile brightness back to native
      allOFF(true, 0);
      break;

    case CV_MODE:
      if (pr.mode == 's') {                            // show: hold the contract baseline
        g_contractArmed = true;
        g_lastEffect = CE_NONE;
        g_effectStartMs = now;
        // M:v=show is the FIRST line of a load burst, and Studio's Resend re-sends that burst
        // with no intervening X — so show entry means "a FRESH show". Without this, show B's
        // sections merge into show A's and, past the 8-entry cap, are dropped silently.
        scoreClear(g_scoreCount, g_scoreIndex);
        g_lastAccentBeat = BEAT_NONE;                  // ...and no beat edge survives the boundary
      } else if (pr.mode == 'i') {                     // idle: hand back to native autonomy
        g_contractArmed = false;
        scoreClear(g_scoreCount, g_scoreIndex);        // parity with X and with the Logics fork
        g_lastAccentBeat = BEAT_NONE;
        useTempInternalBrightness = false;
      }
      break;

    case CV_QUERY: {                                   // optional ack (targeted only)
      if (p.unit == 'F' || p.unit == 'R') {
        // Hand-rolled formatting — byte-identical to the snprintf() this replaces:
        //   "!P%cq:ver=1.2,phase=2,i=%d,c=%02x%02x%02x,bpm=%u\r"
        // Dropping that one call drops all of avr-libc's stdio (~1.3 KB) from the image.
        // test/host/compile_contract.cpp guard A10 pins these bytes exactly.
        // Widest line: "!PF"(3) + "q:ver=1.2,phase=2,i="(20) + i(<=3) + ",c="(3) + hex(6)
        //              + ",bpm="(5) + bpm(<=5) + "\r"(1) + NUL(1) = 47 B.
        char  buf[48];
        char* w = buf;
        *w++ = '!'; *w++ = 'P'; *w++ = p.unit;
        w = _fmtStr(w, "q:ver=1.2,phase=2,i=");
        w = _fmtU16(w, (uint16_t)g_effect);            // uint8_t enum => never negative
        w = _fmtStr(w, ",c=");
        w = _fmtHex8(w, g_contractColor.r);
        w = _fmtHex8(w, g_contractColor.g);
        w = _fmtHex8(w, g_contractColor.b);
        w = _fmtStr(w, ",bpm=");
        w = _fmtU16(w, g_clock.bpm);
        *w++ = '\r';
        *w   = '\0';
        serialPort->print(buf);
      }
      break;
    }

    default:
      break;
  }
}

// ---- loop hooks -------------------------------------------------------------
// Called every loop pass (spec §8 P): crisp pulse-deadline expiry.
inline void contractPulseTick() {
  // WRAP-SAFE deadline test. `millis() >= g_pulseDeadline` compares an ABSOLUTE deadline, and
  // g_pulseDeadline is `now + durMs` — which OVERFLOWS uint32 once every ~49.7 days, in the
  // durMs-wide window just before millis() wraps. The deadline then lands at a tiny value, the
  // absolute compare is instantly true, and the accent expires on its very next tick instead of
  // running its ~180 ms. The signed-difference idiom below is correct across the wrap and is
  // BIT-IDENTICAL everywhere else (verified exhaustively: zero disagreements outside the wrap
  // window), so no rendered value moves.
  // This is what the other two forks already do — Flthy (ContractFlthy.h:363) and the Logics
  // (ContractLogics.h:177) both store start+duration and test the ELAPSED difference, and
  // contract_core's strobeCoolDownExpired() uses the same unsigned-difference idiom for exactly
  // this reason. The PSI was the only board of the three still comparing an absolute deadline.
  if (g_pulseActive && (int32_t)(millis() - g_pulseDeadline) >= 0) {
    g_pulseActive = false;
    firstTime = true;                                  // re-init the native state machines the base look uses
    // Do NOT touch g_lastEffect / g_effectStartMs here. Clearing g_lastEffect made
    // runContractAnim() re-stamp g_effectStartMs = millis() on the very next frame, i.e.
    // it RESTARTED the base look's timeline after every accent. Tolerable at one accent
    // per bar; with a v1.2 am=2 accent a comet/wipe/gradient base would be reset to frame
    // zero on EVERY BEAT. The base look's timeline must survive the accent.
  }
}

// Called inside the 25 ms loop gate. Returns true when the contract owns the frame
// (so the native runPattern path is skipped). Drives beat-clock, score, envelope,
// then renders. Returns false when idle (native autonomy runs).
inline bool contractLoopTick() {
  if (!g_contractArmed) return false;
  uint32_t now = millis();

  // deadline: a finite-duration look reverts to idle swipe when it elapses.
  // Wrap-safe for the same reason as contractPulseTick() above — `now + pr.durMs` overflows
  // uint32 every ~49.7 days, and an absolute compare would then disarm the contract instantly
  // and hand the panel back to native autonomy mid-show. (g_animDeadline == 0 still means
  // "hold, no deadline", so the && guard is load-bearing and stays.)
  if (g_animDeadline && (int32_t)(now - g_animDeadline) >= 0) {
    g_contractArmed = false;
    useTempInternalBrightness = false;
    return false;
  }

  // Phase-2 score: switch looks on beat boundaries (shared-core scoreActiveIndex)
  if (g_clock.running && g_scoreCount > 0) {
    BeatPos bp = beatPosAt(g_clock, now);
    int idx = scoreActiveIndex(g_score, g_scoreCount, bp.beatIndex);
    if (idx >= 0 && idx != g_scoreIndex) {
      g_scoreIndex = idx;
      const ScoreEntry& e = g_score[idx];
      // section span drives the am=3 build ramp; the last section has no successor, so
      // give it a long tail rather than a zero-width span (parity w/ ContractLogics.h:308-309).
      g_sectionStart = e.atBeat;
      g_sectionEnd   = (idx + 1 < g_scoreCount) ? g_score[idx + 1].atBeat : (e.atBeat + 9999);
      g_effect = e.effect;
      if (e.effect == CE_NATIVE && e.nativeCode >= 0) g_nativeCode = e.nativeCode;  // scored native section -> authored code, not stale g_nativeCode
      g_contractColor = _scaled(e.color);
      g_speed = e.speed;
      g_beatMod = e.beatMod;
      g_activeAccentMode = e.accentMode;
      g_lastEffect = CE_NONE;
    }

    // v1.2 AUTONOMOUS ACCENT (the Phase-2 half of the effect-swap accent). Once per NEW
    // beat, fire the same overlay verb P fires live — so a delivered blueprint and a
    // live-mirrored show look the same. Order matters:
    //   1. consume the edge UNCONDITIONALLY (beatEdge advances the guard even when we do
    //      not fire) — otherwise a non-firing beat is re-tested on every 25 ms frame and,
    //      once its bar position happens to qualify, re-fires forever;
    //   2. then gate on the SHARED beatAccentFires() predicate — the same one
    //      beatAccentAmount() gates the brightness pump on, so the pump and the effect
    //      accent can never disagree about which beats carry an accent;
    //   3. accentFx == CE_NONE (any v1.1 entry, or a rejected ae=) => never arms.
    // Deliberately NOT gated on m=/beatMod: the pump depth and the accent effect are
    // independent knobs (an m=0 section can still punch an effect accent).
    if (beatEdge(g_lastAccentBeat, bp.beatIndex) && g_scoreIndex >= 0) {
      const ScoreEntry& e = g_score[g_scoreIndex];
      if (e.accentFx != CE_NONE && beatAccentFires(e.accentMode, bp.barPos)) {
        _fireAccent(e.accentFx, _scaled(e.accentColor),
                    e.accentDur10 ? (uint32_t)e.accentDur10 * 10u : 180u, now);
      }
    }
  }

  // beat accent envelope -> volatile brightness pump (never EEPROM).
  // Level math lives in contract_core's envBright() — the SHARED envelope all three
  // boards render through (b= is the ceiling, m= is the dip depth). Do not reintroduce
  // a board-local floor here: it desyncs the PSIs from the Logics/HPs on every cue.
  // am=0 (an explicit "calm" section) means NO pump — without that guard the accent
  // is always 0 and the board would sit parked at the dip floor (parity w/ RSeries/Flthy).
  // v1.2: while the accent overlay is up it renders at the unit's CEILING, un-pumped —
  // an accent that dipped with the envelope would be backwards (the PSI was the only
  // board that did this), and a scored accent must land at the same level on all three.
  uint8_t effBright = g_bright;
  if (!g_pulseActive && g_clock.running && g_beatMod && g_activeAccentMode) {
    BeatPos bp = beatPosAt(g_clock, now);
    // am=3 ("build") scales the accent by progress THROUGH THE SECTION. Passing a
    // constant 0 here made every build section return an accent of exactly 0 — i.e. the
    // board parked at the envelope floor for the section's whole length (fully black at
    // m=255). Derive real progress from the score span; beatAccentAmount clamps to 0..1
    // and ignores it for every other accent mode.
    float prog = 0.0f;
    if (g_scoreIndex >= 0 && g_sectionEnd > g_sectionStart)
      prog = (float)(bp.beatIndex - g_sectionStart) / (float)(g_sectionEnd - g_sectionStart);
    uint8_t env = beatAccentAmount(g_activeAccentMode, bp, g_beatMod, prog);
    effBright = envBright(g_bright, g_beatMod, env);
  }
  useTempInternalBrightness = true;                    // 3P volatile path only
  tempGlobalBrightnessValue = _clampBright(effBright);

  runContractAnim();
  return true;
}

// ---- serial intake dispatch (called from serialEvent/receiveEvent) ----------
// cmdString already assembled with the leading '!'. Route to the contract parser.
inline void parseContract(const char* cmd) {
  ParsedContract p;
  if (contractParse(cmd + 1, p)) applyContract(p);     // +1 skips the leading '!'
}

// ---- I2C DEFERRAL: get the contract OUT of the interrupt handler -------------------------
// receiveEvent() (main.cpp:2258) is the Wire onReceive callback, and it runs in INTERRUPT
// CONTEXT — it is called from the TWI ISR (__vector_36). Parsing and rendering there is a real
// hazard, for two reasons that compound:
//
//  1. STACK. FastLED's showPixels() ends with an UNCONDITIONAL `sei` (it cli's to bit-bang the
//     WS2812 data, patches timer0_millis, then re-enables interrupts). Arduino's twi.c re-arms
//     the I2C slave BEFORE invoking this callback. So the moment a render runs inside the ISR,
//     interrupts come back on with the slave live, and any further I2C traffic during the
//     render window RE-ENTERS the same ISR on top of the frame already on the stack. Measured
//     (test/host/stack_report.py): ~190-230 B per level against 1,076 B of headroom — one level
//     of nesting is 77% used, two is 95%, three overflows. An AVR stack overflow does not trap;
//     it walks down into .bss and silently corrupts globals. That is a miserable bug to chase
//     on a bench.
//  2. RE-ENTRANCY. parseContract() writes shared effect/LED state. It was reachable from BOTH
//     main context (serialEvent) and the ISR, so an I2C command could land in the middle of a
//     serial one and corrupt the state machine — a data bug entirely independent of the stack.
//
// THE HAZARD IS INHERITED, NOT INTRODUCED, and this fix is scoped accordingly. Stock upstream
// reaches the same `sei` by the same route (receiveEvent -> parseCommand -> runPattern ->
// allOFF -> FastLED.show), so an unmodified PSI Pro has it too. This fork made it worse by
// adding a SECOND route in, carrying parseContract — the single largest stack frame in the whole
// image (138 B). So: we take OUR path out of the ISR and leave Neil's native path exactly as it
// was. Fixing upstream's own exposure is not ours to do in a fork that carries his name.
//
// THE HANDSHAKE. g_i2cPending is the entire synchronisation, and the ordering is load-bearing:
//   * the ISR writes the buffer FIRST and publishes the flag LAST, so loop() can never observe a
//     half-written line;
//   * the ISR refuses to touch the buffer at all while the flag is set, so loop() can parse
//     straight out of g_i2cLine with interrupts ON and no copy — which matters, because copying
//     it to a local would put another 96 B on the stack we are trying to protect;
//   * loop() clears the flag only AFTER parsing, which closes the window completely.
// A line arriving while one is still un-serviced is DROPPED, not queued. That is deliberate: the
// drop window is one parse (tens of microseconds, against I2C commands that arrive tens of
// milliseconds apart), and dropping a command is the fail-safe direction where the old behaviour
// — re-entering the parser and corrupting live state — was not.
//
// `volatile` on the flag is required (loop() must re-read it, not cache it), and a bool is one
// byte on AVR, so the store is atomic and no cli/sei guard is needed around it.
static volatile bool g_i2cPending = false;
static char          g_i2cLine[CMD_MAX_LENGTH];

// Called from the I2C ISR. Copies the line and returns. Does NOT parse, does NOT render.
inline void contractQueueFromISR(const char* cmd) {
  if (g_i2cPending) return;                       // previous line not serviced yet -> drop
  uint8_t i = 0;
  while (cmd[i] && i < (uint8_t)(CMD_MAX_LENGTH - 1)) { g_i2cLine[i] = cmd[i]; i++; }
  g_i2cLine[i] = '\0';
  g_i2cPending = true;                            // publish LAST — this is the handshake
}

// Called from loop(), in MAIN context, every pass (not just inside the 25 ms gate — the first
// contract command has to be able to ARM the layer, and contractLoopTick() bails early when it
// is not yet armed, so servicing from in there would deadlock the very first '!' line).
inline void contractServicePending() {
  if (!g_i2cPending) return;
  parseContract(g_i2cLine);                       // safe with interrupts on: the ISR will not
  g_i2cPending = false;                           // write g_i2cLine while the flag is set
}

// Phase-1 boot hook (nothing yet; symmetry with the RSeries fork).
inline void contractSetup() { }
