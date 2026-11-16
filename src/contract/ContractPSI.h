// Part of the DroidNet Driveable-Animation Contract — an additive layer bolted onto
// Neil Hutchison's PSI Pro firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// src/contract/ContractPSI.h — PSI Pro (MaxPSI) firmware layer for the
// Driveable-Animation Contract. ADDITIVE: the native JawaLite T/A/D/P grammar is
// untouched; this header only adds the '!'-prefixed contract family. ZERO edits to the
// render primitives — every effect reuses an existing CRGB-parametric primitive with a
// live colour.
//
// ONE EXCEPTION, and it is not additive: this fork is SERIAL-ONLY BY DEFAULT. Upstream
// also listens on I2C, and that intake is compiled out unless PSI_ENABLE_I2C is defined
// (see the I2C INTAKE block in include/config.h for the full reasoning and the numbers).
// It is the single place this fork is less capable than the firmware it forked.
//
// INCLUDE ORDER (see main.cpp ~line 366): this header is included AFTER config.h /
// matrices.h / FastLED and AFTER the global state block (leds[], level[],
// useTempInternalBrightness/tempGlobalBrightnessValue, firstTime, lastPSIeventCode,
// serialPort) is declared, AFTER functions.h has prototyped the native render
// primitives it still calls (allON/allOFF/VUMeter/runPattern/brightness), and AFTER
// include/psi_vm.h (main.cpp ~line 359) — whose vmContractSparkle
// shims replaced the native scanCol()/DiscoBall() this layer used to call directly;
// see psi_vm.h's own "Contract-layer shims" comment, and AFTER src/wdp/wdp_fork.h, whose
// announcer the Q reply waits for. It is included BEFORE
// loop()/serialEvent()/receiveEvent() so those can call the contract entry points.
//
// DESIGN:
//   * State: the per-unit contract state is the core's ContractUnitState (g_u), and every
//     lifecycle step goes through the core's helpers (contractOnShow/OnStop/OnIdle,
//     contractShowBegin, contractLevel, scoreAccept). This file adds only what is the PSI's own:
//     who holds the panel, the owner's brightness, and the renderers.
//   * Color is TRACTABLE: allON(CRGB,...), fill_column() and VUMeter(...) in main.cpp all take a
//     live CRGB. CE_SPARKLE goes through vmContractSparkle(d, col) in include/psi_vm.h.
//     flash, pulse and scan use the core's one timing definition (fxFlashOn, fxPulseBright,
//     fxScanPos), the same on all three boards. _renderLook() is the one renderer, for the base
//     look and the overlay.
//   * Brightness rides the VOLATILE 3P path only (useTempInternalBrightness +
//     tempGlobalBrightnessValue, read by brightness() in main.cpp) — NEVER the 2P EEPROM path —
//     so B, the envelope and a show never wear EEPROM. The owner's own 3P state is saved when the
//     contract takes the panel and put back when it gives it back. The level is always
//     contractLevel(ceiling, b): a fraction of the owner's brightness, never above it (S6).
//   * Lifecycle (L1): M:v=show arms and keeps the native idle look until the show's first C,
//     live A or P; X is dark until M:v=idle or a look; M:v=idle returns to the default pattern.
//     A pulse with no show overlays the native look and returns to it (L3). A native T or D
//     command ends a show (L6).
//   * The accent overlay is ONE primitive with two triggers: verb P (live) and the board's own
//     per-beat score edge (a scored A's ae=/ac=/ad=). Same code path, same look.
//   * Verb L feeds the contract's own meter look (A:i=meter) and nothing else, honouring the VU
//     inversion (VUMeter in main.cpp: level[c] > i => pixel OFF, so high energy => LOW level).
//     A native VU mode (the owner's, or a show's native:21 / native:92) runs as stock (S10).
//   * Addressing = the JUMP_FRONT_REAR jumper: open (pulled up) = front (!P F), closed = rear (!P R).
#pragma once
// NO <stdio.h>. The verb-Q ack (CV_QUERY) was the one and only stdio call site in this
// firmware, and that single snprintf() linked in avr-libc's vfprintf (918 B) +
// __ultoa_invert (188 B) + fputc (96 B) + snprintf (90 B) + strnlen/strnlen_P (44 B) —
// ~1.3 KB of a 28 KB image — to emit one status line. The stock upstream firmware links
// none of it. The ack is now built by the _fmtStr/_fmtU16/_fmtHex8 helpers below, whose
// output is byte-identical (pinned by host guard A10). Do not re-add this include.
// The core's keyword tables and reply literals live in flash on the AVR (its two optional hooks):
// without them every one of those strings is copied into SRAM at boot.
#if defined(__AVR__)
  #include <avr/pgmspace.h>
  #define CONTRACT_PSTR(s)      PSTR(s)
  #define CONTRACT_READ_BYTE(p) pgm_read_byte(p)
#endif
#include "contract_core.h"

// ---- forward declaration: fill_column ---------------------------------------
// Every OTHER board primitive this layer calls (allON/allOFF/VUMeter/runPattern/
// brightness — the native ones; scanCol()/DiscoBall() are gone, replaced by the
// vmContractSparkle shim in include/psi_vm.h) is declared in
// include/functions.h, which main.cpp pulls in via preamble.h at line 286 — well
// above our include point at main.cpp:366. fill_column() is
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

// ---- safety and capacity ------------------------------------------------------
static const uint8_t SAFE_MAX_BRIGHTNESS = 200;   // Neil's LED-life cap; the owner's ceiling never exceeds it

