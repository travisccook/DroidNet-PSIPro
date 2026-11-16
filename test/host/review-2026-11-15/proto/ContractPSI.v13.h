// Part of the DroidNet Driveable-Animation Contract — an additive layer bolted onto
// Neil Hutchison's PSI Pro firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// src/contract/ContractPSI.h — PSI Pro (MaxPSI) firmware layer for the
// Driveable-Animation Contract. ADDITIVE: the native JawaLite T/A/D/P grammar is
// untouched; this header only adds the '!'-prefixed contract family. ZERO edits to the
// render primitives — every effect reuses an existing CRGB-parametric primitive with a
// live g_contractColor.
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
// include/psi_vm.h (main.cpp ~line 359) — whose vmContractScan/vmContractSparkle
// shims replaced the native scanCol()/DiscoBall() this layer used to call directly;
// see psi_vm.h's own "Contract-layer shims" comment, and AFTER src/wdp/wdp_fork.h, whose
// wdpFinish() the verb-Q ack calls. It is included BEFORE
// loop()/serialEvent()/receiveEvent() so those can call the contract entry points.
//
// DESIGN (verified against src/main.cpp + include/config.h; re-verified 2026-07-14
// after the animation-VM sweep deleted flash()/scanCol()/Cylon_Col()/DiscoBall()):
//   * Color is TRACTABLE: allON(CRGB,...) main.cpp:493, fill_column/fill_row
//     main.cpp:533/563, displayMatrixColor main.cpp:576, VUMeter(...) main.cpp:795 —
//     all already take a live CRGB. The two effects that used to call native
//     scanCol()/DiscoBall() directly (CE_SCAN/CE_SPARKLE, below) now go through
//     vmContractScan(d, col)/vmContractSparkle(d, col) in include/psi_vm.h — shims
//     that take the same live CRGB `col` the deleted natives did, not a VM palette
//     byte. runContractAnim() is a parallel dispatcher (vs runPattern main.cpp:957)
//     driven by g_effect/g_contractColor.
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
// Every OTHER board primitive this layer calls (allON/allOFF/VUMeter/runPattern/
// brightness — the native ones; scanCol()/DiscoBall() are gone, replaced by the
// vmContractScan/vmContractSparkle shims in include/psi_vm.h) is declared in
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

// Score capacity (v1.3 section 6), reported as cap= in the Q reply. An entry is 22 B on AVR.
#ifndef PSI_SCORE_CAP
  #ifdef PSI_ENABLE_I2C
    #define PSI_SCORE_CAP 12     // the I2C build has less SRAM (Wire's buffers, the deferral line)
  #else
    #define PSI_SCORE_CAP 16
  #endif
#endif

// ---- state ----------------------------------------------------------------------
// The whole per-unit contract state is the core's ContractUnitState (one unit on this board).
static ContractUnitState g_u;
static BeatClock         g_clock;
// Raw .bss storage, not `ScoreEntry g_score[N]`: ScoreEntry carries default member initialisers, so
// an array of it is constant-initialised and its image is stored in flash to be copied to SRAM at
// boot. Entries are only read below g_u.scoreLen and scoreAccept() writes a whole entry.
static uint8_t           g_scoreMem[sizeof(ScoreEntry) * PSI_SCORE_CAP];
static ScoreEntry* const g_score = (ScoreEntry*)(void*)g_scoreMem;

static uint32_t       g_lastLineMs = 0;            // L2: the last well-formed line addressed to this board
static ContractEffect g_lastEffect = CE_NONE;      // forces firstTime when the rendered look changes
// S6 and the give-back. g_owned: the contract holds the panel (a look, a dark baseline or a pulse),
// so the owner's own 3P state below is saved and is put back when the panel is given back.
static bool    g_owned        = false;
static bool    g_tookLook     = false;             // more than a pulse was shown: give-back returns to the swipe
static bool    g_ownerTempOn  = false;
static uint8_t g_ownerTempVal = 0;
// Verb L feeds level[] for the meter. With no L for 2 s the stock random walk runs (section 6).
static bool     g_levelOwned = false;              // read by VUMeter() in main.cpp
static uint32_t g_levelMs    = 0;
// The Q reply is sent from loop(), never from the parser: nothing here may block the reader (section 11).
static char    g_replyUnit  = 0;                   // 'F' or 'R' while a reply is owed
static uint8_t g_replyRoute = 0;

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

