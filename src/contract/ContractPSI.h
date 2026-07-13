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
//     runPattern no-op guard (main.cpp:2121-2129).
//   * Verb L feeds level[10] (main.cpp:327) honoring the VU inversion
//     (main.cpp:1714: level[c] > i => pixel OFF, so high energy => LOW level).
//   * Addressing = JUMP_FRONT_REAR jumper (config.h:101-105, main.cpp:358):
//     HIGH=front (!P F), LOW=rear (!P R).
#pragma once
#include <stdio.h>          // snprintf (verb Q ack line)
#include "contract_core.h"

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

// verb P (pulse) overlay — own deadline, always resets
static bool     g_pulseActive   = false;
static CRGB     g_pulseColor     = CRGB(255, 255, 255);
static uint32_t g_pulseDeadline = 0;
static uint32_t g_pulseStartMs  = 0;

// beat-clock + score (shared core)
static BeatClock  g_clock;
// default 2 = pump on every beat (downbeat-emphasized) so a live A with m>0 but no
// am= still breathes; an explicit am= (incl. a section's am=0 calm) overrides it.
static uint8_t    g_activeAccentMode = 2;
static ScoreEntry g_score[8];
static int        g_scoreCount = 0;
static int        g_scoreIndex = -1;

// ---- small local helpers (no Arduino map/min dependency) --------------------
static inline uint8_t _clampBright(int v) {
  return (uint8_t)(v < 0 ? 0 : (v > SAFE_MAX_BRIGHTNESS ? SAFE_MAX_BRIGHTNESS : v));
}
// contract speed (0..255, higher=faster) -> primitive time_delay ms (inverse map).
static inline uint16_t _speedToDelay(uint8_t s) {
  // s=0 -> 400 ms (slow), s=255 -> 20 ms (fast)
  return (uint16_t)(400 - ((uint32_t)s * (400 - 20)) / 255);
}
static inline CRGB _scaled(const RGB& c) { return CRGB(c.r, c.g, c.b); }

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
inline void _renderComet() {
  uint32_t elapsed = millis() - g_effectStartMs;
  int head = fxHead(elapsed, g_speed, COLUMNS);
  for (int p = 0; p < COLUMNS; p++) {
    uint8_t cb = fxCometBright(p, head, COLUMNS);
    CRGB c(( g_contractColor.r * cb) / 255, (g_contractColor.g * cb) / 255, (g_contractColor.b * cb) / 255);
    fill_column((uint8_t)p, c, 0);
  }
  FastLED.show(brightness());
}

// ---- chase (fx spatial helper; brightness rides the global 3P path only) ----
inline void _renderChase() {
  uint32_t elapsed = millis() - g_effectStartMs;
  CRGB off(0, 0, 0);
  for (int p = 0; p < COLUMNS; p++) {
    bool lit = fxChaseLit(p, elapsed, g_speed);
    fill_column((uint8_t)p, lit ? g_contractColor : off, 0);
  }
  FastLED.show(brightness());
}

// ---- wipe (fx spatial helper; brightness rides the global 3P path only) -----
inline void _renderWipe() {
  uint32_t elapsed = millis() - g_effectStartMs;
  CRGB off(0, 0, 0);
  for (int p = 0; p < COLUMNS; p++) {
    bool lit = fxWipeLit(p, elapsed, g_speed, COLUMNS);
    fill_column((uint8_t)p, lit ? g_contractColor : off, 0);
  }
  FastLED.show(brightness());
}

// ---- gradient (fx hue helper; brightness rides the global 3P path only) -----
inline void _renderGradient() {
  uint32_t elapsed = millis() - g_effectStartMs;
  for (int p = 0; p < COLUMNS; p++) {
    RGB c = fxHsv2rgb(fxGradientHue(p, COLUMNS, 0, elapsed, g_speed), 255, 255);
    fill_column((uint8_t)p, CRGB(c.r, c.g, c.b), 0);
  }
  FastLED.show(brightness());
}