// Score capacity (v1.3 section 6: at least 12), reported as cap= in the query reply. An entry is
// 22 B on AVR. The I2C build has 282 B less SRAM (Wire's buffers, the deferral line), so it holds
// 12; measured stack headroom is in DROIDNET_FORK.md and gated by run.sh.
#ifndef PSI_SCORE_CAP
  #ifdef PSI_ENABLE_I2C
    #define PSI_SCORE_CAP 12
  #else
    #define PSI_SCORE_CAP 16
  #endif
#endif

// ---- state ----------------------------------------------------------------------
// The whole per-unit contract state is the core's ContractUnitState (one unit on this board).
static ContractUnitState g_u;
static BeatClock         g_clock;
// Raw .bss storage, not `ScoreEntry g_score[N]`: an array of ScoreEntry runs the type's constructor
// over every entry at boot, and that start-up loop is 192 B of flash here (measured, both builds).
// Entries are only read below g_u.scoreLen and scoreAccept() writes a whole entry.
static uint8_t           g_scoreMem[sizeof(ScoreEntry) * PSI_SCORE_CAP];
static ScoreEntry* const g_score = (ScoreEntry*)(void*)g_scoreMem;

static ContractEffect g_lastEffect = CE_NONE;      // forces firstTime when the rendered look changes
static uint32_t       g_lastLineMs = 0;            // L2: the last well-formed line addressed to this board
// Section 10: a reply to Q is owed. It is written from loop() (contractReplyTick), never from the
// parser: nothing here may wait on the serial port, or a burst of lines would overrun it.
static char    g_replyUnit  = 0;                   // 'F' or 'R' while a reply is owed
static uint8_t g_replyRoute = 0;
// S6 and the give-back. g_owned: the contract holds the panel (a look, a dark baseline or a pulse),
// so the owner's own 3P state below is saved and is put back when the panel is given back.
static bool    g_owned        = false;
static bool    g_tookLook     = false;             // more than a pulse was shown: give-back returns to the swipe
static bool    g_ownerTempOn  = false;
static uint8_t g_ownerTempVal = 0;

// ---- small local helpers -----------------------------------------------------------
// contract speed (0..255, higher=faster) -> primitive time_delay ms (inverse map).
static PSI_NOINLINE inline uint16_t _speedToDelay(uint8_t s) {
  // s=0 -> 400 ms (slow), s=255 -> 20 ms (fast)
  return (uint16_t)(400 - ((uint32_t)s * (400 - 20)) / 255);
}
static PSI_NOINLINE inline CRGB _scaled(const ContractRGB& c) { return CRGB(c.r, c.g, c.b); }
// Scale a color by a 0..255 factor. The uint16_t widening is REQUIRED: `uint8_t * uint8_t` promotes
// to a 16-bit int on AVR and 255 * 255 overflows it.
static PSI_NOINLINE inline CRGB _scaleC(const CRGB& c, uint8_t v) {
  return CRGB((uint16_t)c.r * v / 255, (uint16_t)c.g * v / 255, (uint16_t)c.b * v / 255);
}

// ---- addressing (fork spec §4) ----------------------------------------------
// class 'P'/'*' AND jumper: HIGH=front(!P F), LOW=rear(!P R). Fail-silent otherwise.
inline bool contractAddressed(char cls, char unit) {
  if (cls != 'P' && cls != '*') return false;         // not a PSI class
  if (unit == '*') return true;                        // any PSI
  bool isFront = psiJumperOpen();                      // HIGH (pulled up) => front
  if (unit == 'F') return isFront;
  if (unit == 'R') return !isFront;
  return false;                                        // 'T' (HP) never matches PSI
}

// ---- ownership and the ceiling (S6) -----------------------------------------------
// Does the contract have something on the panel? CB_NONE with no pulse is the native look, even
// while a show is armed (L1: M:v=show keeps the idle look until the show's first C, A or P).
static inline bool _owns() { return g_u.base != CB_NONE || g_u.pulseActive; }

// The owner's own brightness right now: the pot, the EEPROM value or the owner's own 3P, never
// anything the contract wrote. While the contract holds the 3P override, read through it.
static PSI_NOINLINE uint8_t _ownerBri() {
  uint8_t b;
  if (g_owned) {
    const uint8_t mine = tempGlobalBrightnessValue;
    useTempInternalBrightness = g_ownerTempOn; tempGlobalBrightnessValue = g_ownerTempVal;
    b = brightness();
    useTempInternalBrightness = true; tempGlobalBrightnessValue = mine;
  } else {
    b = brightness();
  }
  return b > SAFE_MAX_BRIGHTNESS ? SAFE_MAX_BRIGHTNESS : b;
}
static PSI_NOINLINE void _takePanel() {
  if (g_owned) return;
  // S6: an armed show's ceiling is the owner's brightness when it armed (_boundary). A live look
  // or a pulse with no show armed takes it now.
  if (!g_u.inShow) g_u.ceil = _ownerBri();
  g_ownerTempOn  = useTempInternalBrightness;
  g_ownerTempVal = tempGlobalBrightnessValue;
  g_owned = true;
  g_tookLook = false;
}
// L1 "gives the board back completely": the owner's brightness state returns and, unless all the
// contract did was pulse over the native look (L3), the default pattern (swipe) runs next.
// When: contractLoopTick() calls this once nothing of the contract's is left to show AND the gate
// lets the native look light (S10: a native look runs as stock, but its START is a look change
// and, from dark, an onset). A native command from the owner calls it at once (_yieldNow).
static PSI_NOINLINE void _givePanelBack() {
  if (!g_owned) return;
  useTempInternalBrightness = g_ownerTempOn;
  tempGlobalBrightnessValue = g_ownerTempVal;
  if (g_tookLook) {
    lastPSIeventCode = defaultPattern;
    patternRunning = false;
    timingReceived = false;
  }
  g_owned = false;
}
static inline void _holdPanel() { _takePanel(); g_tookLook = true; }