// S7: the native codes a show may select. Upstream modes 0-21 and 92 and the fork's 22, less 2, 3
// and 5: Flash and Alarm/Scream are full-field white at 8.3 Hz and 4 Hz, past the strobe cap.
// (Moves to the core as nativeAllowedPsi() so Studio reads the same list.)
static PSI_NOINLINE bool _nativeAllowed(int n) {
  if (n == 92) return true;
  return n >= 0 && n <= 22 && n != 2 && n != 3 && n != 5;
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
  g_u.ceil = _ownerBri();                              // a live look or a pulse with no reset before it
  g_ownerTempOn  = useTempInternalBrightness;
  g_ownerTempVal = tempGlobalBrightnessValue;
  g_owned = true;
  g_tookLook = false;
}
// L1 "gives the board back completely": the owner's brightness state returns and, unless all the
// contract did was pulse over the native look (L3), the default pattern (swipe) runs next.
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

// L1: the three boundaries. Each runs the core's ONE reset, then its own display action.
static PSI_NOINLINE void _boundary(char kind, uint8_t wdSec) {    // 's'how, 'x' stop, 'i'dle
  const uint8_t ceil = _ownerBri();
  if (kind == 's')      contractOnShow(g_u, ceil, wdSec);
  else if (kind == 'x') contractOnStop(g_u, ceil);
  else                  contractOnIdle(g_u, ceil);
  g_clock.running = false;                             // one unit on this board: nothing needs the clock now
  g_lastEffect = CE_NONE;
  if (kind == 'x') _holdPanel();                       // dark, and it stays dark until idle or a look
  else             _givePanelBack();                   // show: the idle look until the first C, A or P
}
// The show's first C, live A or P on an armed unit: the baseline becomes dark.
static inline void _showBegin() { if (contractShowBegin(g_u)) _holdPanel(); }

// A native T, A or D command addressed to this PSI takes the panel back from a show that is
// displaying (main.cpp calls this before it runs the command, which then sets its own pattern
// state). In the I2C build this runs in the TWI interrupt; it never renders.
inline void contractYieldToNative() {
  if (g_owned) _boundary('i', 0);
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

// ---- the column-built looks. fill_column() only STAGES into leds[]; each must show. ----
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
// ---- strobe-capped flash toggle (reuses allON/allOFF; enforces < ~3 Hz) -----
static inline void _renderFlash(const CRGB& c, uint16_t delayMs, uint32_t t0) {
  uint32_t half = STROBE_MIN_STATE_MS;
  if (delayMs > half) half = delayMs;                  // never faster than the cap
  bool on = ((millis() - t0) % (2 * half)) < half;
  if (on) allON(c, true, 0); else allOFF(true, 0);
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
    case CE_FLASH:   _renderFlash(col, d, t0);                      break;
    case CE_PULSE:   allON(col, true, 0);                           break;  // the envelope pumps brightness (S8 holds: S2 floors it)
    case CE_SCAN:    vmContractScan(d, col);                        break;
#ifndef PSI_SLIM_LOOKS
    case CE_RAINBOW: _renderRainbow(d);                             break;
    case CE_COMET:   _renderComet(col, t0);                         break;
    case CE_CHASE:   _renderChase(col, t0);                         break;
    case CE_WIPE:    _renderWipe(col, t0);                          break;
    case CE_GRADIENT:_renderGradient(t0);                           break;
    case CE_COLORCYCLE: _renderColorcycle(t0);                      break;
    case CE_TWINKLE: _renderTwinkle(col);                           break;
#endif
    case CE_SPARKLE: vmContractSparkle(d, col);                     break;
    case CE_METER:   VUMeter(d, 0, 0);                              break;  // level[] fed by verb L
    case CE_NATIVE:  runPattern(g_u.nativeCode);                    break;
    default:         allON(col, true, 0);                           break;
  }
}

