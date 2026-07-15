#include "native_mocks/Arduino.h"
#include "native_mocks/FastLED.h"
#include <assert.h>
#include <stdio.h>

// Name-hiding regression guard (see test/host/run.sh stage [7/7]'s own comment for the full
// story): FastLED declares RGB as an ENUMERATOR of EOrder, which silently hides any `struct
// RGB` sharing the name — the real cross-compile caught exactly this once, when the shared
// contract core still defined `struct RGB` and every plain use of the type failed to build.
// This mock's own EOrder must keep RGB as a plain integral enumerator; if a `struct RGB` (or
// anything else) ever shadowed it again, `(int)RGB` would stop compiling here first, on the
// host, for free.
static_assert((int)RGB == 0012, "EOrder::RGB must remain a valid enumerator, not a shadowed type");

static int g_hookCalls = 0;
static uint8_t g_hookScale = 0;
static void hook(const CRGB*, int, uint8_t s) { g_hookCalls++; g_hookScale = s; }

int main() {
  // Color constants.
  CRGB green = CRGB::Green;
  assert(green.r == 0x00 && green.g == 0x80 && green.b == 0x00);
  // u32 assignment.
  CRGB c; c = 0xC8AA00UL;
  assert(c.r == 0xC8 && c.g == 0xAA && c.b == 0x00);
  // nscale8_video KATs: zero->zero; nonzero channel gains +1 when scale != 0.
  CRGB d(200, 0, 1); d %= 100;
  assert(d.r == (uint8_t)(((200 * 100) >> 8) + 1) && d.g == 0 && d.b == 1);
  CRGB z(10, 20, 30); z %= 0;
  assert(z.r == 0 && z.g == 0 && z.b == 0);
  // qmul8 saturates.
  CRGB m(100, 2, 255); m *= 5;
  assert(m.r == 255 && m.g == 10 && m.b == 255);
  // show plumbing: per-call scale, and no-arg show uses stored scale.
  static CRGB buf[4];
  g_frame_hook = hook;
  FastLED.addLeds<WS2812, 4, GRB>(buf, 4);
  FastLED.setBrightness(20);
  FastLED.show(77);
  assert(g_hookCalls == 1 && g_hookScale == 77);
  FastLED.show();
  assert(g_hookCalls == 2 && g_hookScale == 20);
  // clear zeroes but does not latch.
  buf[0] = CRGB(1, 2, 3);
  FastLED.clear();
  assert(buf[0].r == 0 && g_hookCalls == 2);
  puts("test_fastled_mock: OK");
  return 0;
}