// The owner sent 3P (the native volatile-brightness command) while the contract holds the panel.
// main.cpp has just written the 3P pair, which is the pair the show's frames ride on: take the
// owner's value into the saved copy, so that it is what a Resend arms with and what the panel is
// given back at. (The show in progress keeps the ceiling it armed with: S6. The next tick puts
// the show's level back in the pair.) The pot and the EEPROM value need nothing: the contract
// never writes them, and _ownerBri() reads them live.
inline void contractOwnerSet3P() {
  if (!g_owned) return;
  g_ownerTempOn  = useTempInternalBrightness;
  g_ownerTempVal = tempGlobalBrightnessValue;
}

// L1: the three boundaries. Each runs the core's ONE reset, then its own display action.
static PSI_NOINLINE void _boundary(char kind, uint8_t wdSec) {    // 's'how, 'x' stop, 'i'dle
  const uint8_t ceil = _ownerBri();
  if (kind == 's')      contractOnShow(g_u, ceil, wdSec);
  else if (kind == 'x') contractOnStop(g_u, ceil);
  else                  contractOnIdle(g_u, ceil);
  g_clock.running = false;                             // one unit on this board: nothing needs the clock now
  g_lastEffect = CE_NONE;
  if (kind == 'x') _holdPanel();                       // dark, and it stays dark until idle or a look
  // show and idle: the native look returns (after M:v=show, until the first C, A or P). The
  // hand-over itself is contractLoopTick()'s, at its next frame and when the gate allows it.
}
// The show's first C, live A or P on an armed unit: the baseline becomes dark.
static inline void _showBegin() { if (contractShowBegin(g_u)) _holdPanel(); }

// L6: a native T, A or D command addressed to this PSI ends a contract show on it, as M:v=idle
// would, and then runs (main.cpp calls contractYieldToNative() before it runs the command). The
// command sets its own pattern state, so the give-back here restores the owner's brightness and
// leaves the pattern alone. In the I2C build the native command can arrive in the TWI interrupt:
// there the yield is only a flag, and loop() runs it before it draws or parses anything else
// (_serviceYield(), first thing in contractLoopTick() and in contractServicePending()).
static PSI_NOINLINE void _yieldNow() {
  // Always the whole reset, held panel or not: a score that is loaded with the clock running
  // would otherwise take the panel back at its next section, by itself.
  g_tookLook = false;                                  // the native command chose the pattern
  _boundary('i', 0);
  _givePanelBack();                                    // now: the owner's own light is never held back
}
#ifdef PSI_ENABLE_I2C
static volatile bool g_yieldPending = false;
inline void contractYieldToNative() { g_yieldPending = true; }
static inline void _serviceYield() { if (g_yieldPending) { g_yieldPending = false; _yieldNow(); } }
#else
inline void contractYieldToNative() { _yieldNow(); }
static inline void _serviceYield() { }
#endif

// Verb L feeds the contract's OWN meter look, and only while the contract is drawing it. The fed
// level is kept here and goes into level[] (main.cpp) when the contract runs VUMeter() for
// A:i=meter; a native VU mode (the owner's 21 or 92, or a show's native:21 / native:92) never
// sees it and runs as stock (S10): its bars are its own random walk, which no line can empty and
// refill. With no L for 2 s the random walk runs under the contract's meter too, so a meter look
// with no level feed still moves.
static uint8_t  g_levelRows = 0;                       // rows dark in every column: 0 full .. 6 empty
static bool     g_levelFed = false;
static bool     g_meterMine = false;                   // VUMeter() is running for the contract's meter look
static uint32_t g_levelMs  = 0;
inline bool contractHoldsLevels() {
  if (!g_meterMine) return false;
  if (g_levelFed && (uint32_t)(millis() - g_levelMs) >= 2000u) g_levelFed = false;
  return g_levelFed;
}

