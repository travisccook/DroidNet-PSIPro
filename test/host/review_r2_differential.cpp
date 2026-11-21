// test/host/review_r2_differential.cpp — R2 REVIEW, 2026-11-21: the two flash recoveries that claim
// "no change in behaviour", held to R1's code (0e87a91, copied below as it was) over many inputs.
// This test PASSES: it is the evidence for the claims, not a finding.
//
//   400c51a  a scored entry's m= and am= copied as parsed: R1 wrote `pr.hasBeatMod ? pr.beatMod : 0`
//            and `pr.hasAm ? pr.accentMode : 0`. Checked at the parser (the flag and the value, for
//            every line) and end to end (the entry the firmware stores for a scored A, field by
//            field, against the entry R1's code builds from the same parse).
//   1011103  _measure() in a loop of three over the frame's bytes: R1 scaled r, g and b written out.
//            Checked exhaustively for one LED (every colour channel at every scale, in each of the
//            three places), and on random frames of 1 to 48 LEDs; with the products also taken in
//            16 bits unsigned, as an AVR's int does (255 * 256 fits).
//
// The firmware (src/main.cpp, verbatim) on the host mocks, as the timeline scenarios build it.
#include "native_mocks/Arduino.h"
#include "native_mocks/EEPROM.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <string>
#include "../../src/main.cpp"

static long g_checks = 0, g_fail = 0;
#define EXPECT(c, ...) do { g_checks++; if (!(c)) { if (g_fail++ < 20) { printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)
static uint32_t gR = 0x2545F491u;
static uint32_t rnd() { gR ^= gR << 13; gR ^= gR >> 17; gR ^= gR << 5; return gR; }

