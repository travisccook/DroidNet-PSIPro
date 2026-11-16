// CPU cost of the contract_core.h per-frame math and of one show() on the ATmega32U4, in simavr.
#include <Arduino.h>
#include <avr/sleep.h>
#include <FastLED.h>
#include "contract_core.h"
static inline uint16_t t1() { return TCNT1; }   // 0.5 us/tick at prescaler 8
volatile uint8_t sink; volatile uint32_t vnow = 123456; volatile uint8_t vspeed = 128;
CRGB leds[48];
static void rep(const __FlashStringHelper* n, uint16_t ticks) { Serial1.print(n); Serial1.print(F(" us=")); Serial1.println(ticks / 2); Serial1.flush(); }
void setup() {
  Serial1.begin(9600);
  TCCR1A = 0; TCCR1B = _BV(CS11);
  BeatClock bc; bc.running = true; bc.bpm = 120; bc.anchorMs = 1000; bc.bpb = 4; bc.beatOffset = 0;
  uint16_t a, b;
  a = t1(); BeatPos bp = beatPosAt(bc, vnow); b = t1(); sink = bp.barPos; rep(F("beatPosAt"), b - a);
  a = t1(); uint8_t amt = beatAccentAmount(2, bp, 200, 0.5f, bc); b = t1(); sink = amt; rep(F("beatAccentAmount"), b - a);
  a = t1(); uint8_t e = envBright(200, 200, amt); b = t1(); sink = e; rep(F("envBright"), b - a);
  a = t1(); for (int i = 0; i < 48; i++) { uint8_t tb = fxTwinkleBright(i, vnow, vspeed); leds[i] = CRGB((uint16_t)255 * tb / 255, (uint16_t)128 * tb / 255, (uint16_t)64 * tb / 255); } b = t1(); sink = leds[3].r; rep(F("twinkle48"), b - a);
  a = t1(); int head = fxHead(vnow, vspeed, 10); for (int p = 0; p < 10; p++) sink = fxCometBright(p, head, 10); b = t1(); rep(F("comet10"), b - a);
  a = t1(); for (int p = 0; p < 10; p++) sink = fxWipeLit(p, vnow, vspeed, 10); b = t1(); rep(F("wipe10"), b - a);
  a = t1(); for (int p = 0; p < 10; p++) sink = fxChaseLit(p, vnow, vspeed); b = t1(); rep(F("chase10"), b - a);
  a = t1(); for (int p = 0; p < 10; p++) { ContractRGB c = fxHsv2rgb(fxGradientHue(p, 10, 0, vnow, vspeed), 255, 255); sink = c.r; } b = t1(); rep(F("gradient10"), b - a);
  FastLED.addLeds<WS2812, 4, GRB>(leds, 48);
  a = t1(); FastLED.show(200); b = t1(); rep(F("show48"), b - a);
  Serial1.println(F("bench done")); Serial1.flush();
  cli(); set_sleep_mode(SLEEP_MODE_PWR_DOWN); sleep_enable(); sleep_cpu();   // asleep with interrupts off: simavr exits
}
void loop() {}