// ---- S9: one limit for every path --------------------------------------------------------
// Called by FastLED.show() for EVERY frame, as the last thing before it goes out (the hook is
// PSI_FRAME_GATE, set in main.cpp). It measures the frame's field level (the mean over the LEDs
// of each LED's brightest channel, times the master scale) and returns the scale to send.
//   * A frame the contract DRAWS (its looks, the overlay, X's dark) goes through the core's
//     field gate: a dark-to-lit swing across the field is let through only 340 ms after the last
//     one, and until then the frame is scaled down. It is also never brighter than the ceiling.
//   * A frame of a native look is never changed (S10: native looks run as stock), whether the
//     owner started the look or a show selected it from the allow-list. The gate is only told
//     about it, so that what the contract does next keeps its distance. What the contract may
//     hold is the look's EDGES, and it does that before the look draws: the start of a
//     native:<n> look and the brightness a show gives it from then on (contractLoopTick), and
//     the hand-over back to the board's own look (_givePanelBack). An accent overlay still up
//     when its section gives way to a native:<n> one is the contract's own drawing: gated.
// The level is computed exactly, floor(sum * scale / (n * 255)): at a low brightness a unit of
// level is several percent of the ceiling, and a measure that rounded twice would miss a rise
// an observer can see. The measuring is 48 compares and two divides; nothing here blocks.
#ifndef PSI_FRAME_GATE_WIRED
#error "FastLED.h was included before main.cpp named PSI_FRAME_GATE: frames would skip the S9 gate"
#endif
uint8_t contractFrameScale(const CRGB* l, uint8_t n, uint8_t scale) {
  uint16_t sum = 0;                                    // 48 * 255 fits
  for (uint8_t i = 0; i < n; i++) {
    uint8_t m = l[i].r;
    if (l[i].g > m) m = l[i].g;
    if (l[i].b > m) m = l[i].b;
    sum += m;
  }
  const uint32_t now = millis();
#ifdef PSI_ENABLE_I2C
  const uint8_t sreg = SREG;                           // a native frame can be drawn from the TWI
  cli();                                               // interrupt: keep the gate's state whole
#endif
  const bool drawn = g_owned && (g_u.base != CB_NATIVE || g_u.pulseActive);   // the contract drew this frame itself
  if (drawn && scale > g_u.ceil) scale = g_u.ceil;
  const uint8_t level = (uint8_t)((uint16_t)(((uint32_t)sum * scale) / 255u) / n);
  if (drawn) {
    const uint8_t allowed = contractFieldGate(g_u, level, now);
    if (allowed < level) scale = (uint8_t)(((uint16_t)scale * allowed) / level);   // rounds down
  } else {
    if (!g_owned && !g_u.inShow) g_u.ceil = scale;     // no show: the step follows the owner's level
    contractFieldForced(g_u, level, now);
  }
#ifdef PSI_ENABLE_I2C
  SREG = sreg;
#endif
  return scale;
}

// The gate, asked without being told: how far may the field rise to `level` right now? One copy
// of the core's contractFieldAllowed() for the brightness a show gives a native look.
static PSI_NOINLINE uint8_t _gateAllows(uint8_t level) { return contractFieldAllowed(g_u, level, millis()); }
// May a native look be given the panel now? Yes when a dark-to-lit onset would be allowed: 340 ms
// after the field last lit or got brighter, whether the panel is lit or dark at this moment. The
// contract cannot see what the look will draw. It may clear the panel and light it at once (most
// native modes do when they start), or be in a dark stretch of its own and relight at its own
// time: to anyone watching, that light follows the contract's last one. So the two edges where
// frames of the contract's give way to a native look's (a native:<n> starting, the hand-back to
// the board's own look) both wait for this, and the panel keeps what it shows until then.
static PSI_NOINLINE bool _gateLetsNativeLight() {
  return strobeCoolDownExpired(g_u.fieldOnsetMs, millis(), 2 * STROBE_MIN_STATE_MS);
}

// ---- the looks this build has ----------------------------------------------------
// PSI_NO_STRIP_LOOKS (set only in the PSIPro-i2c environment) builds R1 without the six strip and
// hue looks: with Wire and the I2C intake linked, they do not fit the part. A line that names one
// is not dropped and still counts for verb V; it draws the nearest look this build has:
//   comet, chase, wipe -> scan       twinkle -> sparkle       gradient, colorcycle -> solid in c=
// As an accent overlay all six become solid: scan and sparkle keep state between frames, which is
// why the core refuses them as accents. R2's fixed-point clock is what brings the six back.
#ifdef PSI_NO_STRIP_LOOKS
static PSI_NOINLINE ContractEffect _lookHere(ContractEffect e, bool overlay) {
  switch (e) {
    case CE_COMET: case CE_CHASE: case CE_WIPE: return overlay ? CE_SOLID : CE_SCAN;
    case CE_TWINKLE:                            return overlay ? CE_SOLID : CE_SPARKLE;
    case CE_GRADIENT: case CE_COLORCYCLE:       return CE_SOLID;
    default:                                    return e;
  }
}
#else
static inline ContractEffect _lookHere(ContractEffect e, bool) { return e; }
#endif

// ---- rainbow (newly authored; ignores color per contract §6) ----------------
static uint8_t g_rainbowHue = 0;
static inline void _renderRainbow(uint16_t delayMs) {
  static uint32_t lastStep = 0;
  uint32_t now = millis();
  if (now - lastStep >= delayMs) { lastStep = now; g_rainbowHue += 3; }
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV((uint8_t)(g_rainbowHue + i * 4), 255, 255);
  FastLED.show(brightness());
}

