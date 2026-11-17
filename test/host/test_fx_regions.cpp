// Part of the DroidNet Driveable-Animation Contract test harness.
// Copyright (c) 2026 Travis Cook. Shared byte-identically across the DroidNet
// RSeries/PSI/Flthy forks.  SPDX-License-Identifier: LGPL-2.1-only OR MIT
//
// A PART OF A STRAND, FOR EVERY SHARED LOOK BY ITSELF (core revision CA1.7). Pure core: no
// layer, no mock, no gate. -std=c++11 clean.
//
// The S9 gate measures a unit's whole field. A look that moves its light ALONG the strand can
// keep that level still and blink every part of the strand: a chase at 30 ms a step lit each
// position, and each two positions side by side, eleven times a second. The gate cannot see
// that, so the shared timing has to keep clear of it by itself. This test holds it to that:
//
//   every look the core times (chase, scan there-and-back and round a ring, comet, wipe, flash,
//   pulse, twinkle, gradient, colorcycle), ALONE, at EVERY speed 0..255, on EVERY strand length
//   6..28, for 4 s: no window of the strand goes from dark to lit more than three times in
//   any one second.
//
// A window: every run of neighbouring positions that is a quarter of the strand or longer
// (so the halves and the whole strand are among them). Its level is the mean over its positions
// of what the fx helpers return for each (lit 255, dark 0, or a comet's, a pulse's, a twinkle's
// brightness; for gradient and colorcycle the brightest channel and the luminance of the hue).
// An onset is counted as the gate counts one: a rise of more than a tenth of full scale above
// the last trough (and a fall of as much below the last peak ends it).
//
// It prints the most onsets any window had in a second, by look and strand length, and the step
// each strand length gets at the top speed.
#include "../../src/contract/contract_core.h"
#include <cstdio>
#include <cstring>

static int g_fail = 0; static long g_total = 0;
#define CHECK(cond) do { g_total++; if (!(cond)) { g_fail++; if (g_fail < 40) printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum Look { CHASE, SCAN, SCANRING, COMET, WIPE, FLASH, PULSE, TWINKLE, GRADIENT, CYCLE, NLOOKS };
static const char* const kName[NLOOKS] = { "chase", "scan", "scan, ring", "comet", "wipe", "flash", "pulse", "twinkle", "gradient", "colorcycle" };
static const int NMIN = 6, NMAX = 28;
static const int STEP = 26;                        // more than a tenth of 255

// One frame of a look on a strand of N: each position's level and luminance, 0..255.
static void frameOf(int look, int N, uint8_t sp, uint32_t t, uint8_t* lv, uint8_t* ly) {
  for (int p = 0; p < N; p++) {
    uint8_t v = 0; bool hue = false; ContractRGB c = { 0, 0, 0 };
    switch (look) {
      case CHASE:    v = fxChaseLit(p, t, sp, N) ? 255 : 0; break;
      case SCAN:     v = fxScanPos(t, sp, N, false) == p ? 255 : 0; break;
      case SCANRING: v = fxScanPos(t, sp, N, true) == p ? 255 : 0; break;
      case COMET:    v = fxCometBright(p, fxHead(t, sp, N), N); break;
      case WIPE:     v = fxWipeLit(p, t, sp, N) ? 255 : 0; break;
      case FLASH:    v = fxFlashOn(t, sp) ? 255 : 0; break;
      case PULSE:    v = fxPulseBright(t, sp); break;
      case TWINKLE:  v = fxTwinkleBright(p, t, sp); break;
      case GRADIENT: c = fxHsv2rgb(fxGradientHue(p, N, 0, t, sp), 255, 255); hue = true; break;
      default:       c = fxHsv2rgb(fxCycleHue(0, t, sp), 255, 255); hue = true; break;
    }
    if (hue) { lv[p] = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b); ly[p] = contractLuminance(c.r, c.g, c.b); }
    else lv[p] = ly[p] = v;
  }
}
// What decides a look's timing at a speed, so that two speeds with the same timing are run once.
static uint32_t timingOf(int look, int N, uint8_t sp) {
  switch (look) {
    case CHASE:    return fxChaseStepMs(sp);
    case SCAN:     return fxScanStepMs(sp, N, false);
    case SCANRING: return fxScanStepMs(sp, N, true);
    case COMET:    return fxCometStepMs(sp, N);
    case WIPE:     return fxWipeStepMs(sp, N);
    case FLASH:    return fxFlashHalfMs(sp);
    case PULSE:    return fxPulsePeriodMs(sp);
    case TWINKLE:  return 1000u + sp;              // its period is its own: every speed is run
    default:       return fxStepMs(sp);
  }
}
// One window's observer: the gate's own rule on the window's level.
struct Obs {
  bool started, lit; int ref; uint32_t at[4]; int n, most;
  void reset() { started = lit = false; ref = 0; n = most = 0; }
  void see(int x, uint32_t t) {
    if (!started) { started = true; ref = x; lit = x >= STEP; return; }
    if (lit) { if (x > ref) ref = x; else if (ref - x >= STEP) { lit = false; ref = x; } }
    else if (x < ref) ref = x;
    else if (x - ref >= STEP) {
      lit = true; ref = x; at[n++ & 3] = t;
      int w = 0; for (int k = 0; k < 4 && k < n; k++) if (t - at[(n - 1 - k) & 3] < 1000u) w++;
      if (w > most) most = w;
    }
  }
};
static Obs g_obs[2][NMAX][NMAX];
// The most onsets any window of a quarter of the strand or more had in one second.
static int worst(int look, int N, uint8_t sp) {
  const int quarter = (N + 3) / 4;
  for (int m = 0; m < 2; m++) for (int a = 0; a < N; a++) for (int b = a; b < N; b++) g_obs[m][a][b].reset();
  uint8_t lv[NMAX], ly[NMAX], was[NMAX]; bool first = true; int most = 0;
  const bool moves = look == PULSE || look == TWINKLE || look == GRADIENT || look == CYCLE;   // changes between steps
  const bool twoMeasures = look == GRADIENT || look == CYCLE;
  for (uint32_t t = 0; t < 4000u; t += 5u) {
    frameOf(look, N, sp, t, lv, ly);
    if (!first && !moves && memcmp(lv, was, (size_t)N) == 0) continue;
    first = false; memcpy(was, lv, (size_t)N);
    for (int m = 0; m < (twoMeasures ? 2 : 1); m++) {
      const uint8_t* v = m ? ly : lv;
      for (int a = 0; a < N; a++) {
        int sum = 0;
        for (int b = a; b < N; b++) {
          sum += v[b];
          const int len = b - a + 1;
          if (len < quarter) continue;
          Obs& o = g_obs[m][a][b];
          o.see(sum / len, t);
          if (o.most > most) most = o.most;
        }
      }
    }
  }
  return most;
}

