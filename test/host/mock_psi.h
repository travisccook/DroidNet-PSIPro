// Minimal host mock of the MaxPSI board API surface that ContractPSI.h uses, to
// TYPE-CHECK the firmware layer on the host (NOT a behavioral sim). Every signature
// mirrors src/main.cpp + include/config.h as verified 2026-07-12:
//   allON(CRGB,bool,unsigned long)                 main.cpp:461
//   allOFF(bool,unsigned long)                     main.cpp:480
//   scanCol(unsigned long,int,CRGB,bool)           main.cpp:727
//   DiscoBall(unsigned long,int,int,CRGB,unsigned long)  main.cpp:1759
//   VUMeter(unsigned long,uint8_t,unsigned long)   main.cpp:1677
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

// FastLED facade (only .show(brightness) is called by the fork layer)
struct _FastLED {
  void show(uint8_t = 0) {}
  void clear() {}
} FastLED;

// board globals (declared in main.cpp before the ContractPSI.h include point)
static CRGB    leds[NUM_LEDS];
static int     level[10] = {0};
static bool    useTempInternalBrightness = false;
static uint8_t tempGlobalBrightnessValue = 20;
static bool    firstTime = false;

struct _Stream { void print(const char*) {} };
static _Stream  _sp;
static _Stream* serialPort = &_sp;

// render primitives + parser targets (exact signatures from src/main.cpp)
inline void allON(CRGB, bool, unsigned long = 0) {}
inline void allOFF(bool, unsigned long = 0) {}
inline void scanCol(unsigned long, int, CRGB, bool) {}
inline void DiscoBall(unsigned long, int, int, CRGB, unsigned long) {}
inline void VUMeter(unsigned long, uint8_t, unsigned long) {}
inline void runPattern(int) {}
inline uint8_t brightness() { return useTempInternalBrightness ? tempGlobalBrightnessValue : 20; }
