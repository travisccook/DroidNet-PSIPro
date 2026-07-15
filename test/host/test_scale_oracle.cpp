// Part of the DroidNet PSI Pro fork test harness. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// SCALE-MATH ORACLE for the clean-room WS2812 driver (include/FastLED.h).
//
// The 31 goldens gate the firmware's STAGING semantics, and they compile against
// test/host/native_mocks/FastLED.h — not against the shipped driver. This oracle is
// the bridge: it holds the driver's 8-bit scalers to BOTH references, exhaustively,
// so the mock-gated goldens transfer to the real driver and the real driver matches
// FastLED on hardware:
//   * vs the harness MOCK  -> if equal, every golden the mock certified holds for the
//     driver too (the mock's qmul8 / nscale8_video are the only scalers a golden ever
//     exercises during staging).
//   * vs FastLED 3.5.0     -> if equal, the panel sees exactly what stock FastLED drew.
//
// The FastLED reference formulas below are the C paths (SCALE8_C, QMUL8_C) with
// FASTLED_SCALE8_FIXED == 1 (its shipped default, fastled_config.h:37), transcribed
// from the vendored source at the cited lines. That transcription was verified
// byte-identical to the ACTUAL vendored FastLED 3.5.0 functions over all 256*256
// inputs (a driver-vs-vendored-source diff run during development); this file is the
// permanent, dependency-free regression gate.
//
// Exhaustive: every one of the 256*256 = 65,536 (value, scale) pairs.

#include "../../include/FastLED.h"   // the shipped driver, host path (__AVR__ undefined)
#include <cstdio>
#include <cstdint>

// ---- FastLED 3.5.0 reference (C path, FASTLED_SCALE8_FIXED == 1) -------------
namespace fl {
// scale8.h:24
static inline uint8_t scale8(uint8_t i, uint8_t scale) {
  return (uint8_t)((((uint16_t)i) * (1 + (uint16_t)scale)) >> 8);
}
// scale8.h:101
static inline uint8_t scale8_video(uint8_t i, uint8_t scale) {
  return (uint8_t)((((int)i * (int)scale) >> 8) + ((i && scale) ? 1 : 0));
}
// scale8.h:344
static inline void nscale8x3_video(uint8_t &r, uint8_t &g, uint8_t &b, uint8_t scale) {
  uint8_t nz = (scale != 0) ? 1 : 0;
  r = (r == 0) ? 0 : (uint8_t)((((int)r * (int)scale) >> 8) + nz);
  g = (g == 0) ? 0 : (uint8_t)((((int)g * (int)scale) >> 8) + nz);
  b = (b == 0) ? 0 : (uint8_t)((((int)b * (int)scale) >> 8) + nz);
}
// math8.h:379
static inline uint8_t qmul8(uint8_t i, uint8_t j) {
  int p = ((int)i * (int)j);
  if (p > 255) p = 255;
  return (uint8_t)p;
}
}  // namespace fl

// ---- harness MOCK reference (test/host/native_mocks/FastLED.h) ---------------
namespace mk {
// native_mocks/FastLED.h:8
static inline uint8_t qmul8(uint8_t i, uint8_t j) {
  unsigned p = (unsigned)i * (unsigned)j;
  return (p > 255) ? 255 : (uint8_t)p;
}
// native_mocks/FastLED.h:13  (the per-channel op behind CRGB::nscale8_video / %=)
static inline uint8_t nscale8_video_ch(uint8_t c, uint8_t scale) {
  uint8_t bump = (scale != 0) ? 1 : 0;
  return (c == 0) ? 0 : (uint8_t)((((int)c * (int)scale) >> 8) + bump);
}
}  // namespace mk

int main() {
  long d_scale8 = 0, d_scale8_video = 0, d_qmul8_fl = 0, d_qmul8_mk = 0;
  long d_nscale_fl = 0, d_nscale_mk = 0;

  for (int v = 0; v < 256; ++v) {
    for (int s = 0; s < 256; ++s) {
      uint8_t i = (uint8_t)v, sc = (uint8_t)s;

      // driver (global ::) vs FastLED
      if (::scale8(i, sc)       != fl::scale8(i, sc))       ++d_scale8;
      if (::scale8_video(i, sc) != fl::scale8_video(i, sc)) ++d_scale8_video;
      if (::qmul8(i, sc)        != fl::qmul8(i, sc))        ++d_qmul8_fl;

      // driver vs mock (the two scalers a golden actually exercises)
      if (::qmul8(i, sc)        != mk::qmul8(i, sc))        ++d_qmul8_mk;

      // nscale8x3_video: one channel value i, scale sc, three ways
      uint8_t dr = i, dg = i, db = i; ::nscale8x3_video(dr, dg, db, sc);
      uint8_t fr = i, fg = i, fb = i; fl::nscale8x3_video(fr, fg, fb, sc);
      uint8_t mch = mk::nscale8_video_ch(i, sc);
      if (dr != fr || dg != fg || db != fb) ++d_nscale_fl;
      if (dr != mch) ++d_nscale_mk;  // channels are symmetric, so one compare suffices
    }
  }

  long total = d_scale8 + d_scale8_video + d_qmul8_fl + d_qmul8_mk + d_nscale_fl + d_nscale_mk;
  printf("scale-oracle (exhaustive 256x256 = 65,536 pairs per op):\n");
  printf("  vs FastLED 3.5.0:  scale8=%ld  scale8_video=%ld  qmul8=%ld  nscale8x3_video=%ld  diffs\n",
         d_scale8, d_scale8_video, d_qmul8_fl, d_nscale_fl);
  printf("  vs harness mock :  qmul8=%ld  nscale8_video=%ld  diffs   (mock has no scale8/scale8_video\n",
         d_qmul8_mk, d_nscale_mk);
  printf("                     by design: goldens never scale during staging)\n");
  if (total != 0) {
    printf("FAIL: %ld mismatch(es) — the driver's scalers diverge from a reference.\n", total);
    return 1;
  }
  printf("OK  all scalers byte-identical to FastLED 3.5.0 AND the harness mock over the full domain\n");
  return 0;
}
