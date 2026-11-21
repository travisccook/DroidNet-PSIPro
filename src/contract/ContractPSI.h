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
//   * The gate (S9, S11): every frame the contract draws is measured by brightest channel and by
//     luminance and passed through the core's field gate in FastLED.show() (contractFrameScale):
//     a frame the gate holds is not sent, and the panel keeps what it shows. A frame of another
//     look or colour waits for the core's look gate, asked before it is drawn (contractLoopTick)
//     and taken by the frame that is sent, by its brightest LED; a section waits its 170 ms
//     (contractSectionMayApply). Native looks (S10) are never changed: the gate is told what
//     they put on the LEDs.
//   * The accent overlay is ONE primitive with two triggers: verb P (live) and the board's own
//     per-beat score edge (a scored A's ae=/ac=/ad=). Same code path, same look.
//   * Verb L feeds the contract's own meter look (A:i=meter) and nothing else, honouring the VU
//     inversion (VUMeter in main.cpp: level[c] > i => pixel OFF, so high energy => LOW level).
//     A native VU mode (the owner's, or a show's native:21 / native:92) runs as stock (S10).
//   * v1.4's vu (N10 to N12) is that meter with the beat pump moving its level instead of its
//     brightness: contractLoopTick() offers the pump's level to the core's level hold, and the
//     meter draws the level the hold gives. Not pumped, vu is the meter.
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
// S10: a mode that is NOT on the allow-list is on the LEDs (someone's command started it), and
// the contract's next drawn frame takes them from it.
static bool    g_nativeUnknown = false;
static uint8_t g_nativeLevel = 0;                  // the level the gate was told a native:<n> look runs at
// The darkest frame Leia or Knight Rider has put on the LEDs, measured as the gate measures
// (contractFrameScale), and not yet told to the gate.
static bool    g_seenNative  = false;
static uint8_t g_seenLevel   = 0, g_seenLum = 0;
// An edge of a native look the gate has still to be told: 1 it starts (or has the panel back),
// 2 a show moved its brightness. Told when its light is on the LEDs (a start: at the look's first
// lit frame; a move: at its next frame): a look that was given the panel and never drew, or never
// lit, must not leave the gate believing in its light.
static uint8_t g_edgeKind = 0;
static int     g_edgeCode = 0;

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
// The field level a native look runs at, as the gate is told it: none for mode 0 (panel off), the
// brightness the look draws at otherwise.
static inline uint8_t _nativeField(int code, uint8_t bri) { return code == 0 ? (uint8_t)0 : bri; }
// S10, which native modes report their frames to the gate (never held, only told): the ones on
// the allow-list whose whole field never swings fast, which is all of them but two. Leia (6) and
// Knight Rider (15) move a lit area under a quarter of the field several times a second: to the
// gate's rule (a step is a tenth of the ceiling) each move would be an onset, and the contract
// could never light the panel after one. Their frames are not reported; the gate knows their
// edges. The scenario s10_native_looks_are_classified_by_their_stock_field_swing measures every
// listed mode by the gate's own rule and keeps this split honest.
static PSI_NOINLINE bool _nativeReports(int code) { return code != 6 && code != 15 && nativeAllowedPsi(code); }
// S10, the gate's TELL at an edge of a native look, owed until its light is on the LEDs
// (contractFrameScale): it starts (kind 1), or a show moved its brightness (kind 2).
static PSI_NOINLINE void _nativeEdge(uint8_t kind, int code) {
  if (g_edgeKind != 1) g_edgeKind = kind;              // a start that is still owed stays a start
  g_edgeCode = code;
}
static PSI_NOINLINE void _takePanel() {
  if (g_owned) return;
  // S10: the contract's own drawing is about to take the LEDs from the board's own look. If that
  // is a mode off the allow-list, contractLoopTick() tells the gate at the first frame it draws.
  g_nativeUnknown = !nativeAllowedPsi(lastPSIeventCode);
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
// lets a native look start (S10: a native look runs as stock, but its START is a look change and
// an onset). A native command from the owner calls it at once (_yieldNow).
static PSI_NOINLINE void _givePanelBack() {
  g_nativeUnknown = false;
  if (!g_owned) return;
  useTempInternalBrightness = g_ownerTempOn;
  tempGlobalBrightnessValue = g_ownerTempVal;
  if (g_tookLook) {
    lastPSIeventCode = defaultPattern;
    patternRunning = false;
    timingReceived = false;
  } else {
    // The mode that was running gets the panel back as it is (a pulse over it, L3; the owner's
    // command, L6). What the contract drew is still in the frame buffer and on the LEDs, and not
    // every mode replaces it: short circuit (4) fades whatever the buffer holds, and a program
    // that has ended (the lightsaber battle under alwaysOn) draws nothing at all. So the buffer
    // is cleared, and a mode that has ended plays again; the panel would otherwise keep the
    // contract's light with nothing left that owns it.
    FastLED.clear();
    vmReplayIfEnded();
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
  // S10: light the contract did not ask for is on the LEDs from here. The gate is told (it cannot
  // be asked), so that what the contract does next keeps its distance.
  _nativeEdge(1, lastPSIeventCode);
}
#ifdef PSI_ENABLE_I2C
static volatile bool g_yieldPending = false;
inline void contractYieldToNative() { g_yieldPending = true; }
static inline void _serviceYield() { if (g_yieldPending) { g_yieldPending = false; _yieldNow(); } }
#else
inline void contractYieldToNative() { _yieldNow(); }
static inline void _serviceYield() { }
#endif
// D and A (main.cpp doDcommand): upstream runs the default pattern for ANY address. true: do that.
// A D addressed to this PSI (0, 4 front, 5 rear) is its owner's: the show ends first (L6). A D for
// another board changes nothing here while the contract has the panel or a show is armed (a logic
// display's 3D on a shared line ended a scored show: review finding 4); with no show it does what
// upstream does.
inline bool contractYieldForD(int address) {
  if (address == 0 || address == (psiJumperOpen() ? 4 : 5)) { contractYieldToNative(); return true; }
  return !g_owned && !g_u.inShow;
}

// Verb L feeds the contract's OWN meter look, and only while the contract is drawing it. The fed
// level is kept here and goes into level[] (main.cpp) when the contract runs VUMeter() for
// A:i=meter; a native VU mode (the owner's 21 or 92, or a show's native:21 / native:92) never
// sees it and runs as stock (S10): its bars are its own random walk, which no line can empty and
// refill. With no L for 2 s the random walk runs under the contract's meter too, so a meter look
// with no level feed still moves.
static bool     g_levelFed = false;
static bool     g_meterMine = false;                   // VUMeter() is running for the contract's meter look
// v1.4 N10: a vu look is pumped this pass (the clock runs, am= and m= are not 0, no overlay): the
// pump is its level feed, and L is not (N12). Set by contractLoopTick() before it draws.
static bool     g_vuPumped = false;
static uint8_t  g_vuShown  = 0;                        // the field level of the last frame the contract sent
static uint32_t g_levelMs  = 0;
inline bool contractHoldsLevels() {
  if (!g_meterMine) return false;
  if (g_vuPumped) return true;
  if (g_levelFed && (uint32_t)(millis() - g_levelMs) >= 2000u) g_levelFed = false;
  return g_levelFed;
}

// ---- S9 and S11: one limit for every path ---------------------------------------------------
// Called by FastLED.show() for EVERY frame, as the last thing before it goes out (the hook is
// PSI_FRAME_GATE, set in main.cpp), and returns the scale to send it at, or -1: do not send it.
//   * A frame the contract DRAWS (its looks, the overlay, X's dark) is measured twice: its field
//     level (the mean over the LEDs of each LED's brightest channel, times the master scale) and
//     its luminance (the same with the core's Rec.709 weights, so a change of colour counts). The
//     core's field gate is ASKED with both: a dark-to-lit swing in either is let through only
//     340 ms after the last one, and until then the frame is HELD: it is not sent, and the panel
//     keeps what it shows. A frame that is sent is TOLD to the gate as it is, and takes the look
//     gate by its brightest LED. The frame is also never brighter than the ceiling. An accent
//     overlay still up when its section gives way to a native:<n> one is the contract's own
//     drawing.
//   * A frame of a native look is never changed (S10: native looks run as stock), whether the
//     owner started the look or a show selected it from the allow-list. The gate governs its
//     EDGES, and is told at each: when the look starts, at what level, and when a show moves
//     its brightness (both at the look's next frame, here: only light that reached the LEDs is
//     told), and when the contract's own drawing takes the LEDs back (contractLoopTick).
//     The level it is told is the brightness the look draws at, and what a mode
//     puts on the field is anything from a twentieth of that (Knight Rider) to all of it, in any
//     colour, lit at once or a little later. This board can measure a native frame, so it does,
//     and TELLS the gate what is on the LEDs (the frame is never held): for every listed mode
//     whose field does not swing fast, each frame; for Leia and Knight Rider, whose every move
//     would count as an onset, only the last one, before the contract's first frame after it.
//     Then a rise the contract makes from there is the onset it is, and a native look's own
//     light keeps the contract's next onset at its distance.
// The measures are of what goes out on the wire: each channel as the LED driver scales it,
// channel * (scale + 1) >> 8 (scale8, include/FastLED.h), then each LED's brightest channel and its
// luminance, then the mean. At a low brightness a level is several percent of the ceiling: a
// measure that rounded another way (the whole frame's sum times scale / 255, as this was at
// first) was a level off the wire now and then, and took a real rise for texture (review
// finding 6). 48 LEDs, three multiplies each; nothing here blocks.
#ifndef PSI_FRAME_GATE_WIRED
#error "FastLED.h was included before main.cpp named PSI_FRAME_GATE: frames would skip the S9 gate"
#endif
static uint8_t g_mLevel, g_mLum, g_mPeak;              // what _measure() found (peak: the brightest LED)
static uint32_t g_drawSig;                             // the look of the frame the contract is drawing (contractLookSig)
static PSI_NOINLINE void _measure(const CRGB* l, uint8_t n, uint8_t scale) {
  uint16_t sum = 0, sumY = 0;                          // 48 * 255 fits
  uint8_t peak = 0;
  const uint16_t k = (uint16_t)scale + 1u;
  // The frame as bytes, r, g, b, LED after LED (CRGB is those three bytes: a static_assert in
  // include/FastLED.h and in the host mock). One scaling in a loop of three is 44 B less flash
  // than three written out; each channel is still scaled, and compared, as before.
  const uint8_t* p = (const uint8_t*)l;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t c[3], m = 0;
    for (uint8_t j = 0; j < 3; j++) {
      c[j] = (uint8_t)((*p++ * k) >> 8);
      if (c[j] > m) m = c[j];
    }
    if (m > peak) peak = m;
    sum += m;
    sumY += contractLuminance(c[0], c[1], c[2]);
  }
  g_mLevel = (uint8_t)(sum / n); g_mLum = (uint8_t)(sumY / n); g_mPeak = peak;
}
int contractFrameScale(const CRGB* l, uint8_t n, uint8_t scale) {
  const bool drawn = g_owned && (g_u.base != CB_NATIVE || g_u.pulseActive);     // the contract drew this frame itself
  if (drawn && scale > g_u.ceil) scale = g_u.ceil;
  int out = scale;
  _measure(l, n, scale);
  const uint32_t now = millis();
#ifdef PSI_ENABLE_I2C
  const uint8_t sreg = SREG;                           // a native frame can be drawn from the TWI
  cli();                                               // interrupt: keep the gate's state whole
#endif
  const uint8_t level = g_mLevel, lum = g_mLum;
  const bool reports = !drawn && _nativeReports(g_owned ? g_u.nativeCode : lastPSIeventCode);
  if (g_seenNative && (drawn || reports)) {            // what Leia or Knight Rider left on the LEDs
    contractFieldTell(g_u, g_seenLevel, g_seenLum, now);
    g_seenNative = false;
  }
  if (!drawn) {                                        // a native look's frame: never changed
    if (!g_owned && !g_u.inShow) g_u.ceil = scale;     // no show: the step follows the owner's level
    if (g_edgeKind == 1 && g_mPeak >= LOOK_MIN_LEVEL) {
      // The look that took the panel can be seen, for the first time: its start, as the gate
      // hears it. That is the first frame with an LED at 4 levels (CA1.6), not the first whose
      // field level is above nothing: the processing sweep starts with two LEDs of 48, a field
      // level of 0 and a colour anyone can see, and a meter at a brightness of 3 has a field level
      // of 1 and shows none. S4 has to run from the frame that can be seen. A mode whose
      // frames are reported has its light told below. Leia and Knight Rider are told once, here
      // (S10), by the two figures of this frame.
      if (!reports) contractNativeStarted(g_u, g_edgeCode, level, lum, now);
      contractLookForced(g_u, contractLookSigNative(g_edgeCode), now);
      g_edgeKind = 0;
    } else if (g_edgeKind == 2) {
      if (!reports) contractNativeLevel(g_u, level, lum, now);
      g_edgeKind = 0;
    }
    // A mode whose frames are reported: the gate hears what is on the LEDs, as it is. (The level
    // a call would tell it, the brightness the mode draws at, is not what most modes put on the
    // field, and a gate that believed it would take the contract's next rise from the real level
    // for less than it is.) Leia and Knight Rider: their darkest frame is kept, for whoever draws
    // next (they go dark between steps, and what lights the field after that is an onset).
    if (reports) contractFieldTell(g_u, level, lum, now);
    else {                                             // the darkest it has been: a rise from there is an onset
      if (!g_seenNative || level < g_seenLevel) g_seenLevel = level;
      if (!g_seenNative || lum < g_seenLum) g_seenLum = lum;
      g_seenNative = true;
    }
  } else {
    g_edgeKind = 0;                                    // a native look that never lit owes the gate nothing
    // A frame the field gate would hold is NOT SENT (CA1.6): the panel keeps what it shows. Sent
    // scaled down, as it was at first, it kept the field's level legal and still lit, dimly,
    // every part of the panel the frame lights: half a meter, back from dark, had lit again.
    // The look gate (S4, S11) was ASKED before this frame was drawn, and is TAKEN here, by the
    // frame that is sent and by its brightest LED (CA1.6): a frame with no LED at 4 levels shows
    // no colour, and the 170 ms run from the first frame that does. (A sparkle's first frame is
    // black, and its first LED lights a step later: taken when the look was selected, the gate
    // let the next look follow 26 ms after that LED.)
    if (contractFieldAsk(g_u, level, lum, now) != 255) out = -1;
    else {
      contractLookGateAt(g_u, g_drawSig, g_mPeak, now);
      contractFieldTell(g_u, level, lum, now);
      // v1.4 N11 for a pumped vu: the level's 340 ms window runs from the frame that SHOWS a
      // rise, not from the pass that took it. A pass that takes a rise can draw nothing (the look
      // gate holds the look's first frames; VUMeter() opens with a black frame) or have its frame
      // held here, and the bars then light a pass or three later: counted from the take, the next
      // beat's rise came 312 ms after the bars lit (the host's property test, at the owner's
      // brightness 20). The window only ever moves later.
      if (g_vuPumped && level > g_vuShown) g_u.levelMs = now;
      g_vuShown = level;
    }
  }
#ifdef PSI_ENABLE_I2C
  SREG = sreg;
#endif
  return out;
}

// ---- the looks this build has ----------------------------------------------------
// PSI_NO_STRIP_LOOKS (set in platformio.ini for every environment: R1 ships with it) builds
// without the six strip and hue looks: with contract v1.3's gate they do not fit the part. A line
// that names one is not dropped and still counts for verb V; it draws the nearest look the build
// has:
//   comet, chase, wipe -> scan       twinkle -> sparkle       gradient, colorcycle -> solid in c=
// As an accent overlay all six become solid: scan and sparkle keep state between frames, which is
// why the core refuses them as accents. The six stay in this file and are run on the host (the
// all-looks build of the timeline scenarios), so that they can come back when there is room.
// Contract v1.4 (core CA2) adds three ids. logic (the logics' look) and projector (the
// holoprojectors') draw sparkle here in every build, as v1.4's table has it; vu is the PSI's own
// and keeps its id (wherever CE_METER is tested, CE_VU is too): it is the meter, and pumped the
// beat moves its level (contractLoopTick()). None of the three is an accent (accentEffectAllowed()).
#ifdef PSI_NO_STRIP_LOOKS
static PSI_NOINLINE ContractEffect _lookHere(ContractEffect e, bool overlay) {
  switch (e) {
    case CE_COMET: case CE_CHASE: case CE_WIPE: return overlay ? CE_SOLID : CE_SCAN;
    case CE_TWINKLE: case CE_LOGIC: case CE_PROJECTOR: return overlay ? CE_SOLID : CE_SPARKLE;
    case CE_GRADIENT: case CE_COLORCYCLE:       return CE_SOLID;
    default:                                    return e;
  }
}
#else
static inline ContractEffect _lookHere(ContractEffect e, bool overlay) {
  return (e == CE_LOGIC || e == CE_PROJECTOR) ? (overlay ? CE_SOLID : CE_SPARKLE) : e;
}
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
    bool lit = fxChaseLit(p, elapsed, g_u.speed, COLUMNS);
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
    case CE_VU:                                                             // v1.4: the meter; pumped, fed by the pump
      // The top rows are a region of the panel like any other, and the gate sees only the whole
      // field (review finding 1): L alternating between half and full blinked the top half ten
      // times a second, and with no level feed the meter's random walk at 20 ms a step did the same.
      //   * Fed by L, or a vu by the pump (v1.4 N10): the level is the core's (contractLevelOffer; contractHoldsDue() in
      //     contractLoopTick(), on a pass that redraws the bars). A higher one is taken at most
      //     once in 340 ms and a fall at once, so no row lights more than three times a second
      //     through L, nor through B, which shares the window (CA1.7).
      //   * Not fed: the walk is the board's own, redrawn every 170 ms at the fastest (the native
      //     meter: every 250 ms) whatever s= asks for, so a row that goes dark and lights again
      //     does it 340 ms after it last lit.
      // main.cpp's VUMeter() counts the rows that are DARK: v=255 -> 0 (full), v=0 -> 6 (empty).
      g_meterMine = true;
      if (contractHoldsLevels()) {
        const uint8_t rows = (uint8_t)(LEDS_PER_COLUMN - ((uint16_t)g_u.level * LEDS_PER_COLUMN) / 255u);
        for (uint8_t c = 0; c < COLUMNS; c++) level[c] = rows;
      } else if (d < STROBE_MIN_STATE_MS) d = STROBE_MIN_STATE_MS;
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
  // S11: re-applying the same look does not restart it. Asked before anything is written: the
  // look this line makes, against the one the unit has.
  const ContractEffect effect = (lp.has & LP_EFFECT) ? _lookHere(lp.effect, false) : g_u.effect;
  // (CA1.7: nor does a change of colour alone. The look is recoloured in place and its timeline
  // carries on; the look gate still times the change of colour.)
  const bool same = contractLookContinues(g_u, effect, (lp.has & LP_EFFECT) ? lp.nativeCode : g_u.nativeCode,
                                          (lp.has & LP_SPEED) ? lp.speed : g_u.speed);
  if (lp.has & LP_COLOR)   g_u.color = lp.color;
  if (lp.has & LP_SPEED)   g_u.speed = lp.speed;
  if (lp.has & LP_BRIGHT)  contractBrightOffer(g_u, lp.bright);   // CA1.7: taken by contractHoldsDue()
  if (lp.has & LP_BEATMOD) g_u.beatMod = lp.beatMod;
  if (lp.has & LP_AM)      g_u.accentMode = lp.accentMode;
  if (lp.has & LP_EVERY)   g_u.accentEvery = lp.every;
  const bool overlay = g_u.pulseActive;                // the line ends an accent that is up
  g_u.pulseActive = false;
  if (lp.has & LP_EFFECT) {
    _holdPanel();
    g_u.effect = effect;
    g_u.nativeCode = lp.nativeCode;
    g_u.base = (lp.effect == CE_NATIVE) ? CB_NATIVE : CB_LOOK;
  }
  if (!same) {                                         // another look: its timeline starts, and what
    g_u.startMs = now;                                 // the field shows next is a change of look
    contractLookRestart(g_u);
  }
  // Its state machines start again on the next tick: for another look, and for the same look when
  // this line cut an accent short. The accent's image is in the frame buffer, and a look that adds
  // to the buffer (sparkle) went on over it: a field of the accent's colour under the look's name.
  if (!same || overlay) g_lastEffect = CE_NONE;
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
    // m= and am= are 0 when the line has none: the parser writes them only with their flags, and
    // ContractParams starts them at 0 (contract_core.h). Copied as they are, with no test of the
    // flag: the same entry, 58 B less flash than `pr.hasBeatMod ? pr.beatMod : 0` and its twin.
    e.beatMod    = pr.beatMod;
    e.accentMode = pr.accentMode;
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
      contractBrightOffer(g_u, pr.hasBright ? pr.bright : (pr.hasLevel ? pr.level : g_u.brightWant));
      break;

    case CV_LEVEL: {                                   // feed the contract's meter look from Studio energy
      if (g_vuPumped) break;                           // v1.4 N12: an L while the pump feeds vu is not shown
      // v= is 0 when the line has none (the parser writes it only with its flag; ContractParams
      // starts it at 0): taken as it is, 14 B less than testing the flag (test_r2fix_recoveries.cpp).
      contractLevelOffer(g_u, pr.level);               // taken where the meter is drawn (contractHoldsDue)
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
      // A section boundary. S4 for every path: a section is applied 170 ms after the unit's last
      // look change at the earliest (a C seek can move the beat between two sections every few
      // milliseconds); one that has to wait is taken by a later tick, if the beat is still in it.
      if (idx >= 0 && idx != g_u.curSection && contractSectionMayApply(g_u, now)) {
        const ScoreEntry& e = g_score[idx];
        _holdPanel();
        if (!contractLookContinues(g_u, e.effect, e.nativeCode, e.speed)) {   // S11, and CA1.7: not for a colour alone
          g_u.startMs = now;
          contractLookRestart(g_u);
          g_lastEffect = CE_NONE;
        }
        g_u.curSection   = (int8_t)idx;
        g_u.sectionStart = e.atBeat;
        g_u.sectionEnd   = (idx + 1 < g_u.scoreLen) ? g_score[idx + 1].atBeat : (e.atBeat + 9999);
        g_u.effect = e.effect; g_u.nativeCode = e.nativeCode; g_u.color = e.color; g_u.speed = e.speed;
        contractBrightOffer(g_u, e.bright); g_u.beatMod = e.beatMod; g_u.accentMode = e.accentMode;
        g_u.accentEvery = e.accentEvery;
        g_u.base = (e.effect == CE_NATIVE) ? CB_NATIVE : CB_LOOK;
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

  g_vuPumped = false;                                  // set again below, on a pass that pumps a vu
  if (!_owns()) {                                      // nothing of ours left to show: the native look runs
    // S10: handing the panel to the board's own look is the contract's edge, and the light that
    // look shows next is counted as the contract's doing. So the hand-over waits until an onset
    // and a change of look are allowed (contractNativeMayStart), lit or dark; until then the panel
    // keeps what it shows. The gate is told once, as the look takes the LEDs.
    if (g_owned) {
      const int code = g_tookLook ? (int)defaultPattern : lastPSIeventCode;
      const uint8_t level = _nativeField(code, _ownerBri());
      if (!contractNativeMayStart(g_u, code, level, now)) return true;
      _givePanelBack();
      _nativeEdge(1, code);
    }
    return false;
  }

  // CA1.7: verb L's level and the brightness fraction (B, b=, a section's) go up at most once in
  // 340 ms, in ONE window, and down at once (contractHoldsDue): a part of a look lights when either
  // goes up. They are taken on a pass whose frame shows them. The meter is redrawn every d ms, so
  // with the meter up that is a pass that redraws it: taken on every pass, a raise was mostly
  // undone by the next lower line before the meter drew it.
  if ((g_u.effect != CE_METER && g_u.effect != CE_VU) || checkDelay()) contractHoldsDue(g_u, now);
  // S6: b= and B are fractions of the owner's ceiling. The pump rides under that level. The
  // overlay has its own fraction (a P's b=, or the look's level for a scored accent), un-pumped.
  const uint8_t lvl = contractLevel(g_u.ceil, g_u.pulseActive ? g_u.pulseBright : g_u.bright);
  uint8_t effBright = lvl;
  if (!g_u.pulseActive && g_clock.running && g_u.beatMod && g_u.accentMode) {
    // Progress through the section in Q16 (0..65536), for am=3. No float anywhere in this layer or
    // in the core: one float expression links 1.3 KB of soft-float library.
    const uint32_t prog = (g_u.curSection >= 0) ? beatSectionProgress16(bp.beatIndex, g_u.sectionStart, g_u.sectionEnd) : 0;
    uint8_t env = beatAccentAmount(g_u.accentMode, bp, g_u.beatMod, prog, g_clock, g_u.accentEvery);
    // v1.4 N10: on a vu look the pump moves the LEVEL, envBright(255, m, amount) (the core's
    // vector group vuLevel), offered to the level hold and taken by contractHoldsDue() on the next
    // pass that redraws the meter (a raise at most once in 340 ms, in the window B and b= share; a
    // fall at once). The brightness stays at the look's level. Every other look: the pump moves
    // the brightness. One envBright() for both (two copies cost 80 B more).
    const bool vu = g_u.effect == CE_VU;
    const uint8_t e = envBright(vu ? 255 : lvl, g_u.beatMod, env);
    if (vu) contractLevelOffer(g_u, e); else effBright = e;
    g_vuPumped = vu;
  } else if (g_u.pulseActive ? g_u.pulseFx == CE_PULSE : (g_u.base == CB_LOOK && g_u.effect == CE_PULSE)) {
    // pulse: the shared breathe (a 2000 ms cycle at s=0 down to 340 ms at s=255, never faster: S8)
    // whenever nothing pumps the look. Under the beat pump the envelope IS the breathe.
    const uint32_t t0 = g_u.pulseActive ? g_u.pulseStartMs : g_u.startMs;
    effBright = (uint8_t)(((uint16_t)lvl * fxPulseBright(now - t0, g_u.speed)) / 255u);
  }
  ContractEffect eff = (g_u.base == CB_DARK) ? CE_OFF : g_u.effect;
  useTempInternalBrightness = true;                    // 3P volatile path only, never EEPROM

  if (g_u.base == CB_NATIVE && !g_u.pulseActive) {
    // S10: a native:<n> look runs as stock. Its frames are not held, measured or reported; the
    // gate governs its edges, and each is one call.
    uint8_t level = _nativeField(g_u.nativeCode, effBright);
    if (eff != g_lastEffect) {
      // It starts. Most modes clear the panel and draw at once, whatever was on it, so the start
      // waits until an onset and a change of look are allowed, the panel keeping what it shows.
      if (!contractNativeMayStart(g_u, g_u.nativeCode, level, now)) return true;
      _nativeEdge(1, g_u.nativeCode);
      firstTime = true;                                // re-init the native state machines
      timingReceived = false;                          // not with a |timing the owner once gave another command
      g_lastEffect = eff;
    } else if (level != g_nativeLevel) {
      // The show moves its brightness (B, b=, the pump). Every native mode draws at brightness().
      // A rise goes only as far as the gate allows now. While the look has not been lit yet (it
      // started at no brightness, or dark), its first light is its start: that asks again.
      if (level > g_nativeLevel) {
        if (g_edgeKind != 1) level = contractFieldAllowed(g_u, level, now);
        else if (!contractNativeMayStart(g_u, g_u.nativeCode, level, now)) level = g_nativeLevel;
      }
      _nativeEdge(2, g_u.nativeCode);
    }
    g_nativeLevel = level;
    if (g_u.nativeCode != 0) effBright = level;
    tempGlobalBrightnessValue = effBright;
    runPattern(g_u.nativeCode);
    return true;
  }
  tempGlobalBrightnessValue = effBright;

  // What the contract draws itself: the overlay (an effect swap on its own timeline), or the look.
  ContractRGB col = g_u.color;
  uint32_t t0 = g_u.startMs;
  if (g_u.pulseActive) {
    eff = (g_u.pulseFx == CE_NONE) ? CE_SOLID : g_u.pulseFx;
    col = g_u.pulseColor;
    t0  = g_u.pulseStartMs;
  }
  // S11, S4 for every path: a frame that would show another look or colour than the field last
  // showed lit comes 170 ms after the field's last change at the earliest, whichever path asked
  // for it (an accent between two looks, a section, a look after a native one). Until then the
  // frame that is up stays up. Black is not a look: a blackout is never held.
  // (Asked here; taken by the frame that is sent, in contractFrameScale().)
  g_drawSig = contractLookSig(eff, col);
  if (eff != CE_OFF && effBright != 0 && !contractLookAllowed(g_u, g_drawSig, now)) return true;
  if (g_nativeUnknown) {
    // S10: the contract draws again, from this frame, where a mode that is not on the allow-list
    // was. It may have been flashing: the field is taken as dark and just lit, and the contract's
    // next onset keeps 340 ms from here. (A mode on the list needs no such care: the gate has
    // heard its frames, or, for Leia and Knight Rider, hears what they left on the LEDs.)
    contractNativeEnded(g_u, false, now);
    g_nativeUnknown = false;
  }
  if (!g_u.pulseActive && eff != g_lastEffect) {       // a new look: re-init the native state machines
    firstTime = true;
    g_lastEffect = eff;
  }
  _renderLook(eff, _scaled(col), t0);
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
