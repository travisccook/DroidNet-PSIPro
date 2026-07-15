// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Neil Hutchison's PSI Pro firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// Host mock of the MaxPSI board API surface that ContractPSI.h uses. Primarily a
// TYPE-CHECK of the firmware layer, PLUS a thin LATCH MODEL: the mock records every
// FastLED.show() so a test can assert that a renderer actually pushes its frame to
// the strip. That distinction is not cosmetic — on this board leds[] is only a
// staging buffer, and there is NO global FastLED.show() in loop() (src/main.cpp:430+,
// the 25 ms gate calls contractLoopTick()/runPattern() and nothing else). A renderer
// that builds a frame with fill_column() alone therefore latches NOTHING and the panel
// freezes on the last shown frame. The mock models exactly which primitives latch:
//   * allON/allOFF                          -> FastLED.show() when showLED (main.cpp:488 / 509)
//   * vmContractScan/vmContractSparkle/VUMeter/runPattern -> latch internally
//     (include/psi_vm.h vmContractScan/vmContractSparkle; main.cpp VUMeter/runPattern)
//   * fill_column    -> writes leds[] ONLY, never shows (main.cpp:517-524)  <-- the trap
// Every signature mirrors src/main.cpp + include/psi_vm.h + include/config.h as verified
// 2026-07-14 (Task 14 — scanCol()/DiscoBall() are deleted from main.cpp; CE_SCAN/CE_SPARKLE
// now render through the VM shims below, which this mock stubs the same way the natives it
// replaced were stubbed: a bare latch, not a behavioral simulation):
//   allON(CRGB,bool,unsigned long)                 main.cpp:461
//   allOFF(bool,unsigned long)                     main.cpp:480
//   vmContractScan(unsigned long,const CRGB&)      include/psi_vm.h (Task 14)
//   vmContractSparkle(unsigned long,const CRGB&)   include/psi_vm.h (Task 14)
//   VUMeter(unsigned long,uint8_t,unsigned long)   main.cpp:1677
//   fill_column(uint8_t,CRGB,uint8_t=0)            main.cpp:517
//   runPattern(int)                                main.cpp:2116
//   uint8_t brightness()                           main.cpp:2593
//   leds[NUM_LEDS] CRGB / level[10] int            main.cpp:296 / 327
//   useTempInternalBrightness / tempGlobalBrightnessValue / firstTime  main.cpp:300/302/318
//   JUMP_FRONT_REAR jumper via digitalRead()       config.h:101-105 / main.cpp:358
//   NUM_LEDS / LEDS_PER_COLUMN / COLUMNS           config.h:212-214
#pragma once
#include <stdint.h>
#include <cstdio>

typedef uint8_t byte;

static uint32_t _mock_millis = 0;
inline uint32_t millis() { return _mock_millis; }

// board geometry (config.h:212-214)
// config.h:286 / preamble.h:16 — the serial line buffer the contract layer's I2C deferral
// queue is sized from. Kept in step with the firmware by run.sh's CMD_MAX_LENGTH width guard.
#define CMD_MAX_LENGTH 96
#define NUM_LEDS 48
#define LEDS_PER_COLUMN 6
#define COLUMNS 10

// addressing jumper (config.h:102 for ARDUINO_AVR_PRO)
#define JUMP_FRONT_REAR 12
inline int digitalRead(int) { return 1; }   // HIGH default (pulled up) => front

struct CHSV {
  uint8_t h = 0, s = 0, v = 0;
  CHSV() {}
  CHSV(uint8_t hh, uint8_t ss, uint8_t vv) : h(hh), s(ss), v(vv) {}
};
struct CRGB {
  uint8_t r = 0, g = 0, b = 0;
  CRGB() {}
  CRGB(int rr, int gg, int bb) : r((uint8_t)rr), g((uint8_t)gg), b((uint8_t)bb) {}
  CRGB(const CHSV&) {}                        // leds[i] = CHSV(...) conversion
};

// ---- latch recorder (test-only; see header comment) --------------------------
static int     mock_showCount        = 0;   // # of FastLED.show() calls
static uint8_t mock_lastShowBright   = 0;   // brightness argument of the last show()
static int     mock_fillColumnCount  = 0;   // # of fill_column() calls (frame staging)
static CRGB    mock_column[COLUMNS];        // last color staged into each column

// board globals (declared in main.cpp before the ContractPSI.h include point)
static CRGB    leds[NUM_LEDS];
static int     level[10] = {0};
static bool    useTempInternalBrightness = false;
static uint8_t tempGlobalBrightnessValue = 20;
static bool    firstTime = false;

inline uint8_t brightness() { return useTempInternalBrightness ? tempGlobalBrightnessValue : 20; }

// FastLED facade (only .show(brightness)/.clear() are called by the fork layer)
struct _FastLED {
  void show(uint8_t b = 0) { mock_showCount++; mock_lastShowBright = b; }
  void clear() { for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(0, 0, 0); }
} FastLED;

inline void mock_resetLatch() { mock_showCount = 0; mock_fillColumnCount = 0; }

// ---- serial capture (test-only) ---------------------------------------------
// The verb-Q ack is the ONE thing the fork layer writes back to the bus, and it is a
// wire-format contract: Studio parses those exact bytes. This mock used to DISCARD the
// string (`void print(const char*) {}`), so nothing in the suite could see the ack — a
// formatting regression (a dropped zero-pad, an upper-case hex digit, a missing \r) was
// invisible to the host tests. Capture it verbatim so a test can pin the bytes.
static char mock_serialOut[256] = {0};
static int  mock_serialLen      = 0;

struct _Stream {
  void print(const char* s) {
    for (int i = 0; s[i] && mock_serialLen < (int)sizeof(mock_serialOut) - 1; i++)
      mock_serialOut[mock_serialLen++] = s[i];
    mock_serialOut[mock_serialLen] = '\0';
  }
};
static _Stream  _sp;
static _Stream* serialPort = &_sp;

inline void mock_resetSerial() { mock_serialLen = 0; mock_serialOut[0] = '\0'; }

// render primitives + parser targets (exact signatures from src/main.cpp).
// Bodies model ONLY whether the primitive latches the frame (FastLED.show), which is
// what the contract layer depends on; pixel layout (ledMatrix) is not simulated.
inline void allON(CRGB c, bool showLED, unsigned long = 0) {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = c;
  if (showLED) FastLED.show(brightness());              // main.cpp:488
}
inline void allOFF(bool showLED, unsigned long = 0) {
  FastLED.clear();
  if (showLED) FastLED.show();                          // main.cpp:509
}
inline void vmContractScan(unsigned long, const CRGB&) { FastLED.show(brightness()); }     // include/psi_vm.h (Task 14)
inline void vmContractSparkle(unsigned long, const CRGB&) { FastLED.show(brightness()); }  // include/psi_vm.h (Task 14)
inline void VUMeter(unsigned long, uint8_t, unsigned long) { FastLED.show(brightness()); } // main.cpp:1684
inline void runPattern(int) { FastLED.show(brightness()); }                                // native looks latch
// STAGES ONLY — deliberately does NOT show (faithful to main.cpp:517-524).
inline void fill_column(uint8_t column, CRGB color, uint8_t = 0) {
  if (column < COLUMNS) mock_column[column] = color;
  mock_fillColumnCount++;
}
