// WS2812 waveform probe: one show() of a known frame; simavr traces PD4 to a VCD.
#include <Arduino.h>
#include <FastLED.h>
CRGB leds[48];
void setup() {
  FastLED.addLeds<WS2812, 4, GRB>(leds, 48);
  for (uint8_t i = 0; i < 48; i++) leds[i] = CRGB((uint8_t)(i * 5), (uint8_t)(255 - i), (uint8_t)(i ^ 0xA5));
  delay(5);
  FastLED.show(255);          // scale 255 is the identity, so the wire bytes are the leds[] bytes (GRB)
  delay(2);
  FastLED.show(255);          // a second frame 2 ms later
  FastLED.show(255);          // and a third with no delay at all: the same-tick double show the VM
                              // programs do (OP_SHOWNOW then OP_SHOW). Its gap is the staging loop.
  Serial1.begin(9600); Serial1.println(F("wave done")); Serial1.flush();
}
void loop() {}
