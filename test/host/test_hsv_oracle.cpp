// Part of the DroidNet PSI Pro fork test harness. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// HSV ORACLE for the clean-room WS2812 driver (include/FastLED.h).
//
// The firmware's only CHSV -> CRGB call site is CE_RAINBOW (ContractPSI.h:216,
// CHSV(hue, 255, 255)). No golden exercises it (the contract layer is idle in every
// capture, and the mock's CHSV path is a different, non-FastLED wheel by design). So
// the driver's rainbow is gated ONLY here: it must reproduce FastLED 3.5.0's
// hsv2rgb_rainbow byte-for-byte, or CE_RAINBOW's hue mapping — and the Studio
// visualiser's JS parity vectors, which assume FastLED's rainbow — would drift.
//
// The reference below is FastLED 3.5.0 hsv2rgb_rainbow in its DEFAULT config
// (Y1=1, Y2=0, G2=0, Gscale=0 — hsv2rgb.cpp:288-297), FASTLED_SCALE8_FIXED == 1,
// transcribed from hsv2rgb.cpp:278-497. That transcription was verified byte-identical
// to the ACTUAL vendored FastLED function over all 16,777,216 (h,s,v) triples during
// development; this file is the permanent, dependency-free regression gate.
//
// Exhaustive: the whole 256*256*256 cube, plus an explicit sweep of the CE_RAINBOW
// call site (s = v = 255, every hue).

#include "../../include/FastLED.h"   // the shipped driver, host path (__AVR__ undefined)
#include <cstdio>
#include <cstdint>

namespace fl {
// scale8.h:24 / :101 (C paths, FASTLED_SCALE8_FIXED == 1)
static inline uint8_t scale8(uint8_t i, uint8_t s) {
  return (uint8_t)((((uint16_t)i) * (1 + (uint16_t)s)) >> 8);
}
static inline uint8_t scale8_video(uint8_t i, uint8_t s) {
  return (uint8_t)((((int)i * (int)s) >> 8) + ((i && s) ? 1 : 0));
}
// hsv2rgb.cpp:278 hsv2rgb_rainbow, default config, verbatim structure.
static void hsv2rgb_rainbow(uint8_t hue, uint8_t sat, uint8_t val,
                            uint8_t &R, uint8_t &G, uint8_t &B) {
  uint8_t offset8 = (uint8_t)((hue & 0x1F) << 3);
  uint8_t third = scale8(offset8, (256 / 3));   // max 85
  uint8_t r, g, b;
  if (!(hue & 0x80)) {
    if (!(hue & 0x40)) {
      if (!(hue & 0x20)) { r = 255 - third; g = third; b = 0; }               // R->O
      else               { r = 171; g = (uint8_t)(85 + third); b = 0; }       // O->Y (Y1)
    } else {
      if (!(hue & 0x20)) { uint8_t tt = scale8(offset8, ((256 * 2) / 3));      // Y->G (Y1)
                           r = (uint8_t)(171 - tt); g = (uint8_t)(170 + third); b = 0; }
      else               { r = 0; g = 255 - third; b = third; }               // G->A
    }
  } else {
    if (!(hue & 0x40)) {
      if (!(hue & 0x20)) { uint8_t tt = scale8(offset8, ((256 * 2) / 3));      // A->B
                           r = 0; g = (uint8_t)(171 - tt); b = (uint8_t)(85 + tt); }
      else               { r = third; g = 0; b = 255 - third; }               // B->P
    } else {
      if (!(hue & 0x20)) { r = (uint8_t)(85 + third); g = 0; b = (uint8_t)(171 - third); }  // P->K
      else               { r = (uint8_t)(170 + third); g = 0; b = (uint8_t)(85 - third); }  // K->R
    }
  }
  // G2 == 0 and Gscale == 0: green trims skipped.
  if (sat != 255) {
    if (sat == 0) { r = g = b = 255; }
    else {
      uint8_t desat = scale8_video((uint8_t)(255 - sat), (uint8_t)(255 - sat));
      uint8_t satscale = (uint8_t)(255 - desat);
      r = scale8(r, satscale); g = scale8(g, satscale); b = scale8(b, satscale);
      r = (uint8_t)(r + desat); g = (uint8_t)(g + desat); b = (uint8_t)(b + desat);
    }
  }
  if (val != 255) {
    val = scale8_video(val, val);
    if (val == 0) { r = g = b = 0; }
    else { r = scale8(r, val); g = scale8(g, val); b = scale8(b, val); }
  }
  R = r; G = g; B = b;
}
}  // namespace fl

int main() {
  long cube_diffs = 0;
  long first_bad = -1;
  for (int h = 0; h < 256; ++h) {
    for (int s = 0; s < 256; ++s) {
      for (int v = 0; v < 256; ++v) {
        CHSV in((uint8_t)h, (uint8_t)s, (uint8_t)v);
        CRGB d; hsv2rgb_rainbow(in, d);                 // driver (global)
        uint8_t R, G, B; fl::hsv2rgb_rainbow((uint8_t)h, (uint8_t)s, (uint8_t)v, R, G, B);
        if (d.r != R || d.g != G || d.b != B) {
          if (first_bad < 0) first_bad = ((long)h << 16) | (s << 8) | v;
          ++cube_diffs;
        }
      }
    }
  }

  long site_diffs = 0;
  for (int h = 0; h < 256; ++h) {
    CHSV in((uint8_t)h, 255, 255);
    CRGB d; hsv2rgb_rainbow(in, d);
    uint8_t R, G, B; fl::hsv2rgb_rainbow((uint8_t)h, 255, 255, R, G, B);
    if (d.r != R || d.g != G || d.b != B) ++site_diffs;
  }

  printf("hsv-oracle: driver CHSV->CRGB vs FastLED 3.5.0 hsv2rgb_rainbow\n");
  printf("  full cube 256^3 = 16,777,216 triples : %ld diffs\n", cube_diffs);
  printf("  CE_RAINBOW call site (s=v=255, all h) : %ld diffs\n", site_diffs);
  if (cube_diffs || site_diffs) {
    printf("FAIL: CE_RAINBOW would drift from FastLED's rainbow (first bad h,s,v packed = %ld).\n",
           first_bad);
    return 1;
  }
  printf("OK  CE_RAINBOW is byte-identical to FastLED 3.5.0 over the entire HSV domain\n");
  return 0;
}
