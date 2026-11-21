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
#include <initializer_list>

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

// ---- v1.4 (CA2): projector's step rule (N7) on a jewel of seven, with seeded dice ----
// Pixel 0 is the centre, 1 to 6 the ring. The windows: every pair, both halves of the ring, the
// centre with each half, the whole jewel. A step every FX_PROJECTOR_STEP_MIN_MS to _MAX_MS draws a
// candidate slot for each pixel; a brightening step (FX_PROJECTOR_BRIGHT_MS after the last one and
// after the level last went up, and it takes that window) gives every pixel its candidate, any
// other step only the candidates fxProjectorTake() allows. The level is the unit's brightness
// fraction through the core's own hold (contractBrightOffer, contractHoldsDue), steady or raised
// and dropped as fast as lines can come; a change of level repaints the slots and rolls nothing.
static uint32_t g_dice = 1;
static uint32_t dice(uint32_t n) { g_dice = g_dice * 1664525u + 1013904223u; return (g_dice >> 8) % n; }
struct JewelObs {                                   // the gate's rule on one window, counting onsets a second
  bool started, lit; int ref; uint32_t at[8]; int n, most;
  void reset() { started = lit = false; ref = 0; n = most = 0; }
  void see(int x, uint32_t t) {
    if (!started) { started = true; ref = x; lit = x >= STEP; return; }
    if (lit) { if (x > ref) ref = x; else if (ref - x >= STEP) { lit = false; ref = x; } }
    else if (x < ref) ref = x;
    else if (x - ref >= STEP) {
      lit = true; ref = x; at[n++ & 7] = t;
      int w = 0; for (int k = 0; k < 8 && k < n; k++) if (t - at[(n - 1 - k) & 7] < 1000u) w++;
      if (w > most) most = w;
    }
  }
};
static int projectorWorst(uint32_t rgb, bool stormy, uint32_t seed, int& brightenings, int& worstGap) {
  static const uint8_t kWin[26][7] = {                // pixel lists, 0xFF-terminated where shorter
    {0,1,255},{0,2,255},{0,3,255},{0,4,255},{0,5,255},{0,6,255},{1,2,255},{1,3,255},{1,4,255},{1,5,255},{1,6,255},
    {2,3,255},{2,4,255},{2,5,255},{2,6,255},{3,4,255},{3,5,255},{3,6,255},{4,5,255},{4,6,255},{5,6,255},
    {1,2,3,255},{4,5,6,255},{0,1,2,3,255},{0,4,5,6,255},{0,1,2,3,4,5,6} };
  static JewelObs obs[2][26];
  for (int m = 0; m < 2; m++) for (int w = 0; w < 26; w++) obs[m][w].reset();
  g_dice = seed;
  const ContractRGB c = { (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb };
  ContractUnitState u;
  uint8_t slot[7] = { 7, 7, 7, 7, 7, 7, 7 };          // dark until the first brightening
  uint32_t now = 1000, nextStep = 1000, nextLine = 1000, lastBright = 0; bool anyBright = false;
  brightenings = 0; worstGap = 1000000; int most = 0;
  const uint32_t end = now + 30u * 60u * 1000u;       // 30 minutes
  while (now < end) {
    bool changed = false;
    if (stormy && now >= nextLine) {                  // a B or b= line: up or down, as fast as they come
      contractBrightOffer(u, (uint8_t)(dice(3) ? (dice(2) ? 255 : 0) : dice(256)));
      nextLine = now + 10u + dice(40);
    }
    if (contractHoldsDue(u, now)) changed = true;
    if (now >= nextStep) {
      uint8_t cand[7]; for (int p = 0; p < 7; p++) cand[p] = (uint8_t)dice(10);
      const bool may = (!anyBright || strobeCoolDownExpired(lastBright, now, FX_PROJECTOR_BRIGHT_MS)) &&
                       strobeCoolDownExpired(u.levelMs, now, FX_PROJECTOR_BRIGHT_MS);
      for (int p = 0; p < 7; p++) {
        const ContractRGB a = fxProjectorSlot(c, slot[p]), b = fxProjectorSlot(c, cand[p]);
        const bool take = may || fxProjectorTake(a, b);
        // a crumbling step never lights a pixel by either measure
        if (!may && take) CHECK(contractLuminance(b.r, b.g, b.b) <= contractLuminance(a.r, a.g, a.b) &&
                                (b.r > b.g ? (b.r > b.b ? b.r : b.b) : (b.g > b.b ? b.g : b.b)) <= (a.r > a.g ? (a.r > a.b ? a.r : a.b) : (a.g > a.b ? a.g : a.b)));
        if (take) slot[p] = cand[p];
      }
      if (may) {
        if (anyBright && now - lastBright < (uint32_t)worstGap) worstGap = (int)(now - lastBright);
        lastBright = now; anyBright = true; u.levelMs = now; brightenings++;
      }
      changed = true;
      nextStep = now + FX_PROJECTOR_STEP_MIN_MS + dice(FX_PROJECTOR_STEP_MAX_MS - FX_PROJECTOR_STEP_MIN_MS + 1u);
    }
    if (changed) {
      uint8_t lv[7], ly[7];
      for (int p = 0; p < 7; p++) {
        const ContractRGB a = fxProjectorSlot(c, slot[p]);
        const uint8_t r = (uint8_t)((a.r * (uint16_t)u.bright) / 255u), g = (uint8_t)((a.g * (uint16_t)u.bright) / 255u), b = (uint8_t)((a.b * (uint16_t)u.bright) / 255u);
        lv[p] = r > g ? (r > b ? r : b) : (g > b ? g : b); ly[p] = contractLuminance(r, g, b);
      }
      for (int m = 0; m < 2; m++) for (int w = 0; w < 26; w++) {
        int sum = 0, k = 0;
        for (; k < 7 && kWin[w][k] != 255; k++) sum += (m ? ly : lv)[kWin[w][k]];
        obs[m][w].see(sum / k, now);
        if (obs[m][w].most > most) most = obs[m][w].most;
      }
    }
    now += 1u + dice(4);                              // a pass of the loop: 1 to 4 ms
  }
  return most;
}
static void test_projector_regions() {
  printf("projector (v1.4 N7), seven pixels, 30 minutes of steps each: the most onsets one window had in a second\n");
  for (uint32_t rgb : { 0x000000u, 0xff0000u, 0x116530u, 0xffffffu, 0x00ff00u })
    for (int stormy = 0; stormy < 2; stormy++) for (uint32_t seed : { 1u, 77u, 4242u }) {
      int bright = 0, gap = 0;
      const int w = projectorWorst(rgb, stormy != 0, seed, bright, gap);
      CHECK(w <= 3);
      CHECK(gap >= (int)FX_PROJECTOR_BRIGHT_MS);       // three brightenings in any second at the most
      CHECK(stormy ? bright > 0 : bright > 30 * 60 * 2);   // and it does brighten: steady, more than twice a second on average;
                                                       // under B and b= storms seldom, for the raises take the window
      if (seed == 1u) printf("  c=%06x %-26s %d (brightening steps %d, the closest two %d ms)\n", (unsigned)rgb,
                             stormy ? "under B and b= storms:" : "steady level:", w, bright, gap);
    }
}

int main() {
  test_projector_regions();
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