// ---- colorcycle (fx hue helper; brightness rides the global 3P path only) ---
inline void _renderColorcycle() {
  uint32_t elapsed = millis() - g_effectStartMs;
  RGB c = fxHsv2rgb(fxCycleHue(0, elapsed, g_speed), 255, 255);
  allON(CRGB(c.r, c.g, c.b), true, 0);
}

// ---- twinkle (fx per-LED helper; brightness rides the global 3P path only) --
inline void _renderTwinkle() {
  uint32_t now = millis();
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t tb = fxTwinkleBright(i, now, g_speed);
    leds[i] = CRGB((g_contractColor.r * tb) / 255, (g_contractColor.g * tb) / 255, (g_contractColor.b * tb) / 255);
  }
  FastLED.show(brightness());
}

// ---- strobe-capped flash toggle (reuses allON/allOFF; enforces < ~3 Hz) -----
static inline void _renderFlash(const CRGB& c, uint16_t delayMs) {
  uint32_t half = STROBE_MIN_STATE_MS;
  if (delayMs > half) half = delayMs;                  // never faster than the cap
  bool on = ((millis() - g_effectStartMs) % (2 * half)) < half;
  if (on) allON(c, true, 0); else allOFF(true, 0);
}

// ---- the parallel dispatcher: render g_effect each 25 ms tick ----------------
inline void runContractAnim() {
  if (g_effect != g_lastEffect) {                      // effect switch => re-init native state machines
    firstTime = true;
    g_lastEffect = g_effect;
    g_effectStartMs = millis();
  }
  uint16_t d = _speedToDelay(g_speed);

  // verb-P overlay wins while active (drawn as a solid flash-look)
  if (g_pulseActive) { allON(g_pulseColor, true, 0); return; }

  switch (g_effect) {
    case CE_OFF:     allOFF(true, 0);                                   break;
    case CE_SOLID:   allON(g_contractColor, true, 0);                  break;
    case CE_FLASH:   _renderFlash(g_contractColor, d);              break;
    case CE_PULSE:   allON(g_contractColor, true, 0);                  break;  // envelope pumps brightness
    case CE_RAINBOW: _renderRainbow(d);                             break;
    case CE_SCAN:    scanCol(d, 0, g_contractColor, true);          break;
    case CE_COMET:   _renderComet();                                break;
    case CE_CHASE:   _renderChase();                                break;
    case CE_WIPE:    _renderWipe();                                 break;
    case CE_GRADIENT:_renderGradient();                             break;
    case CE_COLORCYCLE: _renderColorcycle();                        break;
    case CE_TWINKLE: _renderTwinkle();                              break;
    case CE_SPARKLE: DiscoBall(d, 0, 3, g_contractColor, 0);        break;
    case CE_METER:   VUMeter(d, 0, 0);                              break;  // level[] fed by verb L
    case CE_NATIVE:  runPattern(g_nativeCode);                      break;
    default:         allON(g_contractColor, true, 0);                  break;
  }
}