// ---- the ONE accent path: verb P (live) and the board's beat edge (scored) both land here.
// Returns false when the strobe cool-down coalesces this accent away: an accent may not START
// more often than every 2 * STROBE_MIN_STATE_MS. pulseLastMs survives every reset (the core keeps
// it), so a reset cannot open the cap.
static PSI_NOINLINE bool _fireAccent(ContractEffect fx, const ContractRGB& c, uint32_t durMs, uint32_t now) {
  if (!strobeCoolDownExpired(g_u.pulseLastMs, now, 2 * STROBE_MIN_STATE_MS)) return false;
  _takePanel();                                        // L3: a pulse may overlay the native look
  g_u.pulseFx      = fx;
  g_u.pulseColor   = c;
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
    g_u.effect = lp.effect;
    g_u.nativeCode = lp.nativeCode;
    g_u.base = (lp.effect == CE_NATIVE) ? CB_NATIVE : CB_LOOK;
    g_lastEffect = CE_NONE;                            // force a retrigger on the next tick
  }
}

// ---- verb dispatch ----------------------------------------------------------
PSI_NOINLINE inline void _applyAnimate(const ContractParams& pr, uint16_t lineCrc, uint32_t now) {
  // S7: a line naming a native code off the allow-list is dropped whole (a scored one then fails V).
  if (pr.hasEffect && pr.effect == CE_NATIVE && !_nativeAllowed(pr.nativeCode)) return;
  if (pr.hasAt) {                                      // a scored section: stored, not shown
    ScoreEntry e;
    e.atBeat     = pr.atBeat;
    e.effect     = pr.hasEffect ? pr.effect : CE_SOLID;
    e.nativeCode = pr.hasEffect ? pr.nativeCode : -1;
    e.color      = pr.hasColor ? pr.color : g_u.color;
    e.speed      = pr.hasSpeed ? pr.speed : 128;
    e.bright     = pr.hasBright ? pr.bright : 255;     // 255 is the ceiling
    e.beatMod    = pr.hasBeatMod ? pr.beatMod : 0;
    e.accentMode = pr.hasAm ? pr.accentMode : 0;
    e.accentEvery = accentEveryFrom(pr);
    if (pr.hasAccentFx) {
      e.accentFx    = pr.accentFx;
      e.accentColor = pr.hasAccentColor ? pr.accentColor : e.color;
      e.accentDur10 = pr.hasAccentDur ? (uint8_t)(pr.accentDurMs / 10u) : 18;   // 18 => 180 ms
    }
    scoreAccept(g_u, g_score, PSI_SCORE_CAP, e, lineCrc);   // full => the load is marked bad
    g_u.lastAccentBeat = accentEdgeAfterSeed(g_clock, now); // C1, not a blind reset
    return;
  }
  _showBegin();
  ContractLookPatch lp = lookPatchFrom(pr);
  if (liveLookOffer(g_u, lp, now)) _applyLook(lp, now);     // S4: else held for the tick
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
      if (_fireAccent(fx, pr.hasColor ? pr.color : ContractRGB{255, 255, 255},
                      pr.hasDur ? pr.durMs : 120, now) && pr.hasBright) {
        g_u.bright = pr.bright;
      }
      break;
    }

    case CV_CLOCK: {                                   // seed, correct or hold the beat clock
      ContractSeed k = beatClockSeed(g_clock, pr, now);
      if (k == CS_IGNORED || k == CS_HOLD) break;      // C6: a hold freezes the cursor and keeps the look
      _showBegin();
      g_u.lastAccentBeat = accentEdgeAfterSeed(g_clock, now);   // C1
      g_u.pinForward = (k == CS_NUDGE);                         // C2
      break;
    }

    case CV_BRIGHT:                                    // a fraction of the ceiling; never the 2P/EEPROM path
      g_u.bright = pr.hasBright ? pr.bright : (pr.hasLevel ? pr.level : g_u.bright);
      break;

    case CV_LEVEL: {                                   // feed the VU meter from Studio energy
      // honor the inversion (main.cpp VUMeter: level[c] > i => pixel OFF): high energy => LOW level
      uint8_t v = pr.hasLevel ? pr.level : 0;
      int lvl = LEDS_PER_COLUMN - ((int)v * LEDS_PER_COLUMN) / 255;   // v=255 -> 0 (full), v=0 -> 6 (empty)
      for (int c = 0; c < COLUMNS; c++) level[c] = lvl;
      g_levelMs = now;
      g_levelOwned = true;
      break;
    }

    case CV_STOP:
      _boundary('x', 0);
      break;

    case CV_MODE:
      if (pr.mode == 's')      _boundary('s', pr.hasWd ? pr.wd : 0);
      else if (pr.mode == 'i') _boundary('i', 0);
      break;

    case CV_VERIFY:                                    // section 7
      if (!pr.hasN || !pr.hasK) break;
      if (scoreVerify(g_u, g_score, pr.n, pr.k) == 2) _boundary('i', 0);   // back to the native look, visibly
      break;

    case CV_QUERY:                                     // section 10: targeted and routed, or not answered
      if (contractQueryAnswerable(p)) { g_replyUnit = p.unit; g_replyRoute = pr.route; }
      break;

    default:
      break;
  }
}