// ---- the column-built looks. fill_column() only STAGES into leds[]; each must show. ----
#ifndef PSI_NO_STRIP_LOOKS
inline void _renderComet(const CRGB& col, uint32_t t0) {
  uint32_t elapsed = millis() - t0;
  int head = fxHead(elapsed, g_u.speed, COLUMNS);
  for (int p = 0; p < COLUMNS; p++) {
    uint8_t cb = fxCometBright(p, head, COLUMNS);
    fill_column((uint8_t)p, _scaleC(col, cb), 0);
  }
  FastLED.show(brightness());
}
inline void _renderChase(const CRGB& col, uint32_t t0) {
  uint32_t elapsed = millis() - t0;
  CRGB off(0, 0, 0);
  for (int p = 0; p < COLUMNS; p++) {
    bool lit = fxChaseLit(p, elapsed, g_u.speed);
    fill_column((uint8_t)p, lit ? col : off, 0);
  }
  FastLED.show(brightness());
}
inline void _renderWipe(const CRGB& col, uint32_t t0) {
  uint32_t elapsed = millis() - t0;
  CRGB off(0, 0, 0);
  for (int p = 0; p < COLUMNS; p++) {
    bool lit = fxWipeLit(p, elapsed, g_u.speed, COLUMNS);
    fill_column((uint8_t)p, lit ? col : off, 0);
  }
  FastLED.show(brightness());
}
inline void _renderGradient(uint32_t t0) {
  uint32_t elapsed = millis() - t0;
  for (int p = 0; p < COLUMNS; p++) {
    ContractRGB c = fxHsv2rgb(fxGradientHue(p, COLUMNS, 0, elapsed, g_u.speed), 255, 255);
    fill_column((uint8_t)p, CRGB(c.r, c.g, c.b), 0);
  }
  FastLED.show(brightness());
}
inline void _renderColorcycle(uint32_t t0) {
  uint32_t elapsed = millis() - t0;
  ContractRGB c = fxHsv2rgb(fxCycleHue(0, elapsed, g_u.speed), 255, 255);
  allON(CRGB(c.r, c.g, c.b), true, 0);
}
inline void _renderTwinkle(const CRGB& col) {
  uint32_t now = millis();
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t tb = fxTwinkleBright(i, now, g_u.speed);
    leds[i] = _scaleC(col, tb);
  }
  FastLED.show(brightness());
}
#endif  // PSI_NO_STRIP_LOOKS
// ---- flash and scan: the core's shared timing (contract v1.3 section 12) -------------------
// flash: a state is 170 ms at s=255 up to 425 ms at s=0, so it can never pass the strobe cap.
static inline void _renderFlash(const CRGB& c, uint32_t t0) {
  if (fxFlashOn(millis() - t0, g_u.speed)) allON(c, true, 0); else allOFF(true, 0);
}
// scan: ONE lit column that walks the panel and back, a step every fxStepMs(speed).
static inline void _renderScan(const CRGB& col, uint32_t t0) {
  const int pos = fxScanPos(millis() - t0, g_u.speed, COLUMNS, false);
  CRGB off(0, 0, 0);
  for (int p = 0; p < COLUMNS; p++) fill_column((uint8_t)p, p == pos ? col : off, 0);
  FastLED.show(brightness());
}

// ---- the ONE look renderer: draw `eff` in `col` on a timeline that starts at t0 -------
// Called for the base look and for the accent overlay; the switch body is emitted once.
// Brightness is NOT a parameter: every primitive latches through brightness() (the volatile 3P
// path), which contractLoopTick() sets each frame.
inline void _renderLook(ContractEffect eff, const CRGB& col, uint32_t t0) {
  uint16_t d = _speedToDelay(g_u.speed);
  switch (eff) {
    case CE_OFF:     allOFF(true, 0);                               break;
    case CE_SOLID:   allON(col, true, 0);                           break;
    case CE_FLASH:   _renderFlash(col, t0);                         break;
    case CE_PULSE:   allON(col, true, 0);                           break;  // contractLoopTick() breathes or pumps the level
    case CE_SCAN:    _renderScan(col, t0);                          break;
    case CE_RAINBOW: _renderRainbow(d);                             break;
#ifndef PSI_NO_STRIP_LOOKS                               // else _lookHere() never lets one reach here
    case CE_COMET:   _renderComet(col, t0);                         break;
    case CE_CHASE:   _renderChase(col, t0);                         break;
    case CE_WIPE:    _renderWipe(col, t0);                          break;
    case CE_GRADIENT:_renderGradient(t0);                           break;
    case CE_COLORCYCLE: _renderColorcycle(t0);                      break;
    case CE_TWINKLE: _renderTwinkle(col);                           break;
#endif
    case CE_SPARKLE: vmContractSparkle(d, col);                     break;
    case CE_METER:                                                          // level[] fed by verb L
      g_meterMine = true;
      if (contractHoldsLevels()) for (uint8_t c = 0; c < COLUMNS; c++) level[c] = g_levelRows;
      VUMeter(d, 0, 0);
      g_meterMine = false;
      break;
    case CE_NATIVE:  runPattern(g_u.nativeCode);                    break;
    default:         allON(col, true, 0);                           break;
  }
}

// ---- the ONE accent path: verb P (live) and the board's beat edge (scored) both land here.
// Returns false when the strobe cool-down coalesces this accent away: an accent may not START
// more often than every 2 * STROBE_MIN_STATE_MS. pulseLastMs survives every reset (the core keeps
// it), so a reset cannot open the cap.
static PSI_NOINLINE bool _fireAccent(ContractEffect fx, const ContractRGB& c, uint32_t durMs, uint8_t bright,
                                     uint32_t now) {
  if (g_u.base == CB_NATIVE) return false;             // R1: no overlay on a native:<n> look, scored or live
  if (!strobeCoolDownExpired(g_u.pulseLastMs, now, 2 * STROBE_MIN_STATE_MS)) return false;
  _takePanel();                                        // L3: a pulse may overlay the native look
  g_u.pulseFx      = _lookHere(fx, true);
  g_u.pulseColor   = c;
  g_u.pulseBright  = bright;                           // the overlay's own level; never the base look's
  g_u.pulseDurMs   = accentDurClamp(durMs);
  g_u.pulseStartMs = now;
  g_u.pulseLastMs  = now;
  g_u.pulseActive  = true;
  return true;
}