// ---- verb dispatch ----------------------------------------------------------
inline void _applyAnimate(const ContractParams& pr, uint32_t now) {
  // Phase-2: an A carrying at= schedules a section instead of applying now.
  if (pr.hasAt) {
    ScoreEntry e;
    e.atBeat     = pr.atBeat;
    e.effect     = pr.hasEffect ? pr.effect : CE_SOLID;
    e.color      = pr.hasColor ? pr.color : RGB{ g_contractColor.r, g_contractColor.g, g_contractColor.b };
    e.speed      = pr.hasSpeed ? pr.speed : g_speed;
    e.beatMod    = pr.hasBeatMod ? pr.beatMod : g_beatMod;
    e.accentMode = pr.hasAm ? pr.accentMode : 0;
    e.nativeCode = pr.hasEffect ? pr.nativeCode : -1;  // thread native code into the score (parity w/ RSeries)
    g_scoreCount = scoreInsert(g_score, g_scoreCount, 8, e);
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
      // strobe cool-down: coalesce pulses arriving faster than the cap
      if (!g_pulseActive && g_pulseStartMs != 0 && (now - g_pulseStartMs) < 2 * STROBE_MIN_STATE_MS) break;
      g_pulseColor    = pr.hasColor ? _scaled(pr.color) : g_contractColor;
      uint32_t len    = pr.hasDur ? pr.durMs : 120;
      if (len < STROBE_MIN_STATE_MS) len = STROBE_MIN_STATE_MS;   // clamp min off/on window
      if (pr.hasBright) g_bright = _clampBright(pr.bright);
      g_pulseStartMs  = now;
      g_pulseDeadline = now + len;
      g_pulseActive   = true;
      g_contractArmed = true;
      break;
    }

    case CV_CLOCK:                                     // seed/adjust the beat-clock (§9)
      beatClockSeed(g_clock, pr, now);
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
      useTempInternalBrightness = false;               // release volatile brightness back to native
      allOFF(true, 0);
      break;

    case CV_MODE:
      if (pr.mode == 's') {                            // show: hold the contract baseline
        g_contractArmed = true;
        g_lastEffect = CE_NONE;
        g_effectStartMs = now;
      } else if (pr.mode == 'i') {                     // idle: hand back to native autonomy
        g_contractArmed = false;
        scoreClear(g_scoreCount, g_scoreIndex);        // parity with X and with the Logics fork
        useTempInternalBrightness = false;
      }
      break;

    case CV_QUERY:                                     // optional ack (targeted only)
      if (p.unit == 'F' || p.unit == 'R') {
        char buf[56];
        snprintf(buf, sizeof(buf), "!P%cq:ver=1.1,phase=2,i=%d,c=%02x%02x%02x,bpm=%u\r",
                 p.unit, (int)g_effect, g_contractColor.r, g_contractColor.g, g_contractColor.b,
                 (unsigned)g_clock.bpm);
        serialPort->print(buf);
      }
      break;

    default:
      break;
  }
}

// ---- loop hooks -------------------------------------------------------------
// Called every loop pass (spec §8 P): crisp pulse-deadline expiry.
inline void contractPulseTick() {
  if (g_pulseActive && millis() >= g_pulseDeadline) {
    g_pulseActive = false;
    g_lastEffect = CE_NONE;                            // force the base look to re-init on restore
  }
}

// Called inside the 25 ms loop gate. Returns true when the contract owns the frame
// (so the native runPattern path is skipped). Drives beat-clock, score, envelope,
// then renders. Returns false when idle (native autonomy runs).
inline bool contractLoopTick() {
  if (!g_contractArmed) return false;
  uint32_t now = millis();

  // deadline: a finite-duration look reverts to idle swipe when it elapses
  if (g_animDeadline && now >= g_animDeadline) {
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
      g_effect = e.effect;
      if (e.effect == CE_NATIVE && e.nativeCode >= 0) g_nativeCode = e.nativeCode;  // scored native section -> authored code, not stale g_nativeCode
      g_contractColor = _scaled(e.color);
      g_speed = e.speed;
      g_beatMod = e.beatMod;
      g_activeAccentMode = e.accentMode;
      g_lastEffect = CE_NONE;
    }
  }

  // beat accent envelope -> volatile brightness pump (never EEPROM).
  // Level math lives in contract_core's envBright() — the SHARED envelope all three
  // boards render through (b= is the ceiling, m= is the dip depth). Do not reintroduce
  // a board-local floor here: it desyncs the PSIs from the Logics/HPs on every cue.
  // am=0 (an explicit "calm" section) means NO pump — without that guard the accent
  // is always 0 and the board would sit parked at the dip floor (parity w/ RSeries/Flthy).
  uint8_t effBright = g_bright;
  if (g_clock.running && g_beatMod && g_activeAccentMode) {
    BeatPos bp = beatPosAt(g_clock, now);
    uint8_t env = beatAccentAmount(g_activeAccentMode, bp, g_beatMod, 0.0f);
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

// Phase-1 boot hook (nothing yet; symmetry with the RSeries fork).
inline void contractSetup() { }