int main() {
  static int table[NLOOKS][NMAX + 1];
  for (int look = 0; look < NLOOKS; look++) for (int N = NMIN; N <= NMAX; N++) {
    int w = 0; uint32_t seen[256]; int nSeen = 0;
    for (int sp = 255; sp >= 0; sp--) {
      const uint32_t key = timingOf(look, N, (uint8_t)sp);
      bool done = false; for (int i = 0; i < nSeen; i++) if (seen[i] == key) { done = true; break; }
      if (done) continue;
      seen[nSeen++] = key;
      const int x = worst(look, N, (uint8_t)sp);
      CHECK(x <= 3);
      if (x > 3 && g_fail < 40) printf("     %s on %d positions at s=%d: a window lit %d times in one second\n", kName[look], N, sp, x);
      if (x > w) w = x;
    }
    table[look][N] = w;
  }
  // the floors themselves, so that a change to one is a change to this file
  CHECK(FX_CHASE_STEP_MIN_MS == 130u && fxChaseStepMs(255) == 130u && fxChaseStepMs(0) == fxStepMs(0));
  CHECK(FX_SCAN_LAP_MIN_MS == 1000u);
  for (int N = 2; N <= 64; N++) for (int sp = 0; sp < 256; sp += 15) {
    const uint32_t there = fxScanStepMs((uint8_t)sp, N, false), ring = fxScanStepMs((uint8_t)sp, N, true), plain = fxStepMs((uint8_t)sp);
    CHECK(there >= plain && there * (uint32_t)(2 * (N - 1)) >= FX_SCAN_LAP_MIN_MS);
    CHECK(ring >= plain && ring * (uint32_t)N >= FX_CYCLE_MIN_MS);
    CHECK(there == plain || (there - 1u) * (uint32_t)(2 * (N - 1)) < FX_SCAN_LAP_MIN_MS);   // and no slower than the lap asks
    CHECK(ring == plain || (ring - 1u) * (uint32_t)N < FX_CYCLE_MIN_MS);
  }
  CHECK(fxScanStepMs(255, 8, false) == 72u && fxScanStepMs(255, 6, false) == 100u && fxScanStepMs(255, 18, false) == 30u);
  CHECK(fxScanStepMs(255, 7, true) == 98u && fxScanStepMs(255, 1, false) == fxStepMs(255) && fxScanPos(500, 255, 1, false) == 0);

  printf("The most onsets one window of a quarter of the strand or more had in a second, at any speed:\n%-11s", "positions");
  for (int N = NMIN; N <= NMAX; N++) printf(" %2d", N);
  printf("\n");
  for (int look = 0; look < NLOOKS; look++) {
    printf("%-11s", kName[look]);
    for (int N = NMIN; N <= NMAX; N++) printf(" %2d", table[look][N]);
    printf("\n");
  }
  printf("The step at s=255, ms:\n");
  static const char* const kStep[4] = { "chase", "scan", "scan, ring", "comet" };
  for (int k = 0; k < 4; k++) {
    printf("%-11s", kStep[k]);
    for (int N = NMIN; N <= NMAX; N++)
      printf(" %2u", (unsigned)(k == 0 ? fxChaseStepMs(255) : k == 1 ? fxScanStepMs(255, N, false) : k == 2 ? fxScanStepMs(255, N, true) : fxCometStepMs(255, N)) % 1000u);
    printf("\n");
  }
  if (g_fail) { printf("FAILURES  %ld/%ld checks passed\n", g_total - g_fail, g_total); return 1; }
  printf("OK  %ld/%ld checks passed\n", g_total, g_total);
  return 0;
}