// A live look: now, or when its S4 hold ends.
static PSI_NOINLINE void _applyLook(ContractLookPatch lp, uint32_t now) {
  lookPatchDefault(lp, g_u.base);                      // L5: no i= and no look yet is solid
  if (lp.has & LP_COLOR)   g_u.color = lp.color;
  if (lp.has & LP_SPEED)   g_u.speed = lp.speed;
  if (lp.has & LP_BRIGHT)  g_u.bright = lp.bright;
  if (lp.has & LP_BEATMOD) g_u.beatMod = lp.beatMod;
  if (lp.has & LP_AM)      g_u.accentMode = lp.accentMode;
  if (lp.has & LP_EVERY)   g_u.accentEvery = lp.every;
  g_u.startMs = now;
  g_u.pulseActive = false;
  if (lp.has & LP_EFFECT) {
    _holdPanel();
    g_u.effect = _lookHere(lp.effect, false);
    g_u.nativeCode = lp.nativeCode;
    g_u.base = (lp.effect == CE_NATIVE) ? CB_NATIVE : CB_LOOK;
    g_lastEffect = CE_NONE;                            // force a retrigger on the next tick
  }
}

// ---- verb dispatch ----------------------------------------------------------
PSI_NOINLINE inline void _applyAnimate(const ContractParams& pr, uint16_t lineCrc, uint32_t now) {
  // S7: native codes are an allow-list, the core's (nativeAllowedPsi: modes 0 to 22 and 92 without
  // Flash, Alarm and Scream, which swing the whole field at 6.4 and 3.8 Hz). A line naming any
  // other code is dropped whole, live or scored; a scored one then fails verb V, as it should.
  if (pr.hasEffect && pr.effect == CE_NATIVE && !nativeAllowedPsi(pr.nativeCode)) return;
  if (pr.hasAt) {                                      // a scored section: stored, not shown
    // A scored A carries its look: an entry is a whole section, not a patch. Without i= the line
    // is dropped (and then counts against verb V, as it should).
    if (!pr.hasEffect) return;
    ScoreEntry e;
    e.atBeat     = pr.atBeat;
    e.effect     = _lookHere(pr.effect, false);
    e.nativeCode = pr.nativeCode;
    e.color      = pr.hasColor ? pr.color : g_u.color;
    e.speed      = pr.hasSpeed ? pr.speed : 128;
    e.bright     = pr.hasBright ? pr.bright : 255;     // S6: carried in the entry; 255 is the ceiling
    e.beatMod    = pr.hasBeatMod ? pr.beatMod : 0;
    e.accentMode = pr.hasAm ? pr.accentMode : 0;
    e.accentEvery = accentEveryFrom(pr);               // an=, ap=: with am=2, the beats where beat % n == ap
    if (pr.hasAccentFx) {
      e.accentFx    = pr.accentFx;
      e.accentColor = pr.hasAccentColor ? pr.accentColor : e.color;
      e.accentDur10 = pr.hasAccentDur ? (uint8_t)(pr.accentDurMs / 10u) : 18;   // 18 => 180 ms
    }
    // The entry keeps the CRC of its line: the core tells a line delivered twice (which changes
    // nothing, the playing section included) from a new line for the same beat (which replaces).
    scoreAccept(g_u, g_score, PSI_SCORE_CAP, e, lineCrc);
    // C1, as after a C: with the clock running and the beat well under way, that beat counts as
    // accented already, so a section that arrives mid-beat does not fire it again.
    g_u.lastAccentBeat = accentEdgeAfterSeed(g_clock, now);
    return;
  }
  _showBegin();
  // S4: a live look less than 170 ms after the last one applied is held, and lines held in the
  // same window merge key by key; contractLoopTick() applies it when the window ends.
  const ContractLookPatch lp = lookPatchFrom(pr);
  if (liveLookOffer(g_u, lp, now)) _applyLook(lp, now);
}

inline void applyContract(const ParsedContract& p, uint16_t lineCrc, uint32_t now) {
  const ContractParams& pr = p.params;
  switch (p.verb) {
    case CV_ANIMATE:
      _applyAnimate(pr, lineCrc, now);
      break;

    case CV_PULSE: {                                   // one-shot accent; outside a show it overlays the native look (L3)
      _showBegin();
      ContractEffect fx = (pr.hasEffect && accentEffectAllowed(pr.effect)) ? pr.effect : CE_SOLID;
      // b= on P is the overlay's alone (S9): it never becomes the base look's level.
      _fireAccent(fx, pr.hasColor ? pr.color : ContractRGB{255, 255, 255}, pr.hasDur ? pr.durMs : 120,
                  pr.hasBright ? pr.bright : g_u.bright, now);
      break;
    }

    case CV_CLOCK: {                                   // seed, correct or hold the beat clock
      ContractSeed k = beatClockSeed(g_clock, pr, now);
      if (k == CS_IGNORED || k == CS_HOLD) break;      // C6: a hold freezes the cursor and keeps the look
      _showBegin();
      // C1: a seek that lands on a beat still accents it; a correction that lands more than 60 ms
      // into a beat does not fire that beat again.
      g_u.lastAccentBeat = accentEdgeAfterSeed(g_clock, now);
      g_u.pinForward = (k == CS_NUDGE);                // C2: a correction never moves the cursor back
      break;
    }

    case CV_BRIGHT:                                    // a fraction of the ceiling; never the 2P/EEPROM path
      if (!contractBrightHonoured(g_u)) break;         // S6: only in show mode, and a reset clears it
      g_u.bright = pr.hasBright ? pr.bright : (pr.hasLevel ? pr.level : g_u.bright);
      break;

    case CV_LEVEL: {                                   // feed the contract's meter look from Studio energy
      // honor the inversion (main.cpp VUMeter: level[c] > i => pixel OFF): high energy => LOW level
      uint8_t v = pr.hasLevel ? pr.level : 0;
      g_levelRows = (uint8_t)(LEDS_PER_COLUMN - ((uint16_t)v * LEDS_PER_COLUMN) / 255u);   // v=255 -> 0 (full), v=0 -> 6 (empty)
      g_levelMs = now;
      g_levelFed = true;
      break;
    }

    case CV_STOP:
      _boundary('x', 0);
      break;

    case CV_MODE:
      if (pr.mode == 's')      _boundary('s', pr.hasWd ? pr.wd : 0);   // L2: wd=<s> arms the watchdog
      else if (pr.mode == 'i') _boundary('i', 0);
      break;

    case CV_VERIFY:                                    // section 7: is the score held the one the host sent?
      if (!pr.hasN || !pr.hasK) break;
      // No: back to the native look, which the owner can see with no return path. The reset keeps
      // ok=2 for Q, and the core drops every scored line until the next M:v=show.
      if (scoreVerify(g_u, g_score, pr.n, pr.k) == 2) _boundary('i', 0);
      break;

    case CV_QUERY:                                     // section 10: by name and with a route, or no reply
      if (contractQueryAnswerable(p)) { g_replyUnit = p.unit; g_replyRoute = pr.route; }
      break;

    default:
      break;
  }
}