// ---- loop hooks -------------------------------------------------------------
// Called every loop pass: crisp pulse expiry (wrap-safe elapsed compare).
inline void contractPulseTick() {
  if (g_u.pulseActive && (uint32_t)(millis() - g_u.pulseStartMs) >= g_u.pulseDurMs) {
    g_u.pulseActive = false;
    if (!_owns()) { _givePanelBack(); return; }        // L3: the native look returns
    firstTime = true;                                  // re-init the native state machines the base look uses
  }
}

// Called every loop pass: send an owed Q reply when it cannot block. Not while a WDP announce is
// in flight, and only when the whole reply fits the serial TX buffer.
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
  uint32_t now = millis();
  if (contractWatchdogExpired(g_u, g_lastLineMs, now)) _boundary('i', 0);     // L2
  { ContractLookPatch lp; if (liveLookDue(g_u, now, lp)) _applyLook(lp, now); }   // S4

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
      // The scored accent. Consume the edge first, always; then the SAME predicate the pump uses
      // (S2). A native section takes no overlay in R1 (section 6).
      if (beatEdge(g_u.lastAccentBeat, bp.beatIndex) && g_u.curSection >= 0) {
        const ScoreEntry& e = g_score[g_u.curSection];
        if (e.accentFx != CE_NONE && e.effect != CE_NATIVE &&
            beatAccentFires(e.accentMode, bp, g_clock, e.accentEvery)) {
          _fireAccent(e.accentFx, e.accentColor, (uint32_t)e.accentDur10 * 10u, now);
        }
      }
    }
  }

  if (!_owns()) { _givePanelBack(); return false; }    // nothing of ours on the panel: native runs

  // S6: b= and B are fractions of the owner's ceiling. The pump rides under that level; the
  // overlay renders at it, un-pumped.
  const uint8_t lvl = contractLevel(g_u.ceil, g_u.bright);
  uint8_t effBright = lvl;
  if (!g_u.pulseActive && g_clock.running && g_u.beatMod && g_u.accentMode) {
    float prog = (g_u.curSection >= 0) ? beatSectionProgress(bp.beatIndex, g_u.sectionStart, g_u.sectionEnd) : 0.0f;
    uint8_t env = beatAccentAmount(g_u.accentMode, bp, g_u.beatMod, prog, g_clock, g_u.accentEvery);
    effBright = envBright(lvl, g_u.beatMod, env);
  }
  useTempInternalBrightness = true;                    // 3P volatile path only, never EEPROM
  tempGlobalBrightnessValue = effBright;
  if (g_levelOwned && (uint32_t)(now - g_levelMs) >= 2000u) g_levelOwned = false;   // no L for 2 s: the random walk

  if (g_u.pulseActive) {
    _renderLook(g_u.pulseFx == CE_NONE ? CE_SOLID : g_u.pulseFx, _scaled(g_u.pulseColor), g_u.pulseStartMs);
    return true;
  }
  const ContractEffect eff = (g_u.base == CB_DARK) ? CE_OFF : g_u.effect;
  if (eff != g_lastEffect) {                           // a new look: re-init the native state machines
    firstTime = true;
    g_lastEffect = eff;
  }
  _renderLook(eff, _scaled(g_u.color), g_u.startMs);
  return true;
}

// ---- serial intake dispatch (called from serialEvent/receiveEvent) ----------
// cmdString already assembled with the leading '!'.
inline void parseContract(const char* cmd) {
  ParsedContract p;
  if (!contractParse(cmd + 1, p)) return;              // +1 skips the leading '!'
  if (!contractAddressed(p.cls, p.unit)) return;       // fail-silent for other boards and units
  const uint32_t now = millis();
  g_lastLineMs = now;                                  // L2: any well-formed line addressed to this board
  // Verb V checks the CRC of each held entry's line: the bytes after '!', as received.
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