// ---- R1's _measure(), verbatim (0e87a91, src/contract/ContractPSI.h:385) ----
static void r1Measure(const CRGB* l, uint8_t n, uint8_t scale, uint8_t& lev, uint8_t& lum, uint8_t& pk) {
  uint16_t sum = 0, sumY = 0;
  uint8_t peak = 0;
  const uint16_t k = (uint16_t)scale + 1u;
  for (uint8_t i = 0; i < n; i++) {
    const uint8_t r = (uint8_t)((l[i].r * k) >> 8), g = (uint8_t)((l[i].g * k) >> 8), b = (uint8_t)((l[i].b * k) >> 8);
    uint8_t m = r;
    if (g > m) m = g;
    if (b > m) m = b;
    if (m > peak) peak = m;
    sum += m;
    sumY += contractLuminance(r, g, b);
  }
  lev = (uint8_t)(sum / n); lum = (uint8_t)(sumY / n); pk = peak;
}
// The same with every product in 16 bits unsigned (an AVR's unsigned int).
static uint8_t s16(uint8_t c, uint8_t scale) { return (uint8_t)((uint16_t)((uint16_t)c * (uint16_t)(scale + 1u)) >> 8); }
static void r1Measure16(const CRGB* l, uint8_t n, uint8_t scale, uint8_t& lev, uint8_t& lum, uint8_t& pk) {
  uint16_t sum = 0, sumY = 0; uint8_t peak = 0;
  for (uint8_t i = 0; i < n; i++) {
    const uint8_t r = s16(l[i].r, scale), g = s16(l[i].g, scale), b = s16(l[i].b, scale);
    uint8_t m = r; if (g > m) m = g; if (b > m) m = b;
    if (m > peak) peak = m;
    sum = (uint16_t)(sum + m); sumY = (uint16_t)(sumY + contractLuminance(r, g, b));
  }
  lev = (uint8_t)(sum / n); lum = (uint8_t)(sumY / n); pk = peak;
}
static bool same(const CRGB* l, uint8_t n, uint8_t scale) {
  uint8_t a, b, c, a16, b16, c16;
  r1Measure(l, n, scale, a, b, c);
  r1Measure16(l, n, scale, a16, b16, c16);
  _measure(l, n, scale);                               // R2's, the firmware's own
  return g_mLevel == a && g_mLum == b && g_mPeak == c && a == a16 && b == b16 && c == c16;
}
static void test_measure() {
  static_assert(sizeof(CRGB) == 3 && offsetof(CRGB, r) == 0 && offsetof(CRGB, g) == 1 && offsetof(CRGB, b) == 2,
                "the loop of three reads a CRGB as its bytes r, g, b");
  // one LED: every channel value at every scale, in each place, the others at 0 and at 255
  for (int s = 0; s < 256; s++) for (int v = 0; v < 256; v++) for (int place = 0; place < 3; place++)
    for (int other : { 0, 255, 128 }) {
      CRGB x((uint8_t)other, (uint8_t)other, (uint8_t)other);
      (place == 0 ? x.r : place == 1 ? x.g : x.b) = (uint8_t)v;
      EXPECT(same(&x, 1, (uint8_t)s), "one LED %02x%02x%02x at scale %d", x.r, x.g, x.b, s);
    }
  // random frames, every length, random scales; and the frames the looks draw (one colour, dark)
  static CRGB f[NUM_LEDS];
  for (long it = 0; it < 400000; it++) {
    const uint8_t n = (uint8_t)(1 + rnd() % NUM_LEDS), scale = (uint8_t)rnd();
    const uint32_t mode = rnd() % 4, one = rnd();
    for (int i = 0; i < n; i++) {
      const uint32_t w = mode == 0 ? rnd() : mode == 1 ? one : mode == 2 ? (rnd() & 1 ? one : 0) : (rnd() & 0x00ff0000u);
      f[i] = CRGB((uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w);
    }
    EXPECT(same(f, n, scale), "random frame %ld (n %u, scale %u)", it, n, scale);
  }
  for (int i = 0; i < NUM_LEDS; i++) f[i] = CRGB(255, 255, 255);
  for (int s = 0; s < 256; s++) EXPECT(same(f, NUM_LEDS, (uint8_t)s), "48 white at scale %d", s);
}

// ---- m= and am= ----
static std::string randomParams() {
  static const char* K[] = { "m", "am", "m", "am", "i", "c", "s", "b", "at", "an", "ap", "ae", "ac", "ad", "mm", "a", "", "M", "AM", "m ", "am " };
  static const char* V[] = { "", "0", "1", "2", "3", "4", "5", "9", "200", "255", "256", "300", "65535", "99999999999", "-1", "+5", "12x",
                             "x", " 7", "=", "solid", "vu", "meter", "flash", "ff0000", "1e3", "0x10", "4294967296" };
  std::string s;
  const int keys = 1 + (int)(rnd() % 7);
  for (int k = 0; k < keys; k++) {
    if (k) s += rnd() % 9 ? "," : ",,";
    s += K[rnd() % (sizeof K / sizeof K[0])];
    if (rnd() % 11) { s += "="; s += V[rnd() % (sizeof V / sizeof V[0])]; }
  }
  return s;
}
// R1's scored entry, built from a parse exactly as R1's _applyAnimate built it (0e87a91:679-686).
static ScoreEntry r1Entry(const ContractParams& pr) {
  ScoreEntry e;
  e.atBeat = pr.atBeat; e.effect = _lookHere(pr.effect, false); e.nativeCode = pr.nativeCode;
  e.color = pr.hasColor ? pr.color : g_u.color; e.speed = pr.hasSpeed ? pr.speed : 128;
  e.bright = pr.hasBright ? pr.bright : 255;
  e.beatMod = pr.hasBeatMod ? pr.beatMod : 0;
  e.accentMode = pr.hasAm ? pr.accentMode : 0;
  e.accentEvery = accentEveryFrom(pr);
  if (pr.hasAccentFx) {
    e.accentFx = pr.accentFx; e.accentColor = pr.hasAccentColor ? pr.accentColor : e.color;
    e.accentDur10 = pr.hasAccentDur ? (uint8_t)(pr.accentDurMs / 10u) : 18;
  }
  return e;
}
static bool sameEntry(const ScoreEntry& a, const ScoreEntry& b) {
  return a.atBeat == b.atBeat && a.effect == b.effect && a.nativeCode == b.nativeCode && a.color.r == b.color.r &&
         a.color.g == b.color.g && a.color.b == b.color.b && a.speed == b.speed && a.bright == b.bright &&
         a.beatMod == b.beatMod && a.accentMode == b.accentMode && a.accentEvery == b.accentEvery && a.accentFx == b.accentFx &&
         (a.accentFx == CE_NONE || (a.accentColor.r == b.accentColor.r && a.accentColor.g == b.accentColor.g &&
                                    a.accentColor.b == b.accentColor.b && a.accentDur10 == b.accentDur10));
}
static void test_m_and_am() {
  long stored = 0;
  for (long it = 0; it < 300000; it++) {
    const std::string line = "P*A:" + randomParams() + (rnd() % 4 ? ",i=solid,at=" + std::to_string((int)(rnd() % 40) - 4) : "");
    ParsedContract p;
    if (!contractParse(line.c_str(), p)) continue;
    const ContractParams& pr = p.params;
    // the parser: m= and am= are 0 whenever their flags are not set (what 400c51a rests on)
    EXPECT((pr.hasBeatMod ? pr.beatMod : 0) == pr.beatMod, "m= %u without its flag: %s", pr.beatMod, line.c_str());
    EXPECT((pr.hasAm ? pr.accentMode : 0) == pr.accentMode, "am= %u without its flag: %s", pr.accentMode, line.c_str());
    // end to end: the entry the firmware stores is R1's
    if (!pr.hasAt || !pr.hasEffect || (pr.effect == CE_NATIVE && !nativeAllowedPsi(pr.nativeCode))) continue;
    g_u.scoreLen = 0; g_u.loadOk = 0; g_u.overflow = false;
    applyContract(p, contractCrc16(line.c_str()), 1000);
    if (g_u.scoreLen != 1) continue;
    stored++;
    EXPECT(sameEntry(g_score[0], r1Entry(pr)), "the stored entry is not R1's: %s", line.c_str());
  }
  EXPECT(stored > 10000, "too few scored entries reached the store (%ld)", stored);
  printf("    m= and am=: %ld scored entries stored, each R1's\n", stored);
}

int main() {
  g_avr_next = 1; EEPROM.mem[alwaysOnAddress] = 0; EEPROM.mem[externalPOTAddress] = 1; EEPROM.mem[internalBrightnessAddress] = 20;
  setup();
  test_measure();
  test_m_and_am();
  printf("%s  %ld/%ld checks: 400c51a and 1011103 behave as R1 (0e87a91) on every input tried\n", g_fail ? "FAIL" : "OK", g_checks - g_fail, g_checks);
  return g_fail ? 1 : 0;
}