// ---- loop hooks -------------------------------------------------------------
// Called every loop pass: crisp pulse expiry (wrap-safe elapsed compare).
inline void contractPulseTick() {
  if (g_u.pulseActive && !contractOverlayUp(g_u, millis())) {   // the core's one expiry rule
    // Over the board's own native look (L3) nothing of the contract's is left: contractLoopTick()
    // hands the panel back at its next frame. Over a contract look: re-init its state machines.
    if (_owns()) firstTime = true;
  }
}

// Called every loop pass: send an owed Q reply when it cannot block and cannot land inside a WDP
// announce. Not while an announce is in flight, and only when the whole reply (57 bytes at most)
// fits the serial TX buffer. One reply a pass; a second Q before the first is answered replaces it.
inline void contractReplyTick() {
  if (!g_replyUnit || gWdp.len != 0 || serialPort->availableForWrite() < (int)CONTRACT_REPLY_MAX) return;
  char buf[CONTRACT_REPLY_MAX];
  const uint8_t n = contractFormatQuery(buf, sizeof buf, g_replyRoute, 'P', g_replyUnit, PSI_SCORE_CAP,
                                        g_u.scoreLen, g_u.loadOk, g_clock.running ? g_clock.bpm : 0);
  serialPort->write((const uint8_t*)buf, n);
  g_replyUnit = 0;
}

