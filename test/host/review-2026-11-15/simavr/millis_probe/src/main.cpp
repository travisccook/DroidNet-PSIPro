// millis-rate probe: the fork's clean-room FastLED.h driver, driven by the same 26 ms millis gate
// main.cpp's loop() uses, with a configurable "compute" delay ahead of show(). Reports millis()
// elapsed vs true elapsed (Timer1, immune to the masked window) per configuration.
#include <Arduino.h>
#include <FastLED.h>
#ifdef __AVR_ATmega32U4__
#define OUT Serial1
#else
#define OUT Serial
#endif
CRGB leds[48];
volatile uint16_t t1ovf = 0;
ISR(TIMER1_OVF_vect) { t1ovf++; }
static uint32_t trueTicks() {            // 4 us per tick
  uint8_t s = SREG; cli();
  uint16_t t = TCNT1; uint16_t o = t1ovf;
  if ((TIFR1 & _BV(TOV1)) && t < 32768) o++;
  SREG = s;
  return ((uint32_t)o << 16) | t;
}
static const uint16_t pre[] = {0, 150, 300, 450, 520, 600, 700, 850, 1000, 1200, 1500, 1700, 2000};
static uint8_t cfg = 0;
static uint32_t prevMs = 0, m0 = 0, t0 = 0;
static uint16_t frames = 0;
static uint8_t shows = 1;
void setup() {
  OUT.begin(9600);
  TCCR1A = 0; TCCR1B = _BV(CS11) | _BV(CS10); TIMSK1 = _BV(TOIE1);
  FastLED.addLeds<WS2812, 4, GRB>(leds, 48);
  fill_solid(leds, 48, CRGB(10, 20, 30));
  OUT.println(F("probe start"));
  OUT.flush();
  m0 = millis(); t0 = trueTicks(); prevMs = m0;
}
void loop() {
  uint32_t now = millis();
  if (now - prevMs > 25) {
    prevMs = now;
    uint16_t d = pre[cfg];
    while (d >= 1000) { delayMicroseconds(1000); d -= 1000; }
    if (d) delayMicroseconds(d);
    for (uint8_t k = 0; k < shows; k++) FastLED.show(20);
    if (++frames == 300) {
      uint32_t m1 = millis(), t1 = trueTicks();
      uint32_t dm = m1 - m0; uint32_t dt_us = (t1 - t0) * 4UL;
      OUT.print(F("shows=")); OUT.print(shows);
      OUT.print(F(" pre_us=")); OUT.print(pre[cfg]);
      OUT.print(F(" millis_ms=")); OUT.print(dm);
      OUT.print(F(" true_us=")); OUT.print(dt_us);
      OUT.print(F(" err_ppm=")); OUT.println((long)(((float)dm * 1000.0f - (float)dt_us) / (float)dt_us * 1e6f));
      OUT.flush();
      frames = 0;
      if (++cfg >= sizeof(pre) / sizeof(pre[0])) { cfg = 0; if (++shows > 2) { OUT.println(F("probe done")); OUT.flush(); for (;;) {} } }
      m0 = millis(); t0 = trueTicks(); prevMs = m0;
    }
  }
}