// Called inside the 25 ms loop gate. Returns true when the contract owns the frame (so the native
// runPattern path is skipped). Watchdog, held look, score, accent, envelope, then render.
inline bool contractLoopTick() {
  _serviceYield();                                     // L6: a native command since the last pass
  uint32_t now = millis();
  // L2: wd seconds with no well-formed line addressed to this board, in show mode: as M:v=idle.
  if (contractWatchdogExpired(g_u, g_lastLineMs, now)) _boundary('i', 0);
  { ContractLookPatch lp; if (liveLookDue(g_u, now, lp)) _applyLook(lp, now); }   // S4: a held look's window ended

  BeatPos bp;                                          // ONE beat position for the whole tick
  if (g_clock.running) {
    bp = beatPosAt(g_clock, now);
    if (g_u.scoreLen) {
      int idx = scoreCursor(g_u.curSection, scoreActiveIndex(g_score, g_u.scoreLen, bp.beatIndex), g_u.pinForward);
      if (idx >= 0 && idx != g_u.curSection) {         // a section boundary
        const ScoreEntry& e = g_score[idx];
        _holdPanel();
        g_u.curSection   = (int8_t)idx;
        g_u.sectionStart = e.atBeat;
        g_u.sectionEnd   = (idx + 1 < g_u.scoreLen) ? g_score[idx + 1].atBeat : (e.atBeat + 9999);
        g_u.effect = e.effect; g_u.nativeCode = e.nativeCode; g_u.color = e.color; g_u.speed = e.speed;
        g_u.bright = e.bright; g_u.beatMod = e.beatMod; g_u.accentMode = e.accentMode;
        g_u.accentEvery = e.accentEvery;
        g_u.startMs = now;
        g_u.base = (e.effect == CE_NATIVE) ? CB_NATIVE : CB_LOOK;
        g_lastEffect = CE_NONE;
      }
      // The scored accent. Consume the edge first, always (or a beat that does not fire is tested
      // again on every frame); then the SAME predicate the pump uses (S2). A native:<n> section
      // takes no overlay in R1 (section 6; _fireAccent refuses it).
      if (beatEdge(g_u.lastAccentBeat, bp.beatIndex) && g_u.curSection >= 0) {
        const ScoreEntry& e = g_score[g_u.curSection];
        if (e.accentFx != CE_NONE && beatAccentFires(e.accentMode, bp, g_clock, e.accentEvery)) {
          _fireAccent(e.accentFx, e.accentColor, (uint32_t)e.accentDur10 * 10u, g_u.bright, now);   // it rides the look's level
        }
      }
    }
  }

  if (!_owns()) {                                      // nothing of ours left to show: the native look runs
    // S10: handing the panel to the native look is the contract's edge, and the light that look
    // shows next is counted as the contract's doing. So the hand-over waits until an onset is
    // allowed (_gateLetsNativeLight); until then the panel keeps what it shows. The native look's
    // own frames then tell the gate (contractFrameScale).
    if (g_owned) {
      if (!_gateLetsNativeLight()) return true;
      _givePanelBack();
    }
    return false;
  }

  // S6: b= and B are fractions of the owner's ceiling. The pump rides under that level. The
  // overlay has its own fraction (a P's b=, or the look's level for a scored accent), un-pumped.
  const uint8_t lvl = contractLevel(g_u.ceil, g_u.pulseActive ? g_u.pulseBright : g_u.bright);
  uint8_t effBright = lvl;
  if (!g_u.pulseActive && g_clock.running && g_u.beatMod && g_u.accentMode) {
    // Progress through the section in Q16 (0..65536), for am=3. No float anywhere in this layer or
    // in the core: one float expression links 1.3 KB of soft-float library.
    const uint32_t prog = (g_u.curSection >= 0) ? beatSectionProgress16(bp.beatIndex, g_u.sectionStart, g_u.sectionEnd) : 0;
    uint8_t env = beatAccentAmount(g_u.accentMode, bp, g_u.beatMod, prog, g_clock, g_u.accentEvery);
    effBright = envBright(lvl, g_u.beatMod, env);
  } else if (g_u.pulseActive ? g_u.pulseFx == CE_PULSE : (g_u.base == CB_LOOK && g_u.effect == CE_PULSE)) {
    // pulse: the shared breathe (a 2000 ms cycle at s=0 down to 340 ms at s=255, never faster: S8)
    // whenever nothing pumps the look. Under the beat pump the envelope IS the breathe.
    const uint32_t t0 = g_u.pulseActive ? g_u.pulseStartMs : g_u.startMs;
    effBright = (uint8_t)(((uint16_t)lvl * fxPulseBright(now - t0, g_u.speed)) / 255u);
  }
  const ContractEffect eff = (g_u.base == CB_DARK) ? CE_OFF : g_u.effect;
  if (g_u.base == CB_NATIVE) {
    // S10: a native:<n> look runs as stock and its frames are not held. What the contract holds is
    // the brightness it gives the look: every rise of it (B, b=, the pump) goes only as far as the
    // gate allows now. Every native mode draws at brightness(), so a look that is dark when the
    // brightness comes up inside the gate's window stays dim until the window ends. (The start of
    // the look is held as a whole, below.)
    const uint8_t allowed = _gateAllows(effBright);
    if (allowed < effBright) effBright = allowed;
  }
  useTempInternalBrightness = true;                    // 3P volatile path only, never EEPROM
  tempGlobalBrightnessValue = effBright;

  if (g_u.pulseActive) {
    _renderLook(g_u.pulseFx == CE_NONE ? CE_SOLID : g_u.pulseFx, _scaled(g_u.pulseColor), g_u.pulseStartMs);
    return true;
  }
  if (eff != g_lastEffect) {                           // a new look: re-init the native state machines
    // S10: a native:<n> look starts. That is the contract's edge, and one the brightness cannot
    // soften: most modes clear the panel and draw at once, whatever was on it. So the start waits
    // until an onset is allowed, the panel keeping what it shows.
    if (g_u.base == CB_NATIVE && !_gateLetsNativeLight()) return true;
    firstTime = true;
    g_lastEffect = eff;
  }
  _renderLook(eff, _scaled(g_u.color), g_u.startMs);
  return true;
}

// ---- serial intake dispatch (called from serialEvent/receiveEvent) ----------
// cmdString already assembled with the leading '!'.
inline void parseContract(const char* cmd) {
  _serviceYield();                                     // a native command that came first is applied first
  ParsedContract p;
  if (!contractParse(cmd + 1, p)) return;              // +1 skips the leading '!'
  if (!contractAddressed(p.cls, p.unit)) return;       // fail-silent for other boards and units
  const uint32_t now = millis();
  g_lastLineMs = now;                                  // L2: any verb, an unanswered wildcard Q included
  // A scored line is stored with the CRC of its bytes after '!', as received.
  const uint16_t crc = (p.verb == CV_ANIMATE && p.params.hasAt) ? contractCrc16(cmd + 1) : 0;
  applyContract(p, crc, now);
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
// Only compiled when there IS an I2C ISR to defer out of. In the default serial-only build
// there is no TWI vector, commands arrive via serialEvent() in main context, and this queue
// would be 96 B of SRAM guarding a hazard that cannot occur.
#ifdef PSI_ENABLE_I2C
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
  _serviceYield();                                // L6, out of the interrupt
  if (!g_i2cPending) return;
  parseContract(g_i2cLine);                       // safe with interrupts on: the ISR will not
  g_i2cPending = false;                           // write g_i2cLine while the flag is set
}
#else
// Serial-only build: no I2C ISR exists, so there is nothing to defer and no queue to pay for.
// loop() still calls contractServicePending() unconditionally; it compiles away to nothing.
inline void contractQueueFromISR(const char*) { }
inline void contractServicePending() { }
#endif  // PSI_ENABLE_I2C

// Phase-1 boot hook (nothing yet; symmetry with the RSeries fork).
inline void contractSetup() { }
