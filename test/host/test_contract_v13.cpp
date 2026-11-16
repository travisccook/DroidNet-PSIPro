// Part of the DroidNet Driveable-Animation Contract test harness.
// Copyright (c) 2026 Travis Cook. Shared byte-identically across the DroidNet
// RSeries/PSI/Flthy forks.  SPDX-License-Identifier: LGPL-2.1-only OR MIT
// Host unit tests for the contract v1.3 additions to contract_core.h — no external deps.
// -std=c++11 clean: the AVR forks build the core with gnu++11.
//
// Built twice by run.sh. The second time with -DCONTRACT_TEST_FLASH_HOOKS, which stands in for an
// AVR fork's PSTR() / pgm_read_byte(): a "flash" pointer is the real pointer with a high address
// bit flipped, and CONTRACT_READ_BYTE flips it back. Any code in the core that dereferences a
// name table directly, instead of through the hook, then faults instead of passing by accident.
#ifdef CONTRACT_TEST_FLASH_HOOKS
#include <stdint.h>
#define CONTRACT_FLASH_FLIP ((uintptr_t)1 << (sizeof(uintptr_t) * 8 - 2))
#define CONTRACT_PSTR(s) ((const char*)((uintptr_t)(s) ^ CONTRACT_FLASH_FLIP))
#define CONTRACT_READ_BYTE(p) (*(const char*)((uintptr_t)(p) ^ CONTRACT_FLASH_FLIP))
#endif
#include "../../src/contract/contract_core.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>

static int g_fail = 0, g_total = 0;
#define CHECK(cond) do { g_total++; if(!(cond)){ g_fail++; \
  printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);} } while(0)

static ContractParams P(const char* line) { ParsedContract p; contractParse(line, p); return p.params; }

// ---- S5: saturating parameters, exact mode words, the v1.3 keys ----
static void test_saturating_params() {
  CHECK(P("LFA:s=300").speed == 255);
  CHECK(P("LFA:b=300").bright == 255);
  CHECK(P("LFA:b=-1").bright == 0);
  CHECK(P("LFA:m=256").beatMod == 255);
  CHECK(P("L*L:v=99999999999").level == 255);
  CHECK(P("**C:bpb=999").bpb == 255);             // range-checked at the seed (S1), not wrapped here
  CHECK(P("**C:bpm=65656").bpm == 65535);         // was 120 after a 16-bit wrap
  // S1 as amended (CA1.2): bpm= is digits and nothing else. Anything else is not a tempo, and above
  // all not 0, which would be Hold: the line is marked and the seed ignores it whole.
  CHECK(P("**C:bpm=120").hasBpm && !P("**C:bpm=120").bpmBad && P("**C:bpm=0120").bpm == 120);
  for (const char* bad : { "**C:bpm=", "**C:bpm", "**C:bpm=abc", "**C:bpm=-120", "**C:bpm=+120", "**C:bpm=x20",
                           "**C:bpm= 120", "**C:bpm=12O", "**C:bpm=128.5", "**C:bpm=120 ", "**C:bpm=-0" }) {
    CHECK(!P(bad).hasBpm && P(bad).bpmBad);
  }
  CHECK(P("**C:bpm=abc,bpm=120").bpmBad && P("**C:bpm=120,bpm=").bpmBad);     // one bad bpm= spoils the line
  // at= and beat= are clamped to +/- CONTRACT_BEAT_MAX, so beat arithmetic cannot overflow an int32
  CHECK(CONTRACT_BEAT_MAX == 1000000000L);
  CHECK(P("LFA:at=2147483647").atBeat == CONTRACT_BEAT_MAX && P("LFA:at=-2147483648").atBeat == -CONTRACT_BEAT_MAX);
  CHECK(P("LFA:at=99999999999").atBeat == CONTRACT_BEAT_MAX && P("LFA:at=1000000000").atBeat == 1000000000L);
  CHECK(P("LFA:at=999999999").atBeat == 999999999L && P("LFA:at=-5").atBeat == -5 && P("LFA:at=40000").atBeat == 40000L);
  CHECK(P("**C:beat=2147483647").beatAnchor == CONTRACT_BEAT_MAX && P("**C:beat=-99999999999").beatAnchor == -CONTRACT_BEAT_MAX);
  CHECK(P("**C:bpm=0").hasBpm && P("**C:bpm=0").bpm == 0);   // C6: hold is a real value, not a parse failure
  for (int am = 0; am <= 4; am++) {               // the five known modes pass through
    char l[16]; snprintf(l, sizeof l, "LFA:am=%d", am);
    CHECK(P(l).hasAm && P(l).accentMode == am);
  }
  CHECK(P("LFA:am=5").accentMode == 0);           // an unknown mode is no accent...
  CHECK(P("LFA:am=9").accentMode == 0);
  CHECK(P("LFA:am=258").accentMode == 0);         // ...and never wraps into a known one (was 2)
  CHECK(P("LFA:am=-1").accentMode == 0);
  CHECK(P("**M:v=show").mode == 's');
  CHECK(P("**M:v=idle").mode == 'i');
  CHECK(P("**M:v=stop").mode == 0);               // used to mean show
  CHECK(P("**M:v=i").mode == 0);
  CHECK(P("**M:v=showtime").mode == 0);
  CHECK(P("**M:v=show,wd=15").hasWd && P("**M:v=show,wd=15").wd == 15);
  CHECK(P("**M:v=show,wd=999").wd == 255);
  CHECK(!P("**M:v=show").hasWd);
  CHECK(P("LFA:an=4,ap=2").hasAn && P("LFA:an=4,ap=2").an == 4 && P("LFA:an=4,ap=2").ap == 2);
  CHECK(!P("LFA:am=2").hasAn && P("LFA:am=2").an == 1 && P("LFA:am=2").ap == 0);
  CHECK(P("LFQ:r=0").hasRoute && P("LFQ:r=0").route == 0);
  CHECK(P("LFQ:r=2").route == 2 && P("LFQ:r=99").hasRoute && P("LFQ:r=99").route == 99 && P("LFQ:r=007").route == 7);
  CHECK(!P("LFQ").hasRoute);
  // section 10 as amended (CA1.2): an r= that is not a number 0..99 is no route at all (no reply)
  for (const char* bad : { "LFQ:r=", "LFQ:r", "LFQ:r=zz", "LFQ:r=100", "LFQ:r=250", "LFQ:r=-1", "LFQ:r=+1", "LFQ:r= 1",
                           "LFQ:r=1x", "LFQ:r=99999999999", "LFQ:r=5,r=" }) {
    CHECK(!P(bad).hasRoute && P(bad).route == 0);
  }
  CHECK(P("LFQ:r=,r=5").hasRoute && P("LFQ:r=,r=5").route == 5);             // the last r= decides
  CHECK(accentDurClamp(0) == 170 && accentDurClamp(180) == 180);
  CHECK(accentDurClamp(4000000000UL) == 2550 && accentDurClamp(_parseU32("-1")) == 2550);
  ParsedContract p;
  CHECK(contractParse("LFV:n=12,k=a1B2", p) && p.verb == CV_VERIFY);
  CHECK(p.params.hasN && p.params.n == 12 && p.params.hasK && p.params.k == 0xA1B2);
  CHECK(contractParse("LFV:n=12,k=a1b", p) && !p.params.hasK);       // k is exactly four hex digits
  CHECK(contractParse("LFV:n=12,k=a1b2c", p) && !p.params.hasK);
  CHECK(contractParse("LFV:n=12,k=a1g2", p) && !p.params.hasK);
  CHECK(contractParse("LFV:n=300,k=0000", p) && p.params.n == 255);
}

// ---- S1 / C6: BPM and bar ranges, and hold ----
static void test_bpm_range_and_hold() {
  BeatClock bc;
  CHECK(beatClockSeed(bc, P("**C:bpm=120,ph=0,bpb=4,beat=0"), 1000) == CS_SEEK);
  CHECK(bc.running && bc.bpm == 120 && bc.anchorMs == 1000);
  const BeatClock before = bc;
  const char* bad[] = { "**C:bpm=1,ph=5,beat=9", "**C:bpm=29,ph=5", "**C:bpm=301,ph=5",
                        "**C:bpm=65535", "**C:bpm=1200,beat=4", "**C:bpm=99999999999" };
  for (const char* line : bad) {
    CHECK(beatClockSeed(bc, P(line), 5000) == CS_IGNORED);
    CHECK(bc.bpm == before.bpm && bc.anchorMs == before.anchorMs
          && bc.beatOffset == before.beatOffset && bc.running);     // ignored WHOLE
  }
  CHECK(beatClockSeed(bc, P("**C:bpm=30"), 1000) == CS_SEEK && bc.bpm == 30);
  CHECK(beatClockSeed(bc, P("**C:bpm=300"), 1000) == CS_SEEK && bc.bpm == 300);
  beatClockSeed(bc, P("**C:bpm=120,bpb=0"), 1000);  CHECK(bc.bpb == 4);
  beatClockSeed(bc, P("**C:bpm=120,bpb=13"), 1000); CHECK(bc.bpb == 4);
  beatClockSeed(bc, P("**C:bpm=120,bpb=999"), 1000); CHECK(bc.bpb == 4);   // saturates to 255, then S1
  beatClockSeed(bc, P("**C:bpm=120,bpb=12"), 1000); CHECK(bc.bpb == 12);
  beatClockSeed(bc, P("**C:bpm=120,bpb=1"), 1000);  CHECK(bc.bpb == 1);
  // S1 as amended: a bpm= that is not a number makes the line a no-op, its ph= and beat= included.
  // (Up to CA1.1 "bpm=" parsed as 0, so one lost byte held the show, and only a C with a tempo
  // resumes a held show: the 4 s correction does not.)
  beatClockSeed(bc, P("**C:bpm=120,ph=0,bpb=4,beat=0"), 1000);
  const BeatClock running = bc;
  for (const char* line : { "**C:bpm=", "**C:bpm=abc", "**C:bpm=-120", "**C:bpm=x20", "**C:bpm=,ph=7,beat=3",
                            "**C:bpm=12O,bpb=3", "**C:ph=7,beat=3,bpm=" }) {
    CHECK(beatClockSeed(bc, P(line), 5000) == CS_IGNORED);
    CHECK(bc.running && bc.bpm == running.bpm && bc.anchorMs == running.anchorMs
          && bc.beatOffset == running.beatOffset && bc.bpb == running.bpb);
  }
  CHECK(beatClockSeed(bc, P("**C:ph=0,beat=8"), 5000) != CS_IGNORED);       // the next correction still lands
  BeatClock stopped;                                 // a bare re-anchor cannot start a clock
  CHECK(beatClockSeed(stopped, P("**C:ph=10,beat=8"), 1000) == CS_IGNORED && !stopped.running);

  // C6: bpm=0 holds. Nothing but a C with a tempo resumes.
  beatClockSeed(bc, P("**C:bpm=120,ph=0,bpb=4,beat=0"), 10000);
  CHECK(beatPosAt(bc, 12000).beatIndex == 4);
  CHECK(beatClockSeed(bc, P("**C:bpm=0"), 12000) == CS_HOLD && !bc.running);
  CHECK(beatPosAt(bc, 20000).beatIndex == 0);                              // a stopped clock reads as zero
  CHECK(beatClockSeed(bc, P("**C:ph=100,beat=9"), 13000) == CS_IGNORED && !bc.running);   // the 4 s correction does not resume
  CHECK(beatClockSeed(bc, P("**C:bpm=0,ph=5,beat=3"), 13000) == CS_HOLD && !bc.running);  // hold twice is still hold
  CHECK(beatClockSeed(bc, P("**C:bpm=120,ph=0,bpb=4,beat=4"), 30000) == CS_SEEK && bc.running);
  CHECK(beatPosAt(bc, 30500).beatIndex == 5);                              // resumed where the host said
  BeatClock fresh;                                   // hold on a clock that never ran: still a hold, still stopped
  CHECK(beatClockSeed(fresh, P("**C:bpm=0"), 5) == CS_HOLD && !fresh.running);
}

// ---- C1 / C2: what a re-anchor may do ----
static void test_seed_kinds_and_edge() {
  BeatClock bc;
  beatClockSeed(bc, P("**C:bpm=120,ph=0,bpb=4,beat=0"), 100000);
  CHECK(accentEdgeAfterSeed(bc, 100000) == BEAT_NONE);   // on the beat: it may accent
  CHECK(accentEdgeAfterSeed(bc, 100060) == BEAT_NONE);   // 60 ms in: still may
  CHECK(accentEdgeAfterSeed(bc, 100061) == 0);           // 61 ms in: beat 0 counts as done
  CHECK(accentEdgeAfterSeed(bc, 100400) == 0);
  CHECK(accentEdgeAfterSeed(bc, 100520) == BEAT_NONE);   // 20 ms into beat 1
  CHECK(accentEdgeAfterSeed(bc, 99900) == -1);           // before the anchor: 400 ms into beat -1
  // the host's bare correction, 400 ms into beat 8: the next beat is 9, 100 ms away
  CHECK(beatClockSeed(bc, P("**C:ph=100,beat=9"), 104400) == CS_NUDGE);
  CHECK(accentEdgeAfterSeed(bc, 104400) == 8);
  // a correction that pulls the position back across a beat boundary is still a nudge
  CHECK(beatClockSeed(bc, P("**C:ph=10,beat=9"), 104520) == CS_NUDGE);     // was 20 ms into beat 9; now 10 ms before it
  CHECK(accentEdgeAfterSeed(bc, 104520) == 8);
  // a bare line that jumps two bars is a seek, not a nudge
  CHECK(beatClockSeed(bc, P("**C:ph=0,beat=0"), 104600) == CS_SEEK);
  // a line with bpm= is always a seek, even when nothing moved
  CHECK(beatClockSeed(bc, P("**C:bpm=120,ph=0,beat=0"), 104600) == CS_SEEK);
  BeatClock off;
  CHECK(accentEdgeAfterSeed(off, 5) == BEAT_NONE);
  CHECK(scoreCursor(1, 0, true) == 1);               // C2: a nudge never steps back
  CHECK(scoreCursor(1, 0, false) == 0);              // a seek may
  CHECK(scoreCursor(1, 2, true) == 2 && scoreCursor(-1, 0, true) == 0 && scoreCursor(3, -1, true) == 3);
}

// ---- S2: the rate floor covers the pump and the overlay; every-N accents ----
static BeatPos posAt(int32_t beat, uint8_t bpb) {
  BeatPos bp; bp.beatIndex = beat; bp.barPos = (uint8_t)(((beat % bpb) + bpb) % bpb); return bp;
}
static void test_rate_floor_and_every_n() {
  CHECK(beatRateDivisor(30) == 1 && beatRateDivisor(120) == 1 && beatRateDivisor(176) == 1);
  CHECK(beatRateDivisor(177) == 2 && beatRateDivisor(300) == 2);
  CHECK(barRateDivisor(300, 4) == 1 && barRateDivisor(300, 1) == 2 && barRateDivisor(176, 1) == 1);
  CHECK(barRateDivisor(300, 2) == 1 && barRateDivisor(120, 0) == 1);     // bpb 0 reads as 4
  for (int bpm = 30; bpm <= 300; bpm++) {            // the property, for every legal tempo and group
    for (int grp = 1; grp <= 16; grp++) {
      const uint32_t k = barRateDivisor((uint16_t)bpm, (uint8_t)grp);
      CHECK(k * (uint32_t)grp * 60000u >= 340u * (uint32_t)bpm);                 // k groups last >= 340 ms
      CHECK(k == 1 || (k - 1) * (uint32_t)grp * 60000u < 340u * (uint32_t)bpm);  // and k is the smallest
    }
  }
  BeatClock bc; bc.running = true; bc.bpm = 240; bc.bpb = 4;
  int fired = 0;
  for (int32_t beat = -8; beat < 8; beat++) {
    const BeatPos bp = posAt(beat, 4);
    const bool f = beatAccentFires(2, bp, bc);
    CHECK(f == (beat % 2 == 0));                     // every second beat at 240 BPM
    if (f) fired++;
    CHECK((beatAccentAmount(2, bp, 255, 0, bc) > 0) == f);          // pump and overlay agree
    CHECK(beatAccentFires(3, bp, bc) == f);                         // build is every beat too, same floor
    CHECK(beatAccentFires(1, bp, bc) == (bp.barPos == 0));          // a 1 s bar needs no divisor
    CHECK(beatAccentFires(4, bp, bc) == (bp.barPos == 0));
    CHECK(!beatAccentFires(0, bp, bc) && !beatAccentFires(9, bp, bc));   // none, and an unknown mode: silent
  }
  CHECK(fired == 8);
  bc.bpm = 300; bc.bpb = 1;                          // a one-beat bar at 300 BPM is a 200 ms bar
  for (int32_t beat = -4; beat < 4; beat++) CHECK(beatAccentFires(1, posAt(beat, 1), bc) == (beat % 2 == 0));
  bc.bpm = 120; bc.bpb = 4;
  for (int32_t beat = -9; beat < 9; beat++) {        // ordinary tempos: the v1.2 predicate, unchanged
    const BeatPos bp = posAt(beat, 4);
    CHECK(beatAccentFires(2, bp, bc) && beatAccentFires(3, bp, bc));
    CHECK(beatAccentFires(1, bp, bc) == (bp.barPos == 0));
  }

  // every-N: one byte, n 1..16 in the high nibble (as n-1), the phase in the low one
  CHECK(accentEveryPack(1, 0) == 0 && accentEveryN(0) == 1 && accentEveryPhase(0) == 0);
  CHECK(accentEveryPack(4, 2) == 0x32 && accentEveryN(0x32) == 4 && accentEveryPhase(0x32) == 2);
  CHECK(accentEveryPack(16, 15) == 0xFF && accentEveryN(0xFF) == 16 && accentEveryPhase(0xFF) == 15);
  CHECK(accentEveryPack(0, 0) == 0 && accentEveryPack(200, 3) == 0xF3);     // n clamps to 1..16
  CHECK(accentEveryPack(4, 4) == 0x30 && accentEveryPack(4, 250) == 0x30);  // a phase >= n is 0
  CHECK(accentEveryPhase(0x1F) == 0);                                       // even from a byte nobody packed
  CHECK(accentEveryFrom(P("LFA:am=2")) == 0 && accentEveryFrom(P("LFA:am=2,an=2,ap=1")) == 0x11);
  CHECK(accentEveryFrom(P("LFA:am=2,an=0")) == 0 && accentEveryFrom(P("LFA:am=2,an=99,ap=99")) == 0xF0);
  for (int32_t beat = -9; beat < 17; beat++) {
    const BeatPos bp = posAt(beat, 4);
    CHECK(beatAccentFires(2, bp, bc, accentEveryPack(2, 0)) == (((beat % 2) + 2) % 2 == 0));   // front
    CHECK(beatAccentFires(2, bp, bc, accentEveryPack(2, 1)) == (((beat % 2) + 2) % 2 == 1));   // rear: alternate
    CHECK(beatAccentFires(2, bp, bc, accentEveryPack(4, 3)) == (((beat % 4) + 4) % 4 == 3));
    CHECK(beatAccentFires(1, bp, bc, accentEveryPack(4, 3)) == (bp.barPos == 0));              // am=1 ignores it
    CHECK((beatAccentAmount(2, bp, 200, 0, bc, accentEveryPack(4, 3)) > 0)
          == beatAccentFires(2, bp, bc, accentEveryPack(4, 3)));
  }
  bc.bpm = 300;                                      // the floor applies to the RESULT of every-N
  for (int32_t beat = -8; beat < 16; beat++) {
    const BeatPos bp = posAt(beat, 4);
    CHECK(beatAccentFires(2, bp, bc, accentEveryPack(2, 1)) == (((beat % 2) + 2) % 2 == 1));   // 400 ms apart: all kept
    CHECK(beatAccentFires(2, bp, bc, accentEveryPack(1, 0)) == (beat % 2 == 0));               // 200 ms apart: every other
  }
  CHECK(beatSectionProgress16(8, 0, 16) == 32768UL && beatSectionProgress16(8, 16, 16) == 0);
  CHECK(beatSectionProgress16(4, 8, 4) == 0);
}

// ---- CA1.3: the beat clock and the envelope in integers ----
// The references here are written with 64-bit integers (and, for the envelope, with the float code
// the core had until CA1.2), which the core itself may not use: it has to fit an 8-bit MCU.
static uint32_t g_clkRng = 0x6C8E9CF5u;
static uint32_t clkRnd() { g_clkRng = g_clkRng * 1664525u + 1013904223u; return g_clkRng >> 4; }
static int64_t floorDiv64(int64_t a, int64_t b) { int64_t q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
// What beatPosAt() must return, from first principles: beats = delta * bpm / 60000, exactly.
static void refPos(const BeatClock& bc, uint32_t nowMs, int32_t& beatIndex, uint32_t& units, uint16_t& phase16, uint8_t& barPos) {
  const int64_t d = (int64_t)(int32_t)(nowMs - bc.anchorMs) * (int64_t)bc.bpm;
  const int64_t beats = floorDiv64(d, 60000);
  units = (uint32_t)(d - beats * 60000);
  beatIndex = (int32_t)((int64_t)bc.beatOffset + beats);
  phase16 = (uint16_t)(((uint64_t)units * 65536u) / 60000u);
  const int bpb = bc.bpb ? bc.bpb : 4;
  barPos = (uint8_t)((((int64_t)beatIndex % bpb) + bpb) % bpb);
}
static bool posIs(const BeatClock& bc, uint32_t nowMs) {
  int32_t bi; uint32_t units; uint16_t ph; uint8_t bar;
  refPos(bc, nowMs, bi, units, ph, bar);
  const BeatPos p = beatPosAt(bc, nowMs);
  uint16_t u2; const int32_t whole = _beatSplit(bc, nowMs, u2);
  return p.beatIndex == bi && p.phase16 == ph && p.barPos == bar && u2 == units && (int32_t)((uint32_t)bc.beatOffset + (uint32_t)whole) == bi;
}
// The accent envelope as it was computed until CA1.2, in float, from a Q16 phase and progress.
static uint8_t refAmountFloat(uint8_t am, uint16_t phase16, uint8_t barPos, uint8_t beatMod, uint32_t progress16) {
  const float phase = (float)phase16 / 65536.0f;
  float env = (1.0f - phase) * (1.0f - phase);
  if (barPos == 0) { env *= 1.3f; if (env > 1.0f) env = 1.0f; }
  if (am == 3) env *= (float)progress16 / 65536.0f;
  float amt = env * (float)beatMod;
  if (amt < 0) amt = 0;
  if (amt > 255) amt = 255;
  return (uint8_t)amt;
}
static void test_integer_clock() {
  // 1. EXACT, everywhere: every tempo, at offsets from a millisecond to the ends of the 24.8 days
  //    either side of the anchor, with the anchor on both sides of the millis() wrap.
  static const uint32_t kAnchor[] = { 0u, 1000u, 0x7FFFFFF0u, 0x80000000u, 0xFFFFFF00u, 0xFFFFFFFFu };
  static const int32_t kDelta[] = { 0, 1, 2, 199, 200, 201, 499, 500, 59999, 60000, 60001, 600000, 3600000, 86400000,
                                    2147483647, -1, -2, -200, -201, -59999, -60000, -60001, -3600000, -2147483647 - 1 };
  for (int bpm = CONTRACT_BPM_MIN; bpm <= CONTRACT_BPM_MAX; bpm++)
    for (uint32_t anchor : kAnchor) for (int32_t delta : kDelta) for (int32_t off : { 0, -5, 1000000000 }) {
      BeatClock bc; bc.running = true; bc.bpm = (uint16_t)bpm; bc.bpb = (uint8_t)(1 + (bpm % 12)); bc.anchorMs = anchor; bc.beatOffset = off;
      CHECK(posIs(bc, anchor + (uint32_t)delta));
    }
  for (int i = 0; i < 300000; i++) {
    BeatClock bc; bc.running = true; bc.bpm = (uint16_t)(CONTRACT_BPM_MIN + clkRnd() % 271u); bc.bpb = (uint8_t)(1 + clkRnd() % 12u);
    bc.anchorMs = clkRnd() * 16u + clkRnd() % 16u; bc.beatOffset = (int32_t)(clkRnd() % 2000001u) - 1000000;
    const uint32_t span = (i % 3 == 0) ? 0xFFFFFFFFu : (i % 3 == 1 ? 7200000u : 4000u);
    CHECK(posIs(bc, bc.anchorMs + (clkRnd() * 16u + clkRnd() % 16u) % span - span / 2u));
  }

  // 2. NO DRIFT. Beat k begins at the first whole millisecond at or after its true instant
  //    (k * 60000 / bpm ms after the anchor): never early, and under a millisecond late, whether k is
  //    the first beat, the one at ten minutes, at one hour, or at 24 days. (The float clock this
  //    replaced was up to 0.06 ms off at ten minutes and 0.35 ms at an hour, growing with time.)
  for (int bpm = CONTRACT_BPM_MIN; bpm <= CONTRACT_BPM_MAX; bpm++) {
    for (uint32_t anchor : { 5000u, 0xFFFFF000u }) {
      BeatClock bc; bc.running = true; bc.bpm = (uint16_t)bpm; bc.bpb = 4; bc.anchorMs = anchor;
      const int64_t perMin = bpm;
      for (int64_t k : { (int64_t)1, (int64_t)2, (int64_t)3, perMin, perMin * 10 - 1, perMin * 10, perMin * 10 + 1, perMin * 60 - 1, perMin * 60,
                         perMin * 60 + 1, perMin * 60 * 24, perMin * 35000 + 7 }) {
        const uint32_t at = (uint32_t)((k * 60000 + bpm - 1) / bpm);        // ceil(k * 60000 / bpm)
        CHECK(beatPosAt(bc, anchor + at).beatIndex == (int32_t)k);
        CHECK(beatPosAt(bc, anchor + at - 1u).beatIndex == (int32_t)k - 1);
        if ((k * 60000) % bpm == 0) CHECK(beatPosAt(bc, anchor + at).phase16 == 0);   // on the millisecond: phase exactly 0
        CHECK(beatPosAt(bc, anchor + at - 1u).phase16 > beatPosAt(bc, anchor + at).phase16);
      }
      CHECK(beatPosAt(bc, anchor - 1u).beatIndex == -1 && beatPosAt(bc, anchor).beatIndex == 0);   // before the anchor: negative, floored
    }
  }
  // the phase rises through a beat and never reaches the next one
  { BeatClock bc; bc.running = true; bc.bpm = 97; bc.bpb = 4; bc.anchorMs = 0xFFFFFE00u;
    uint16_t prev = 0; int32_t beat = 0;
    for (uint32_t t = 0; t < 5000; t++) {
      const BeatPos p = beatPosAt(bc, bc.anchorMs + t);
      if (p.beatIndex == beat) CHECK(p.phase16 >= prev); else { CHECK(p.beatIndex == beat + 1 && p.phase16 < 200); beat = p.beatIndex; }
      prev = p.phase16;
    }
  }
  // a stopped clock, and a tempo no line can set, are still well defined (nothing undefined to trip on)
  { BeatClock bc; CHECK(beatPosAt(bc, 12345).beatIndex == 0 && beatPosAt(bc, 12345).phase16 == 0);
    bc.running = true; bc.bpm = 65535; bc.bpb = 0; bc.anchorMs = 7; bc.beatOffset = 2147483647;
    (void)beatPosAt(bc, 0x80000007u); (void)beatPosAt(bc, 6u); CHECK(beatPosAt(bc, 7).phase16 == 0 && beatPosAt(bc, 7).barPos == 3); }

  // 3. NUDGE OR SEEK, exactly: a bare re-anchor that moves the position by less than one beat
  //    either way is a nudge. The reference compares the two positions in units, in 64 bits.
  for (int i = 0; i < 200000; i++) {
    BeatClock bc; bc.running = true; bc.bpm = (uint16_t)(CONTRACT_BPM_MIN + clkRnd() % 271u); bc.bpb = 4;
    bc.anchorMs = clkRnd() * 16u; bc.beatOffset = (int32_t)(clkRnd() % 4001u) - 2000;
    const uint32_t now = bc.anchorMs + clkRnd() % 600000u;
    int32_t bi; uint32_t ua; uint16_t ph; uint8_t bar;
    refPos(bc, now, bi, ua, ph, bar);
    ContractParams pr; pr.hasPh = true; pr.phMs = clkRnd() % 2500u; pr.hasBeat = true;
    pr.beatAnchor = bi + (int32_t)(clkRnd() % 9u) - 4;                 // the host's idea of the next beat: near, or a seek
    BeatClock nb = bc;
    const ContractSeed k = beatClockSeed(nb, pr, now);
    int32_t bj; uint32_t ub;
    refPos(nb, now, bj, ub, ph, bar);
    const int64_t shift = ((int64_t)bj * 60000 + ub) - ((int64_t)bi * 60000 + ua);
    CHECK(k == ((shift > -60000 && shift < 60000) ? CS_NUDGE : CS_SEEK));
  }
  // 4. C1's edge guard, exactly: more than 60 whole milliseconds into the beat, or not.
  for (int bpm = CONTRACT_BPM_MIN; bpm <= CONTRACT_BPM_MAX; bpm += 7) {
    BeatClock bc; bc.running = true; bc.bpm = (uint16_t)bpm; bc.bpb = 4; bc.anchorMs = 0xFFFFFFC0u; bc.beatOffset = -3;
    for (uint32_t t = 0; t < 6000; t++) {
      int32_t bi; uint32_t units; uint16_t ph; uint8_t bar;
      refPos(bc, bc.anchorMs + t, bi, units, ph, bar);
      CHECK(accentEdgeAfterSeed(bc, bc.anchorMs + t) == ((units / (uint32_t)bpm > ACCENT_EDGE_LATE_MS) ? bi : BEAT_NONE));
    }
  }

  // 5. The section progress, Q16 and clamped.
  CHECK(beatSectionProgress16(0, 0, 16) == 0 && beatSectionProgress16(-3, 0, 16) == 0);
  CHECK(beatSectionProgress16(4, 0, 16) == 16384UL && beatSectionProgress16(15, 0, 16) == 61440UL);
  CHECK(beatSectionProgress16(16, 0, 16) == 65536UL && beatSectionProgress16(99, 0, 16) == 65536UL);
  CHECK(beatSectionProgress16(1, 0, 3) == 21845UL && beatSectionProgress16(2, 0, 3) == 43690UL);     // floor(n * 65536 / d)
  CHECK(beatSectionProgress16(-7, -8, 8) == 4096UL);
  for (int i = 0; i < 100000; i++) {
    const int32_t start = (int32_t)(clkRnd() % 2000001u) - 1000000, len = 1 + (int32_t)(clkRnd() % 65535u);
    const int32_t beat = start + (int32_t)(clkRnd() % (uint32_t)(len + 20)) - 10;
    const int64_t n = (int64_t)beat - start;
    const uint32_t want = n <= 0 ? 0 : (n >= len ? 65536u : (uint32_t)((n * 65536) / len));
    CHECK(beatSectionProgress16(beat, start, start + len) == want);                    // exact up to 65535 beats
  }
  // a section longer than 65535 beats (only the ends of the range can make one): still monotonic,
  // still 0 .. 65536, and within a part in sixteen thousand of the exact ratio
  { const int32_t lo = -CONTRACT_BEAT_MAX, hi = CONTRACT_BEAT_MAX; uint32_t prev = 0;
    for (int64_t b = lo; b <= hi; b += 12345677) {
      const uint32_t got = beatSectionProgress16((int32_t)b, lo, hi);
      const int64_t exact = ((b - lo) * 65536) / ((int64_t)hi - lo);
      CHECK(got >= prev && got <= 65536UL && (int64_t)got - exact <= 4 && exact - (int64_t)got <= 4);
      prev = got;
    }
    CHECK(beatSectionProgress16(hi, lo, hi) == 65536UL && beatSectionProgress16(lo, lo, hi) == 0);
    CHECK(beatSectionProgress16(CONTRACT_BEAT_MAX, CONTRACT_BEAT_MAX, CONTRACT_BEAT_MAX + 9999) == 0); }

  // 6. THE ENVELOPE. Its ends, its shape, and how far it is from the float one it replaced:
  //    never more than 1 in the amount, over every phase.
  BeatClock bc; bc.running = true; bc.bpm = 120; bc.bpb = 4;
  BeatPos bp; bp.beatIndex = 1; bp.barPos = 1;
  bp.phase16 = 0;     CHECK(beatAccentAmount(2, bp, 255, 0, bc) == 255 && beatAccentAmount(2, bp, 200, 0, bc) == 200);   // full at the beat
  bp.phase16 = 32768; CHECK(beatAccentAmount(2, bp, 255, 0, bc) == 63 && beatAccentAmount(2, bp, 200, 0, bc) == 50);     // a quarter at half way
  bp.phase16 = 65535; CHECK(beatAccentAmount(2, bp, 255, 0, bc) == 0);                                                    // gone by the next
  bp.beatIndex = 4; bp.barPos = 0;                                                                                         // a downbeat: x 1.3, capped
  bp.phase16 = 0;     CHECK(beatAccentAmount(1, bp, 255, 0, bc) == 255);
  bp.phase16 = 8192;  CHECK(beatAccentAmount(1, bp, 255, 0, bc) == 253);      // 0.875^2 * 1.3 = 0.9953
  bp.phase16 = 32768; CHECK(beatAccentAmount(1, bp, 255, 0, bc) == 82);       // 0.25 * 1.3 = 0.325
  bp.phase16 = 0;                                                             // build: scaled by the section progress
  CHECK(beatAccentAmount(3, bp, 255, 0, bc) == 0 && beatAccentAmount(3, bp, 255, 32768UL, bc) == 127);
  CHECK(beatAccentAmount(3, bp, 255, 65536UL, bc) == 255 && beatAccentAmount(3, bp, 255, 0xFFFFFFFFUL, bc) == 255);
  int maxDiff = 0; long differ = 0, total = 0;
  for (int am = 1; am <= 3; am++) for (int bar = 0; bar <= 1; bar++)
    for (int mod : { 1, 17, 64, 128, 200, 254, 255 }) for (uint32_t prog : { 0u, 1u, 9000u, 32768u, 65535u, 65536u }) {
      if (am != 3 && prog != 0u) continue;
      if (am == 1 && bar != 0) continue;
      uint8_t prev = 255;
      for (uint32_t ph = 0; ph < 65536u; ph++) {
        BeatPos q; q.beatIndex = bar; q.barPos = (uint8_t)bar; q.phase16 = (uint16_t)ph;
        const int got = beatAccentAmount((uint8_t)am, q, (uint8_t)mod, prog, bc);
        const int ref = refAmountFloat((uint8_t)am, (uint16_t)ph, (uint8_t)bar, (uint8_t)mod, prog);
        const int d = got > ref ? got - ref : ref - got;
        if (d > maxDiff) maxDiff = d;
        if (d) differ++;
        total++;
        if (got > prev) { CHECK(got <= prev); }                               // it only ever falls through a beat
        prev = (uint8_t)got;
      }
    }
  CHECK(maxDiff <= 1);
  CHECK(differ * 50 < total);                                                 // and it is the same value 98 times in 100
  if (maxDiff > 1 || differ * 50 >= total) printf("     (integer envelope against the float one: max difference %d, %ld of %ld values differ)\n", maxDiff, differ, total);
}

// ---- L1 / L2 / L5: one reset, what survives it, armed-but-not-begun, the watchdog ----
static void test_show_reset_and_watchdog() {
  ContractUnitState s;
  CHECK(!s.inShow && s.base == CB_NONE && s.effect == CE_OFF && s.bright == 255 && s.loadOk == 0);
  s.effect = CE_SOLID; s.color = ContractRGB{9, 9, 9}; s.base = CB_LOOK; s.pulseActive = true;
  s.holdPending = true; s.scoreLen = 5; s.curSection = 3; s.loadOk = 1; s.overflow = true;
  s.lastAccentBeat = 42; s.wdSec = 15; s.pinForward = true; s.bright = 10; s.beatMod = 200;
  s.accentMode = 2; s.accentEvery = 0x11; s.pulseLastMs = 7777; s.lastApplyMs = 8888;
  contractOnShow(s, 160, 15);
  CHECK(s.inShow && s.wdSec == 15 && s.ceil == 160 && s.bright == 255);
  CHECK(s.base == CB_NONE);                                // L1: armed, and the native idle look keeps running
  CHECK(s.effect == CE_OFF && s.color.r == 0 && !s.pulseActive && !s.holdPending);
  CHECK(s.beatMod == 0 && s.accentMode == 0 && s.accentEvery == 0);
  CHECK(s.scoreLen == 0 && s.curSection == -1 && s.loadOk == 0);
  CHECK(!s.overflow && s.lastAccentBeat == BEAT_NONE && !s.pinForward);
  CHECK(s.pulseLastMs == 7777 && s.lastApplyMs == 8888);   // the safety anchors are NOT reset
  CHECK(contractShowBegin(s) && s.base == CB_DARK);        // the show's first C, live A or P
  CHECK(!contractShowBegin(s));                            // once
  s.scoreLen = 3;
  contractOnStop(s, 140);                                  // X keeps the mode and the watchdog
  CHECK(s.inShow && s.wdSec == 15 && s.base == CB_DARK && s.scoreLen == 0 && s.ceil == 140);
  CHECK(!contractShowBegin(s));                            // already dark
  CHECK(!contractWatchdogExpired(s, 1000, 15999));
  CHECK(contractWatchdogExpired(s, 1000, 16000));
  CHECK(contractWatchdogExpired(s, 0xFFFFF000u, 0xFFFFF000u + 15000u));   // across the millis wrap
  contractOnIdle(s, 140);
  CHECK(!s.inShow && s.wdSec == 0 && s.base == CB_NONE);
  CHECK(!contractWatchdogExpired(s, 0, 4000000000UL));     // idle: never
  CHECK(!contractShowBegin(s) && s.base == CB_NONE);       // a P outside a show overlays the native look (L3)
  ContractUnitState noWd; contractOnShow(noWd, 200, 0);
  CHECK(!contractWatchdogExpired(noWd, 0, 4000000000UL));  // no wd= on the wire: never

  // a failed load check outlives the idle the unit ran on itself, and any X; M:v=show clears it
  ContractUnitState f; contractOnShow(f, 200, 15); f.loadOk = 2;
  contractOnIdle(f, 200);  CHECK(f.loadOk == 2 && !f.inShow);
  contractOnStop(f, 200);  CHECK(f.loadOk == 2);
  contractOnShow(f, 200, 0); CHECK(f.loadOk == 0);
  ContractUnitState g; contractOnShow(g, 200, 0); g.loadOk = 1;
  contractOnIdle(g, 200);  CHECK(g.loadOk == 0);           // a GOOD verdict does not outlive its show

  // which units keep a board's clock alive at a reset
  ContractUnitState c;
  CHECK(!contractUnitNeedsClock(c));
  c.base = CB_DARK;   CHECK(!contractUnitNeedsClock(c));
  c.base = CB_LOOK;   CHECK(contractUnitNeedsClock(c));
  c.base = CB_NATIVE; CHECK(contractUnitNeedsClock(c));
  c.base = CB_NONE; c.scoreLen = 1; CHECK(contractUnitNeedsClock(c));

  // S6: b= is a fraction of the ceiling
  CHECK(contractLevel(160, 255) == 160 && contractLevel(160, 0) == 0 && contractLevel(160, 128) == 80);
  CHECK(contractLevel(255, 200) == 200 && contractLevel(0, 255) == 0 && contractLevel(1, 254) == 0);
  for (int c2 = 0; c2 <= 255; c2 += 5) for (int b = 0; b <= 255; b += 5) CHECK(contractLevel((uint8_t)c2, (uint8_t)b) <= c2);
}

// ---- S4: the live-look hold ----
static void test_live_hold() {
  ContractUnitState s; ContractLookPatch a, b, c, out;
  a.has = LP_COLOR; a.color = ContractRGB{1, 0, 0};
  b.has = LP_COLOR; b.color = ContractRGB{2, 0, 0};
  c.has = LP_SPEED; c.speed = 77;
  CHECK(liveLookOffer(s, a, 1000));                  // first look: now
  CHECK(!liveLookOffer(s, a, 1100));                 // 100 ms later: held
  CHECK(!liveLookOffer(s, b, 1150));                 // same key: the newest value wins
  CHECK(!liveLookOffer(s, c, 1160));                 // another key: merged in, the colour is kept
  CHECK(!liveLookDue(s, 1169, out));
  CHECK(liveLookDue(s, 1170, out));
  CHECK(out.has == (LP_COLOR | LP_SPEED) && out.color.r == 2 && out.speed == 77);
  CHECK(!liveLookDue(s, 1171, out));                 // delivered once
  CHECK(!liveLookOffer(s, a, 1200));                 // the window restarted at 1170
  CHECK(!liveLookOffer(s, b, 1500));                 // a look is waiting: a later one joins it, it does not jump the queue
  CHECK(liveLookDue(s, 1500, out) && out.color.r == 2);
  CHECK(!liveLookOffer(s, a, 1600) && liveLookDue(s, 1670, out));
  CHECK(liveLookOffer(s, a, 1840));                  // nothing waiting, window over: now
  contractOnStop(s, 255);                            // a reset drops the held look but keeps the window
  CHECK(!liveLookOffer(s, a, 1900));
  contractOnIdle(s, 255);
  CHECK(!liveLookDue(s, 5000, out));                 // ...and the look held across the reset is gone
  ContractUnitState boot;                            // never applied anything: lastApplyMs == 0
  CHECK(!liveLookOffer(boot, a, 100));               // the first 170 ms after power-on: held
  ContractUnitState boot2;
  CHECK(liveLookOffer(boot2, a, 170));

  ContractLookPatch lp = lookPatchFrom(P("LFA:i=solid,c=0a0b0c,s=9,m=7"));
  CHECK(lp.has == (LP_EFFECT | LP_COLOR | LP_SPEED | LP_BEATMOD) && lp.effect == CE_SOLID);
  CHECK(lp.color.g == 0x0b && lp.speed == 9 && lp.beatMod == 7);
  lp = lookPatchFrom(P("LFA:am=2,an=2,ap=1,b=40"));
  CHECK(lp.has == (LP_AM | LP_EVERY | LP_BRIGHT) && lp.accentMode == 2 && lp.every == 0x11 && lp.bright == 40);
  lp = lookPatchFrom(P("LFA:i=native:4"));
  CHECK(lp.effect == CE_NATIVE && lp.nativeCode == 4);
  // L5: an A with no i= on a unit with no look is i=solid; on a unit with a look it is a patch
  ContractLookPatch np = lookPatchFrom(P("LFA:c=ff0000"));
  ContractLookPatch n1 = np; lookPatchDefault(n1, CB_NONE);   CHECK((n1.has & LP_EFFECT) && n1.effect == CE_SOLID);
  ContractLookPatch n2 = np; lookPatchDefault(n2, CB_DARK);   CHECK((n2.has & LP_EFFECT) && n2.effect == CE_SOLID);
  ContractLookPatch n3 = np; lookPatchDefault(n3, CB_LOOK);   CHECK(!(n3.has & LP_EFFECT));
  ContractLookPatch n4 = np; lookPatchDefault(n4, CB_NATIVE); CHECK(!(n4.has & LP_EFFECT));
  ContractLookPatch n5 = lookPatchFrom(P("LFA:i=wipe")); lookPatchDefault(n5, CB_NONE); CHECK(n5.effect == CE_WIPE);
}

// ---- sections 6 and 7: capacity, overflow, per-entry brightness, verb V ----
// A two-unit board (class L: F and R), the way a fork feeds scored lines to the core.
struct TwoUnits {
  ContractUnitState s[2]; ScoreEntry t[2][8];
  void line(const char* afterBang) {
    ParsedContract p;
    if (!contractParse(afterBang, p) || p.verb != CV_ANIMATE || !p.params.hasAt) return;
    if (p.cls != 'L' && p.cls != '*') return;
    ScoreEntry e; e.atBeat = p.params.atBeat;
    const uint16_t crc = contractCrc16(afterBang);
    if (p.unit == 'F' || p.unit == '*') scoreAccept(s[0], t[0], 8, e, crc);
    if (p.unit == 'R' || p.unit == '*') scoreAccept(s[1], t[1], 8, e, crc);
  }
};
static void test_score_verify() {
  CHECK(contractCrc16("123456789") == 0x29B1);       // the CRC-16/CCITT-FALSE check value
  CHECK(contractCrc16("") == 0xFFFF);
  CHECK(contractCrc16("LFA:i=native:0,at=447") == 0xF9E8);

  // THE SHARED V VECTORS. The delivery plan pins the same numbers in Jest
  // (DroidNetSignalBooster benchmarks/lighting-review-2026-11-15/delivery/v_vectors.py). If these
  // fail, find out which side is wrong before changing either.
  TwoUnits b;
  b.line("LFA:i=native:0,c=3b82f6,at=0");
  b.line("LFA:i=rainbow,c=f97316,at=56,am=1,m=200,ae=flash");
  b.line("LFA:i=native:0,at=447");
  b.line("L*A:i=solid,c=0080FF,at=8");               // a wildcard line counts on every unit it addresses
  b.line("**A:i=off,at=512");
  b.line("PFA:i=solid,at=4");                        // another board's line counts nowhere here
  CHECK(b.s[0].scoreLen == 5 && scoreK(b.t[0], b.s[0].scoreLen) == 0x1B18);   // !LFV:n=5,k=1b18
  CHECK(b.s[1].scoreLen == 2 && scoreK(b.t[1], b.s[1].scoreLen) == 0xCB04);   // !LRV:n=2,k=cb04
  ParsedContract v;
  CHECK(contractParse("LFV:n=5,k=1b18", v) && scoreVerify(b.s[0], b.t[0], v.params.n, v.params.k) == 1);
  CHECK(contractParse("LRV:n=2,k=cb04", v) && scoreVerify(b.s[1], b.t[1], v.params.n, v.params.k) == 1);
  CHECK(b.s[0].loadOk == 1 && b.s[1].loadOk == 1);
  b.line("LFA:i=comet,c=ff0000,at=56");              // replaces LF's entry at 56: n stays 5, k moves
  CHECK(b.s[0].scoreLen == 5 && scoreK(b.t[0], 5) == 0xA86C);                 // !LFV:n=5,k=a86c
  CHECK(scoreVerify(b.s[0], b.t[0], 5, 0x1B18) == 2 && scoreVerify(b.s[0], b.t[0], 5, 0xA86C) == 1);
  // a line delivered twice (a mesh retry) changes nothing
  b.line("LFA:i=comet,c=ff0000,at=56"); b.line("**A:i=off,at=512"); b.line("LFA:i=native:0,at=447");
  CHECK(b.s[0].scoreLen == 5 && scoreK(b.t[0], 5) == 0xA86C && !b.s[0].overflow);
  CHECK(b.s[1].scoreLen == 2 && scoreK(b.t[1], 2) == 0xCB04);
  // arrival order does not matter
  TwoUnits r;
  r.line("**A:i=off,at=512"); r.line("L*A:i=solid,c=0080FF,at=8"); r.line("LFA:i=native:0,at=447");
  r.line("LFA:i=rainbow,c=f97316,at=56,am=1,m=200,ae=flash"); r.line("LFA:i=native:0,c=3b82f6,at=0");
  CHECK(r.s[0].scoreLen == 5 && scoreK(r.t[0], 5) == 0x1B18 && r.t[0][0].atBeat == 0 && r.t[0][4].atBeat == 512);
  // what a bad load looks like
  ContractUnitState c1 = r.s[0];  CHECK(scoreVerify(c1, r.t[0], 6, 0x1B18) == 2);     // the host sent one more
  ContractUnitState c2 = r.s[0];  CHECK(scoreVerify(c2, r.t[0], 5, 0x1B19) == 2);     // a line differs
  ContractUnitState c3;           CHECK(scoreVerify(c3, r.t[0], 0, 0x0000) == 1);     // an empty unit, told so: good

  // capacity: a full table refuses, marks the load bad, and keeps what it has
  ScoreEntry t[2]; ContractUnitState s; ScoreEntry e;
  e.atBeat = 16; CHECK(scoreAccept(s, t, 2, e, 0x1111));
  e.atBeat = 0;  CHECK(scoreAccept(s, t, 2, e, 0x2222));
  CHECK(s.scoreLen == 2 && t[0].atBeat == 0 && t[0].crc == 0x2222 && t[1].crc == 0x1111 && s.curSection == -1);
  e.atBeat = 32; CHECK(!scoreAccept(s, t, 2, e, 0x4444));
  CHECK(s.overflow && s.scoreLen == 2 && scoreK(t, 2) == 0x3333);
  e.atBeat = 16; CHECK(scoreAccept(s, t, 2, e, 0x5555) && s.scoreLen == 2);   // a replace still fits a full table
  CHECK(scoreK(t, 2) == (0x2222 ^ 0x5555));
  CHECK(scoreVerify(s, t, 2, 0x2222 ^ 0x5555) == 2); // an overflowed load is bad even when n and k match
  // a unit that failed its check takes no more scored lines until the next M:v=show: a line that
  // arrives late must not start half a show (the 2026-11-16 review's finding 4)
  e.atBeat = 16; CHECK(!scoreAccept(s, t, 2, e, 0x7777) && t[1].crc == 0x5555);
  contractOnIdle(s, 255); CHECK(s.loadOk == 2 && s.scoreLen == 0);
  e.atBeat = 0;  CHECK(!scoreAccept(s, t, 2, e, 0x1234) && s.scoreLen == 0 && !s.overflow);
  contractOnStop(s, 255); CHECK(!scoreAccept(s, t, 2, e, 0x1234) && s.scoreLen == 0);
  contractOnShow(s, 255, 0); CHECK(scoreAccept(s, t, 2, e, 0x1234) && s.scoreLen == 1);
  // the same line again (a load sent twice, or re-sent while it plays) changes nothing, the cursor
  // included; a different line for the same beat does (finding 8)
  s.curSection = 0; s.pinForward = true;
  CHECK(scoreAccept(s, t, 2, e, 0x1234) && s.scoreLen == 1 && s.curSection == 0 && s.pinForward);
  CHECK(scoreAccept(s, t, 2, e, 0x4321) && s.scoreLen == 1 && s.curSection == -1 && !s.pinForward && t[0].crc == 0x4321);
  s.curSection = 0; e.atBeat = 8;
  CHECK(scoreAccept(s, t, 2, e, 0x4321) && s.scoreLen == 2 && s.curSection == -1);    // same CRC, another beat: a new entry
  uint8_t n = 2; ScoreEntry same; same.atBeat = 0;
  CHECK(scoreInsertEx(t, n, 2, same) == SI_REPLACED && n == 2);
  same.atBeat = 7; CHECK(scoreInsertEx(t, n, 2, same) == SI_FULL && n == 2);
  ScoreEntry d; CHECK(d.bright == 255 && d.accentEvery == 0 && d.crc == 0);   // b= absent: the ceiling
}

// ---- section 10: the query reply and its route ----
static void test_query_reply() {
  char buf[CONTRACT_REPLY_MAX];
  struct { uint8_t route; char cls, unit; uint8_t cap, held, ok; uint16_t bpm; const char* want; } rows[] = {
    {   0, 'L', 'F',  32,   5, 1, 120, ";S0!LFq:ver=1.3,rel=1,cap=32,n=5,ok=1,bpm=120\r" },
    {   2, 'P', 'R',  12,   0, 0,   0, ";W2,;S0!PRq:ver=1.3,rel=1,cap=12,n=0,ok=0,bpm=0\r" },
    {  99, 'H', 'T', 255, 255, 2, 300, ";W99,;S0!HTq:ver=1.3,rel=1,cap=255,n=255,ok=2,bpm=300\r" },   // the longest there is
    { 200, 'L', 'F',   1,   1, 9, 5000, ";W99,;S0!LFq:ver=1.3,rel=1,cap=1,n=1,ok=2,bpm=999\r" },      // out-of-range inputs clamp
  };
  for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    const uint8_t n = contractFormatQuery(buf, sizeof buf, rows[i].route, rows[i].cls, rows[i].unit,
                                          rows[i].cap, rows[i].held, rows[i].ok, rows[i].bpm);
    CHECK(n == strlen(rows[i].want) && memcmp(buf, rows[i].want, n) == 0);
    CHECK(n <= CONTRACT_REPLY_MAX);
    const char* bang = strchr(rows[i].want, '!');                              // the contract part alone
    CHECK(strlen(bang) <= CONTRACT_QUERY_MAX);
  }
  CHECK(strlen(rows[2].want) == 54);
  CHECK(contractFormatQuery(buf, CONTRACT_REPLY_MAX - 1, 0, 'L', 'F', 32, 5, 1, 120) == 0);   // too small: nothing written
  ParsedContract p;
  CHECK(contractParse("LFQ:r=0", p) && contractQueryAnswerable(p) && p.params.route == 0);
  CHECK(contractParse("LRQ:r=3", p) && contractQueryAnswerable(p) && p.params.route == 3);
  CHECK(contractParse("LFQ", p) && !contractQueryAnswerable(p));               // no r=: not answered
  CHECK(contractParse("L*Q:r=0", p) && !contractQueryAnswerable(p));           // a wildcard is never answered
  CHECK(contractParse("*FQ:r=0", p) && !contractQueryAnswerable(p));
  CHECK(contractParse("**Q", p) && !contractQueryAnswerable(p));               // (it is still a valid line: a keepalive)
  CHECK(contractParse("LFA:r=0", p) && !contractQueryAnswerable(p));
  CHECK(!contractParse("LFq:ver=1.3,rel=1,cap=32,n=5,ok=1,bpm=120", p));       // a reply is never a command
}

// ---- S8 and section 12: one timing definition for flash / pulse / scan ----
static void test_v1_effect_timing() {
  // (CA1.5: 190 ms at the fastest, 20 ms over the strobe floor, so that a late frame does not put
  // the next ON edge inside the S9 gate's window)
  CHECK(FX_FLASH_MIN_STATE_MS == 190);
  CHECK(fxFlashHalfMs(255) == 190 && fxFlashHalfMs(0) == 445 && fxFlashHalfMs(128) == 317);
  CHECK(fxFlashOn(0, 255) && fxFlashOn(189, 255) && !fxFlashOn(190, 255) && !fxFlashOn(379, 255) && fxFlashOn(380, 255));
  for (int sp = 0; sp <= 255; sp++) CHECK(fxFlashHalfMs((uint8_t)sp) >= FX_FLASH_MIN_STATE_MS && fxFlashHalfMs((uint8_t)sp) > STROBE_MIN_STATE_MS);
  // S8, and CA1.5: the pulse cycle is never shorter than FX_CYCLE_MIN_MS (680 ms), at any speed.
  // It was 340 ms, which the S9 gate does not let through as drawn: the field may light again
  // only 340 ms after its PEAK.
  CHECK(FX_CYCLE_MIN_MS == 680);
  CHECK(fxPulsePeriodMs(0) == 2000 && fxPulsePeriodMs(255) == 680);
  for (int sp = 0; sp <= 255; sp++) CHECK(fxPulsePeriodMs((uint8_t)sp) >= FX_CYCLE_MIN_MS);
  CHECK(fxPulseBright(0, 0) == 0 && fxPulseBright(1000, 0) == 255 && fxPulseBright(500, 0) == 127);
  CHECK(fxPulseBright(2000, 0) == 0 && fxPulseBright(1500, 0) == 127);
  CHECK(fxPulseBright(340, 255) == 255 && fxPulseBright(680, 255) == 0 && fxPulseBright(170, 255) == 127);
  for (int sp = 0; sp <= 255; sp += 5)              // every period, odd ones included: never past 255
    for (uint32_t t = 0; t < 2100; t++) CHECK(fxPulseBright(t, (uint8_t)sp) <= 255);
  const int want[] = { 0, 1, 2, 3, 2, 1, 0, 1 };     // ping-pong on N=4, one step per 30 ms
  for (int i = 0; i < 8; i++) CHECK(fxScanPos(30u * (uint32_t)i, 255, 4, false) == want[i]);
  CHECK(fxScanPos(29, 255, 4, false) == 0 && fxScanPos(30, 255, 4, false) == 1);
  CHECK(fxScanPos(30u * 7u, 255, 6, true) == 1 && fxScanPos(30u * 5u, 255, 6, true) == 5);   // a ring wraps
  CHECK(fxScanPos(157u * 3u, 0, 8, false) == 3);     // s=0: 157 ms a step
  CHECK(fxScanPos(999, 255, 1, false) == 0 && fxScanPos(999, 255, 0, true) == 0 && fxScanPos(999, 255, 2, false) <= 1);
  for (uint32_t t = 0; t < 4000; t += 7) {
    CHECK(fxScanPos(t, 200, 28, false) >= 0 && fxScanPos(t, 200, 28, false) < 28);
    CHECK(fxScanPos(t, 200, 6, true) >= 0 && fxScanPos(t, 200, 6, true) < 6);
  }
}

// ---- the name tables: every effect name and every key, and nothing that only looks like one ----
static void test_name_tables() {
  static const struct { const char* name; ContractEffect e; } fx[] = {
    { "off", CE_OFF }, { "solid", CE_SOLID }, { "flash", CE_FLASH }, { "pulse", CE_PULSE },
    { "rainbow", CE_RAINBOW }, { "scan", CE_SCAN }, { "sparkle", CE_SPARKLE }, { "meter", CE_METER },
    { "comet", CE_COMET }, { "chase", CE_CHASE }, { "wipe", CE_WIPE }, { "gradient", CE_GRADIENT },
    { "colorcycle", CE_COLORCYCLE }, { "twinkle", CE_TWINKLE } };
  for (size_t i = 0; i < sizeof fx / sizeof fx[0]; i++) {
    int nc = 7;
    CHECK(_effectFromName(fx[i].name, strlen(fx[i].name), nc) == fx[i].e && nc == -1);
    char l[40];
    snprintf(l, sizeof l, "LFA:i=%s", fx[i].name);      CHECK(P(l).hasEffect && P(l).effect == fx[i].e);
    snprintf(l, sizeof l, "LFA:i=%sx", fx[i].name);     CHECK(!P(l).hasEffect);     // a longer word is not it
    snprintf(l, sizeof l, "LFA:i=%.*s", (int)strlen(fx[i].name) - 1, fx[i].name);
    CHECK(!P(l).hasEffect);                                                         // nor a shorter one
    snprintf(l, sizeof l, "LFA:ae=%s", fx[i].name);
    CHECK(P(l).hasAccentFx == accentEffectAllowed(fx[i].e));
  }
  int nc = 0;
  CHECK(_effectFromName("native:105", 10, nc) == CE_NATIVE && nc == 105);
  CHECK(_effectFromName("native:", 7, nc) == CE_NONE);          // no number: not native
  CHECK(_effectFromName("native", 6, nc) == CE_NONE && _effectFromName("nativE:5", 8, nc) == CE_NONE);
  CHECK(_effectFromName("", 0, nc) == CE_NONE && _effectFromName(",", 1, nc) == CE_NONE);
  CHECK(_effectFromName("off,solid", 9, nc) == CE_NONE);        // the table's own text is not a name
  CHECK(_effectFromName("solidXXXX", 5, nc) == CE_SOLID);       // a slice: only its first 5 bytes are read
  CHECK(!P("LFA:i=Solid").hasEffect && !P("LFA:i=").hasEffect && !P("LFA:i").hasEffect);

  // every key sets its own field and no other
  const ContractParams a = P("LFA:i=wipe,c=010203,s=4,d=5,b=6,m=7,at=8,am=2,an=3,ap=1");
  CHECK(a.hasEffect && a.hasColor && a.hasSpeed && a.hasDur && a.hasBright && a.hasBeatMod && a.hasAt && a.hasAm);
  CHECK(a.effect == CE_WIPE && a.color.b == 3 && a.speed == 4 && a.durMs == 5 && a.bright == 6 && a.beatMod == 7);
  CHECK(a.atBeat == 8 && a.accentMode == 2 && a.hasAn && a.an == 3 && a.hasAp && a.ap == 1);
  CHECK(!a.hasBpm && !a.hasPh && !a.hasBpb && !a.hasBeat && !a.hasLevel && !a.hasAccentFx && !a.hasWd && !a.hasN && !a.hasK && !a.hasRoute);
  const ContractParams b = P("**C:bpm=120,ph=40,bpb=3,beat=-8,v=9,ae=flash,ac=0a0b0c,ad=300,wd=15,n=5,k=00ff,r=2");
  CHECK(b.hasBpm && b.bpm == 120 && b.hasPh && b.phMs == 40 && b.hasBpb && b.bpb == 3 && b.hasBeat && b.beatAnchor == -8);
  CHECK(b.hasLevel && b.level == 9 && b.hasAccentFx && b.accentFx == CE_FLASH && b.hasAccentColor && b.accentColor.g == 0x0b);
  CHECK(b.hasAccentDur && b.accentDurMs == 300 && b.hasWd && b.wd == 15 && b.hasN && b.n == 5 && b.hasK && b.k == 0x00ff);
  CHECK(b.hasRoute && b.route == 2 && !b.hasEffect && !b.hasColor && !b.hasSpeed && !b.hasAt && !b.hasAm);
  // near-misses of keys are unknown keys: parsed past, nothing set
  const ContractParams z = P("LFA:I=solid,cc=010203,a=1,t=2,bp=3,bea=4,beats=5,w=6,kk=0000,ii=off,sw=50,=7");
  CHECK(!z.hasEffect && !z.hasColor && !z.hasAt && !z.hasAm && !z.hasBpm && !z.hasBpb && !z.hasBeat && !z.hasWd && !z.hasK);
  CHECK(_nameIndex(CONTRACT_PSTR("a,bb,ccc"), "bb", 2) == 1 && _nameIndex(CONTRACT_PSTR("a,bb,ccc"), "ccc", 3) == 2);
  CHECK(_nameIndex(CONTRACT_PSTR("a,bb,ccc"), "b", 1) == -1 && _nameIndex(CONTRACT_PSTR("a,bb,ccc"), "", 0) == -1);
  CHECK(_nameIndex(CONTRACT_PSTR("a,bb,ccc"), "a,bb", 4) == -1 && _nameIndex(CONTRACT_PSTR("a"), "a", 1) == 0);

  // a default ScoreEntry, wherever it is declared (the defaults come from its constructor)
  static ScoreEntry table[3];
  ScoreEntry local;
  const ScoreEntry* all[] = { &table[0], &table[2], &local };
  for (size_t i = 0; i < 3; i++) {
    const ScoreEntry& e = *all[i];
    CHECK(e.atBeat == 0 && e.effect == CE_SOLID && e.nativeCode == -1 && e.speed == 128 && e.beatMod == 0);
    CHECK(e.color.r == 0 && e.color.g == 0 && e.color.b == 0 && e.accentMode == 0 && e.accentFx == CE_NONE);
    CHECK(e.accentColor.r == 0 && e.accentDur10 == 18 && e.bright == 255 && e.accentEvery == 0 && e.crc == 0);
  }
  // S6: verb B is honoured only in show mode, and a reset clears it
  ContractUnitState s;
  CHECK(!contractBrightHonoured(s));
  contractOnShow(s, 160, 0); CHECK(contractBrightHonoured(s));
  s.bright = 64; contractOnStop(s, 160); CHECK(s.bright == 255 && contractBrightHonoured(s));   // X stays in show mode
  s.bright = 64; contractOnShow(s, 160, 0); CHECK(s.bright == 255);
  s.bright = 64; contractOnIdle(s, 160); CHECK(s.bright == 255 && !contractBrightHonoured(s));
}

// ---- S7: the forks' native allow-lists ----
static int countAllowed(bool (*allowed)(int)) { int n = 0; for (int c = -300; c <= 300; c++) if (allowed(c)) n++; return n; }
static void test_native_allow_psi_flthy() {
  // PSI Pro: modes 0..22 and 92, without Flash (2), Alarm (3) and Scream (5)
  CHECK(countAllowed(nativeAllowedPsi) == 21);
  for (int c = 0; c <= 22; c++) CHECK(nativeAllowedPsi(c) == (c != 2 && c != 3 && c != 5));
  CHECK(nativeAllowedPsi(92) && !nativeAllowedPsi(23) && !nativeAllowedPsi(91) && !nativeAllowedPsi(93));
  CHECK(!nativeAllowedPsi(-1) && !nativeAllowedPsi(32767) && !nativeAllowedPsi(-32767));
  // Flthy: Leia (1) and rainbow (6), nothing else
  CHECK(countAllowed(nativeAllowedFlthy) == 2 && nativeAllowedFlthy(1) && nativeAllowedFlthy(6));
  CHECK(!nativeAllowedFlthy(0) && !nativeAllowedFlthy(2) && !nativeAllowedFlthy(5) && !nativeAllowedFlthy(7) && !nativeAllowedFlthy(-1));
}

// ---- S7: the logics' native allow-list ----
static void test_native_allow_logics() {
  int allowed = 0;
  for (int c = -5; c <= 300; c++) if (nativeAllowedLogics(c)) allowed++;
  CHECK(allowed == 14 + 6 + 2);
  CHECK(!nativeAllowedLogics(3));                                    // Leia sets its own brightness, past the ceiling (S6)
  CHECK(nativeAllowedLogics(1) && nativeAllowedLogics(4) && nativeAllowedLogics(12));
  CHECK(nativeAllowedLogics(0) && nativeAllowedLogics(14) && nativeAllowedLogics(19) && nativeAllowedLogics(24));
  CHECK(nativeAllowedLogics(101) && nativeAllowedLogics(105));
  // S10, by onsets (test_native_s10.cpp is the count): failure, flash colour and fire pass it,
  // fire at a slower speed; three custom effects do not, or cannot be said to
  CHECK(nativeAllowedLogics(2) && nativeAllowedLogics(6) && nativeAllowedLogics(22) && nativeAllowedLogics(10));
  CHECK(!nativeAllowedLogics(102) && !nativeAllowedLogics(103) && !nativeAllowedLogics(104));
  // ...and the speed the contract starts each at: the engine's own (0) but for four
  for (int c = -5; c <= 300; c++)
    CHECK(nativeSpeedLogics(c) == (c == 20 ? 2 : (c == 4 || c == 19 || c == 22) ? 1 : 0));
  // the sequences whose whole field stays at the level the gate is told
  for (int c = -5; c <= 300; c++)
    CHECK(nativeSteadyLogics(c) == (c == 0 || c == 1 || c == 4 || c == 5 || c == 7 || c == 8 || (c >= 11 && c <= 14)));
  // S6: which of them take the board's brightness (b=, B)
  for (int c = -5; c <= 300; c++) {
    CHECK(nativeDimsLogics(c) == (nativeAllowedLogics(c) && c <= 14));
    CHECK(!nativeGatedLogics(c) && !nativeGatedPsi(c));              // S10: neither board has a gated native look
    CHECK(nativeGatedFlthy(c) == (c == 1));                          // the holoprojector's Leia is
    if (nativeGatedFlthy(c)) CHECK(nativeAllowedFlthy(c));
  }
  CHECK(!nativeAllowedLogics(15) && !nativeAllowedLogics(18));       // text effects need a message
  CHECK(!nativeAllowedLogics(25));                                   // one past the engine's effect table
  CHECK(!nativeAllowedLogics(99) && !nativeAllowedLogics(100));      // RANDOM; the SD-card bitmap
  CHECK(!nativeAllowedLogics(106) && !nativeAllowedLogics(-1) && !nativeAllowedLogics(32767));
}

// ---- S9: the field gate ----
// One observer of a unit's field: dark at or below `dark`, lit at or above `lit`. Counts its
// dark-to-lit onsets and keeps the shortest time between two.
struct FieldObserver {
  uint8_t dark, lit; bool isLit; uint32_t last; bool any; uint32_t minGap; uint32_t recent[4]; int worst;
  FieldObserver() : FieldObserver(0, 0) {}
  FieldObserver(uint8_t d, uint8_t l) : dark(d), lit(l), isLit(false), last(0), any(false), minGap(0xFFFFFFFFu), worst(0) {
    for (int i = 0; i < 4; i++) recent[i] = 0;
  }
  void see(uint8_t level, uint32_t now) {
    if (!isLit && level >= lit) {
      isLit = true;
      if (any && now - last < minGap) minGap = now - last;
      int w = 1;
      for (int i = 0; i < 3; i++) if (any && i < seen && now - recent[i] < 1000) w++;
      if (w > worst) worst = w;
      recent[2] = recent[1]; recent[1] = recent[0]; recent[0] = now; if (seen < 3) seen++;
      last = now; any = true;
    } else if (isLit && level <= dark) isLit = false;
  }
  int seen = 0;
};
static uint32_t g_gateRng = 0x2545F491u;
static uint32_t gateRnd(uint32_t n) { g_gateRng ^= g_gateRng << 13; g_gateRng ^= g_gateRng >> 17; g_gateRng ^= g_gateRng << 5; return g_gateRng % n; }
static void test_field_gate() {
  // the step is more than a tenth of the ceiling, and at least 2 levels
  CHECK(contractFieldStep(255) == 26 && contractFieldStep(160) == 17 && contractFieldStep(0) == 2 && contractFieldStep(3) == 2);
  CHECK(contractFieldStep(9) == 2 && contractFieldStep(10) == 2 && contractFieldStep(19) == 2 && contractFieldStep(20) == 3 && contractFieldStep(100) == 11);
  for (int c = 0; c < 256; c++) { const int st = contractFieldStep((uint8_t)c); CHECK(st >= 2 && st * 10 > c && (st == 2 || (st - 1) * 10 <= c)); }

  // a first onset, well after power-on, passes untouched; so does everything while the field stays lit
  ContractUnitState s; s.ceil = 200;
  CHECK(contractFieldGate(s, 0, 1000) == 0 && !s.fieldLit);
  CHECK(contractFieldGate(s, 200, 1010) == 200 && s.fieldLit && s.fieldOnsetMs == 1010);
  CHECK(contractFieldGate(s, 180, 1020) == 180 && s.fieldLit);              // a dip smaller than a step: still lit
  CHECK(contractFieldGate(s, 200, 1030) == 200 && s.fieldOnsetMs == 1010);  // back to the old peak: not a new one
  // a blackout is never delayed
  CHECK(contractFieldGate(s, 0, 1040) == 0 && !s.fieldLit);
  // ...what follows it is: no more than a step less one above the trough until 340 ms after the onset
  CHECK(contractFieldGate(s, 200, 1050) == 20);
  CHECK(contractFieldGate(s, 20, 1100) == 20);                              // under a step: a look's own texture passes
  CHECK(contractFieldGate(s, 21, 1200) == 20);
  CHECK(contractFieldGate(s, 200, 1349) == 20 && !s.fieldLit);
  CHECK(contractFieldGate(s, 200, 1350) == 200 && s.fieldLit && s.fieldOnsetMs == 1350);
  // the window runs from when the field last got BRIGHTER, not from when it began to
  CHECK(contractFieldGate(s, 0, 1700) == 0);
  CHECK(contractFieldGate(s, 60, 1710) == 60 && s.fieldLit && s.fieldOnsetMs == 1710);   // a slow rise begins...
  CHECK(contractFieldGate(s, 120, 1900) == 120 && s.fieldOnsetMs == 1900);
  CHECK(contractFieldGate(s, 200, 2100) == 200 && s.fieldOnsetMs == 2100);               // ...and ends here
  CHECK(contractFieldGate(s, 0, 2110) == 0);
  CHECK(contractFieldGate(s, 200, 2400) == 20);                                          // 690 ms after it began, 300 after it ended
  CHECK(contractFieldGate(s, 200, 2440) == 200);
  // a fall to a trough that is not dark, and back: the same rule, from that trough
  CHECK(contractFieldGate(s, 120, 2450) == 120 && !s.fieldLit && s.fieldRef == 120);
  CHECK(contractFieldGate(s, 200, 2460) == 140);
  CHECK(contractFieldGate(s, 100, 2470) == 100 && s.fieldRef == 100);                    // the trough follows the level down
  CHECK(contractFieldGate(s, 200, 2480) == 120);
  // the gate's state is a safety anchor: no reset clears it
  contractFieldGate(s, 200, 3000); contractFieldGate(s, 0, 3010);
  contractOnStop(s, 200);     CHECK(contractFieldGate(s, 200, 3100) == 20);
  contractOnIdle(s, 200);     CHECK(contractFieldGate(s, 200, 3200) == 20);
  contractOnShow(s, 200, 0);  CHECK(contractFieldGate(s, 200, 3300) == 20);
  CHECK(contractFieldGate(s, 200, 3340) == 200);
  // millis() wrapping changes nothing
  ContractUnitState w; w.ceil = 255;
  CHECK(contractFieldGate(w, 255, 0xFFFFFF00u) == 255);
  CHECK(contractFieldGate(w, 0, 0xFFFFFF80u) == 0);
  CHECK(contractFieldGate(w, 255, 0x00000010u) == 25);          // 272 ms later, across the wrap
  CHECK(contractFieldGate(w, 255, 0x00000054u) == 255);         // 340 ms later
  // the first 340 ms after power-on are held (the anchor starts at 0): the fail-safe direction
  ContractUnitState boot; boot.ceil = 255;
  CHECK(contractFieldGate(boot, 255, 100) == 25 && contractFieldGate(boot, 255, 340) == 255);

  // a native look takes the LEDs: the fork ASKS first (no state change), and TELLS the gate when
  // the look is really on the LEDs
  ContractUnitState n; n.ceil = 160;
  CHECK(contractFieldMayLight(n, 1000) && !n.fieldLit && n.fieldOnsetMs == 0);      // asking changes nothing
  CHECK(contractFieldGate(n, 160, 1010) == 160 && n.fieldLit && n.fieldRef == 160 && n.fieldOnsetMs == 1010);   // told: it lit at 1010
  CHECK(contractFieldMayLight(n, 1100) && n.fieldOnsetMs == 1010);          // lit already, at the ceiling: nothing new
  CHECK(contractFieldGate(n, 160, 1110) == 160 && n.fieldOnsetMs == 1010);  // a contract look as bright takes over: no onset
  CHECK(contractFieldGate(n, 0, 1120) == 0);                                // the contract blacks it out
  CHECK(!contractFieldMayLight(n, 1130) && !n.fieldLit);                    // back to native now would be an onset 120 ms after the last
  CHECK(!contractFieldMayLight(n, 1349) && contractFieldMayLight(n, 1350));
  CHECK(contractFieldAllowed(n, 160, 1349) == 16 && contractFieldAllowed(n, 160, 1350) == 160);
  CHECK(contractFieldAllowed(n, 16, 1130) == 16 && contractFieldAllowed(n, 17, 1130) == 16);
  CHECK(contractFieldGate(n, 160, 1360) == 160 && n.fieldOnsetMs == 1360);
  CHECK(contractFieldGate(n, 0, 1370) == 0 && !n.fieldLit && n.fieldRef == 0);   // a native lights-out is level 0: dark at once
  CHECK(contractFieldGate(n, 60, 1380) == 16);                              // (dimmed, but still an onset from dark)
  CHECK(contractFieldGate(n, 100, 1710) == 100 && contractFieldGate(n, 50, 1720) == 50 && !n.fieldLit);   // lit, then dimmed by a step
  CHECK(!contractFieldMayLight(n, 1800));                                   // a native look at the ceiling is an onset from there...
  CHECK(contractFieldAllowed(n, 66, 1800) == 66 && contractFieldAllowed(n, 67, 1800) == 66);   // ...one within a step of the trough is not
  // light nobody asked the gate about (a button): recorded, never refused, and the contract's
  // next onset keeps 340 ms from it
  ContractUnitState f; f.ceil = 200;
  contractFieldGate(f, 200, 1000); contractFieldGate(f, 0, 1010);
  CHECK(contractFieldGate(f, 200, 1100) == 20 && !f.fieldLit);              // the contract's own relight is held...
  contractFieldForced(f, 200, 1100);
  CHECK(f.fieldLit && f.fieldRef == 200 && f.fieldOnsetMs == 1100);         // ...a button's is on the LEDs: recorded
  CHECK(contractFieldGate(f, 0, 1110) == 0 && contractFieldGate(f, 200, 1400) == 20 && contractFieldGate(f, 200, 1440) == 200);
  contractFieldForced(f, 200, 1500); CHECK(f.fieldOnsetMs == 1440);         // lit already, no brighter: nothing new
  contractFieldForced(f, 0, 1600);   CHECK(!f.fieldLit && f.fieldRef == 0); // a button's lights-out
  // asking is exactly the gate's answer, for any state, level and time
  for (int i = 0; i < 20000; i++) {
    ContractUnitState a; a.ceil = (uint8_t)gateRnd(256); a.fieldLit = gateRnd(2) != 0; a.fieldRef = (uint8_t)gateRnd(256);
    a.fieldOnsetMs = gateRnd(2000);
    const uint8_t level = (uint8_t)gateRnd(256); const uint32_t now = a.fieldOnsetMs + gateRnd(700);
    ContractUnitState b2 = a;
    const uint8_t asked = contractFieldAllowed(a, level, now);
    CHECK(asked == contractFieldGate(b2, level, now));
  }

  // THE PROPERTY, on the gate alone. Any level sequence at all goes in, at any frame times; the
  // gate's output is watched by observers with different ideas of dark and lit, each at least a
  // step apart. For every one of them: two onsets are never less than 340 ms apart (so never
  // more than three in a second); the output never exceeds the input; a fall is never delayed.
  for (int run = 0; run < 400; run++) {
    ContractUnitState g; g.ceil = (uint8_t)(run % 4 == 0 ? 255 : 20 + gateRnd(236));
    const uint8_t c = g.ceil, step = contractFieldStep(c);
    FieldObserver obs[5] = { FieldObserver((uint8_t)(c / 10), (uint8_t)(c / 2)), FieldObserver((uint8_t)(c * 19 / 100), (uint8_t)(c * 3 / 4)),
                             FieldObserver(0, step), FieldObserver((uint8_t)(c - step), c),
                             FieldObserver((uint8_t)gateRnd(c / 2), 0) };
    obs[4].lit = (uint8_t)(obs[4].dark + step + gateRnd((uint32_t)(c - obs[4].dark - step) + 1u));
    uint32_t now = 1000u + gateRnd(5000); uint8_t level = 0, prevOut = 0;
    const uint32_t kind = gateRnd(4);
    for (int f = 0; f < 3000; f++) {
      now += (gateRnd(20) == 0) ? 10u + gateRnd(90) : (kind == 3 ? 1u + gateRnd(12) : 10u);
      switch (kind) {
        case 0: if (gateRnd(3) == 0) level = (uint8_t)(gateRnd(2) ? c : 0); break;                 // hostile: full swings
        case 1: level = (uint8_t)gateRnd((uint32_t)c + 1u); break;                                 // noise
        case 2: { const int d = (int)gateRnd(41) - 20, v = (int)level + d;                         // a random walk, with jumps
                  level = (uint8_t)(v < 0 ? 0 : (v > c ? c : v)); if (gateRnd(25) == 0) level = (uint8_t)(gateRnd(2) ? c : 0); break; }
        default: level = (uint8_t)((f / (2 + (int)gateRnd(3))) % 2 ? c : gateRnd(step)); break;    // a strobe with a dirty floor
      }
      const uint8_t out = contractFieldGate(g, level, now);
      CHECK(out <= level);
      if (level <= prevOut) CHECK(out == level);                  // going down (or staying): never delayed, never altered
      prevOut = out;
      for (FieldObserver& o : obs) o.see(out, now);
    }
    for (FieldObserver& o : obs) CHECK(o.minGap >= 2 * STROBE_MIN_STATE_MS && o.worst <= 3);
  }
}

// ---- the overlay: one expiry rule, and its own brightness ----
static void test_overlay_helpers() {
  ContractUnitState s;
  CHECK(s.pulseBright == 255 && !contractOverlayUp(s, 5));
  s.pulseActive = true; s.pulseStartMs = 1000; s.pulseDurMs = 170;
  CHECK(contractOverlayUp(s, 1000) && contractOverlayUp(s, 1169) && s.pulseActive);
  CHECK(!contractOverlayUp(s, 1170) && !s.pulseActive);
  s.pulseActive = true; s.pulseStartMs = 0xFFFFFFF0u; s.pulseDurMs = 100;   // across the millis() wrap
  CHECK(contractOverlayUp(s, 0x00000010u) && !contractOverlayUp(s, 0x00000054u));
  s.pulseBright = 30; contractOnStop(s, 255); CHECK(s.pulseBright == 255); // a reset puts it back
}

// ---- core revision CA1.4: two measures, the look gate, native looks ----
static void test_ca14() {
  // Rec.709 luminance, in integers: white is itself, the three primaries add up to it within 1
  CHECK(contractLuminance(255, 255, 255) == 255 && contractLuminance(0, 0, 0) == 0);
  CHECK(contractLuminance(255, 0, 0) == 53 && contractLuminance(0, 255, 0) == 182 && contractLuminance(0, 0, 255) == 18);
  CHECK(contractLuminance(160, 160, 160) == 160 && contractLuminance(1, 1, 1) == 1);

  // TWO MEASURES. White to blue at one level: the brightest channel never moves, the luminance
  // falls to a fourteenth. Blue back to white is then an ONSET, in luminance, and waits.
  ContractUnitState s; s.ceil = 200;
  const uint8_t st200 = contractFieldStep(200), st160 = contractFieldStep(160);
  CHECK(contractFieldScale(s, 200, 200, 1000) == 255 && s.fieldLit && s.fieldLitY && s.fieldOnsetMs == 1000);
  CHECK(contractFieldScale(s, 200, 14, 1050) == 255 && s.fieldLit && !s.fieldLitY && s.fieldRefY == 14);   // a fall: never delayed
  const uint8_t held = contractFieldScale(s, 200, 200, 1100);        // white again, 100 ms after the field lit
  CHECK(held == (uint8_t)((14 + st200 - 1) * 255 / 200));            // luminance held a step less one above its trough
  CHECK(!s.fieldLitY && s.fieldOnsetMs == 1000);
  // ...and BOTH measures remember what the unit showed, not what was drawn: the level is a fall
  CHECK(!s.fieldLit && s.fieldRef == (uint8_t)(200 * held / 255));
  CHECK(contractFieldScale(s, 200, 200, 1339) == held);
  CHECK(contractFieldScale(s, 200, 200, 1340) == 255 && s.fieldLit && s.fieldLitY && s.fieldOnsetMs == 1340);
  // ONE RISE IS ONE ONSET (CA1.6). The level lights first; luminance lighting later in the same
  // rise is not a second onset, however long after.
  ContractUnitState t; t.ceil = 200;
  contractFieldScale(t, 100, 10, 1000);                              // blue, half bright: lit in level only
  CHECK(t.fieldLit && !t.fieldLitY && t.fieldOnsetMs == 1000 && t.fieldRise);
  CHECK(contractFieldScale(t, 100, 100, 1200) == 255 && t.fieldLitY && t.fieldOnsetMs == 1200 && t.fieldRise);
  // ...a fall in either measure ends the rise: blue again (luminance falls), and white after
  // that is a second onset in luminance. It waits for the shared time.
  CHECK(contractFieldScale(t, 100, 10, 1250) == 255 && !t.fieldLitY && !t.fieldRise);
  CHECK(contractFieldScale(t, 100, 100, 1300) < 255 && !t.fieldLitY);
  CHECK(contractFieldScale(t, 100, 100, 1539) < 255 && contractFieldScale(t, 100, 100, 1540) == 255 && t.fieldRise);
  // A coloured look that fades in (the defect CA1.6 fixes): 116530 from black to full in 100
  // frames. The brightest channel crosses the step at a quarter of the way, luminance at a third.
  // No frame is held, and the look gate's time follows the rise to its last frame.
  { ContractUnitState f; f.ceil = 255; f.fieldOnsetMs = 0;
    int heldFrames = 0;
    for (int i = 0; i <= 100; i++) {
      const uint8_t r = (uint8_t)(0x11 * i / 100), g = (uint8_t)(0x65 * i / 100), b = (uint8_t)(0x30 * i / 100);
      if (contractFieldScale(f, g, contractLuminance(r, g, b), 1000u + 10u * (uint32_t)i) != 255) heldFrames++;
    }
    CHECK(heldFrames == 0 && f.fieldLit && f.fieldLitY && f.fieldRise && f.fieldOnsetMs == 2000 && f.lookMs == 2000);
    // the same fade begun inside the window is held as ever, in the measure that lights first
    ContractUnitState h; h.ceil = 255;
    contractFieldScale(h, 255, 255, 900); contractFieldScale(h, 0, 0, 950);
    heldFrames = 0;
    for (int i = 0; i <= 100; i++) {
      const uint8_t r = (uint8_t)(0x11 * i / 100), g = (uint8_t)(0x65 * i / 100), b = (uint8_t)(0x30 * i / 100);
      const uint32_t now = 1000u + 2u * (uint32_t)i;
      if (contractFieldScale(h, g, contractLuminance(r, g, b), now) != 255) { heldFrames++; CHECK(now < 1240 && !h.fieldLit && !h.fieldLitY); }
    }
    CHECK(heldFrames > 0); }
  // The case the shared time was made for still holds: white by a colour change, X, white again.
  { ContractUnitState w; w.ceil = 255;
    CHECK(contractFieldScale(w, 255, 18, 1000) == 255 && w.fieldRise);                     // blue: lit in level only
    CHECK(contractFieldScale(w, 255, 255, 2000) == 255 && w.fieldLitY && w.fieldOnsetMs == 2000);   // white: luminance lights
    CHECK(contractFieldScale(w, 0, 0, 2010) == 255 && !w.fieldLit && !w.fieldLitY && !w.fieldRise);  // X
    CHECK(contractFieldScale(w, 255, 255, 2030) < 255 && !w.fieldLit && !w.fieldLitY);     // held
    CHECK(contractFieldScale(w, 255, 255, 2339) < 255 && contractFieldScale(w, 255, 255, 2340) == 255); }
  // A frame in which one measure lights and the other falls begins no rise, whichever of the two
  // is which: two looks that trade places cannot pass the rise to each other.
  { ContractUnitState p; p.ceil = 255;
    contractFieldScale(p, 255, 217, 1000);                                                 // (255, 229, 0): both lit
    contractFieldScale(p, 229, 212, 2000);                                                 // (229, 229, 0): the level falls a step
    CHECK(!p.fieldLit && p.fieldLitY && !p.fieldRise);
    CHECK(contractFieldScale(p, 255, 182, 3000) == 255 && p.fieldLit && !p.fieldLitY && !p.fieldRise);   // level lights, luminance falls
    CHECK(contractFieldScale(p, 255, 217, 3010) < 255 && !p.fieldLitY);                    // luminance may not light on it
    ContractUnitState q; q.ceil = 255;
    contractFieldScale(q, 255, 217, 1000); contractFieldScale(q, 255, 182, 2000);          // luminance falls
    CHECK(q.fieldLit && !q.fieldLitY);
    CHECK(contractFieldScale(q, 229, 212, 3000) == 255 && !q.fieldLit && q.fieldLitY && !q.fieldRise);   // luminance lights, level falls
    CHECK(contractFieldScale(q, 255, 212, 3010) < 255 && !q.fieldLit); }                   // the level may not light on it
  // contractFieldAllowed() is for light a fork cannot scale: it waits for the window, rise or no rise
  { ContractUnitState a; a.ceil = 255;
    contractFieldScale(a, 255, 18, 1000);
    CHECK(a.fieldRise && contractFieldAllowed(a, 255, 1100) < 255 && contractFieldAllowed(a, 255, 1340) == 255); }
  // the rise is a safety anchor like the rest of the gate, and a look whose light is not known ends it
  { ContractUnitState a; a.ceil = 255;
    contractFieldScale(a, 255, 18, 1000);
    contractOnStop(a, 255); contractOnShow(a, 255, 0); contractOnIdle(a, 255); CHECK(a.fieldRise && a.fieldLit);
    contractNativeEnded(a, false, 1500); CHECK(!a.fieldRise && !a.fieldLit && !a.fieldLitY); }
  // a measure at 0 does not limit, and nothing is ever scaled up
  ContractUnitState z; z.ceil = 200;
  CHECK(contractFieldScale(z, 0, 0, 1000) == 255 && contractFieldScale(z, 150, 0, 1010) == 255 && z.fieldRefY == 0);
  // contractFieldGate() is the same rule with lum = level, for a fork that shows the allowed
  // level itself: a scaled frame rounds down, so it is never above what the gate allows
  for (int i = 0; i < 20000; i++) {
    ContractUnitState a; a.ceil = (uint8_t)gateRnd(256); a.fieldLit = gateRnd(2) != 0; a.fieldRef = (uint8_t)gateRnd(256);
    a.fieldLitY = gateRnd(2) != 0; a.fieldRefY = (uint8_t)gateRnd(256); a.fieldOnsetMs = gateRnd(2000);
    ContractUnitState b = a;
    const uint8_t level = (uint8_t)gateRnd(256); const uint32_t now = a.fieldOnsetMs + gateRnd(700);
    const uint8_t allowed = contractFieldGate(a, level, now), scale = contractFieldScale(b, level, level, now);
    CHECK(allowed <= level && (uint8_t)(((uint16_t)level * scale) / 255u) <= allowed);
    CHECK(scale == 255 ? allowed == level : allowed < level);
    if (scale == 255) CHECK(a.fieldLit == b.fieldLit && a.fieldLitY == b.fieldLitY && a.fieldOnsetMs == b.fieldOnsetMs && a.fieldRef == b.fieldRef);
  }
  // THE PROPERTY, on both measures: any frames at all (a level, and a luminance no higher), any
  // times. What the unit shows is watched in level and in luminance: two onsets are never less
  // than 340 ms apart in either, and a fall is never delayed.
  for (int run = 0; run < 300; run++) {
    ContractUnitState g; g.ceil = (uint8_t)(run % 4 == 0 ? 255 : 30 + gateRnd(226));
    const uint8_t c = g.ceil, step = contractFieldStep(c);
    FieldObserver oL[3] = { FieldObserver((uint8_t)(c / 10), (uint8_t)(c / 2)), FieldObserver(0, step), FieldObserver((uint8_t)(c - step), c) };
    FieldObserver oY[3] = { FieldObserver((uint8_t)(c / 10), (uint8_t)(c / 2)), FieldObserver(0, step), FieldObserver((uint8_t)(c - step), c) };
    uint32_t now = 1000u + gateRnd(5000); uint8_t level = 0, lum = 0;
    for (int f = 0; f < 3000; f++) {
      now += (gateRnd(20) == 0) ? 10u + gateRnd(90) : 10u;
      if (gateRnd(3) == 0) level = (uint8_t)(gateRnd(4) ? gateRnd((uint32_t)c + 1u) : (gateRnd(2) ? c : 0));
      if (gateRnd(3) == 0) lum = (uint8_t)gateRnd(256);
      if (lum > level) lum = level;                                 // a field's luminance is never above its brightest channel
      const uint8_t scale = contractFieldScale(g, level, lum, now);
      const uint8_t outL = (uint8_t)(((uint16_t)level * scale) / 255u), outY = (uint8_t)(((uint16_t)lum * scale) / 255u);
      for (FieldObserver& o : oL) o.see(outL, now);
      for (FieldObserver& o : oY) o.see(outY, now);
    }
    for (FieldObserver& o : oL) CHECK(o.minGap >= 2 * STROBE_MIN_STATE_MS && o.worst <= 3);
    for (FieldObserver& o : oY) CHECK(o.minGap >= 2 * STROBE_MIN_STATE_MS && o.worst <= 3);
  }

  // THE SAME PROPERTY, ON COLOURS THAT FADE (CA1.6). Frames of real colours, where the level and
  // the luminance move together but cross the step at different times: fades up and down at
  // random rates, cuts to black and to full, changes of colour in the middle of a fade. Every
  // narrow observer of each measure (dark below d, lit from d + step, for every d the step
  // apart or half of it) sees no two onsets less than 340 ms apart.
  { static const uint8_t kRgb[][3] = { { 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 0x11, 0x65, 0x30 },
                                      { 0x22, 0xc5, 0x5e }, { 255, 229, 0 }, { 229, 229, 0 }, { 255, 180, 0 }, { 16, 16, 16 } };
    const int nRgb = (int)(sizeof kRgb / sizeof kRgb[0]);
    long heldFades = 0;                                    // clean fades tried
    for (int run = 0; run < 400; run++) {
      ContractUnitState g; g.ceil = (uint8_t)(run % 3 == 0 ? 255 : 30 + gateRnd(226));
      const uint8_t c = g.ceil, step = contractFieldStep(c);
      FieldObserver oL[24], oY[24];
      int nObs = 0;
      for (int d = 0; d + step <= c && nObs < 24; d += (step + 1) / 2, nObs++) { oL[nObs] = FieldObserver((uint8_t)d, (uint8_t)(d + step)); oY[nObs] = oL[nObs]; }
      uint32_t now = 1000u + gateRnd(5000);
      int k = 0, target = 0, rate = 4, col = (int)gateRnd((uint32_t)nRgb);       // brightness 0..255 of the colour, fading to target
      bool fading = false, anyHeld = false;
      for (int f = 0; f < 4000; f++) {
        now += (gateRnd(20) == 0) ? 10u + gateRnd(60) : 10u;
        if (!fading) {
          if (gateRnd(40) == 0) { target = gateRnd(3) ? (int)gateRnd(256) : (gateRnd(2) ? 255 : 0); rate = 1 + (int)gateRnd(12); }
          if (gateRnd(60) == 0) { k = target = gateRnd(2) ? 255 : 0; }                        // a cut
          if (gateRnd(90) == 0) col = (int)gateRnd((uint32_t)nRgb);                           // another colour
          // now and then: a fade from black that starts with the window open, left alone to its end
          if (k == 0 && target == 0 && gateRnd(25) == 0 && (uint32_t)(now - g.fieldOnsetMs) >= 340u) { fading = true; anyHeld = false; target = 255; rate = 1 + (int)gateRnd(12); }
        }
        if (k < target) k = k + rate > target ? target : k + rate; else if (k > target) k = k - rate < target ? target : k - rate;
        const uint8_t r = (uint8_t)(kRgb[col][0] * k * c / (255 * 255)), gg = (uint8_t)(kRgb[col][1] * k * c / (255 * 255)), b = (uint8_t)(kRgb[col][2] * k * c / (255 * 255));
        const uint8_t level = r > gg ? (r > b ? r : b) : (gg > b ? gg : b), lum = contractLuminance(r, gg, b);
        const ContractUnitState before = g;
        const uint8_t scale = contractFieldScale(g, level, lum, now);
        if (scale != 255) anyHeld = true;
        CHECK(scale == 255 || level > before.fieldRef || lum > before.fieldRefY);         // a fall is never held
        const uint8_t outL = (uint8_t)(((uint16_t)level * scale) / 255u), outY = (uint8_t)(((uint16_t)lum * scale) / 255u);
        for (int i = 0; i < nObs; i++) { oL[i].see(outL, now); oY[i].see(outY, now); }
        if (fading && k == 255) { CHECK(!anyHeld); heldFades++; fading = false; }         // the defect CA1.6 fixes: no frame of it was held
      }
      for (int i = 0; i < nObs; i++) CHECK(oL[i].minGap >= 2 * STROBE_MIN_STATE_MS && oY[i].minGap >= 2 * STROBE_MIN_STATE_MS);
    }
    CHECK(heldFades > 500); }                                                             // (clean fades were in fact tried)

  // THE LOOK GATE. A look is its effect and its colour; a native look is its code.
  const ContractRGB red = { 255, 0, 0 }, blue = { 0, 0, 255 };
  const uint32_t sigRed = contractLookSig(CE_SOLID, red), sigBlue = contractLookSig(CE_SOLID, blue);
  CHECK(sigRed != sigBlue && sigRed != contractLookSig(CE_PULSE, red) && sigRed != 0);
  CHECK(contractLookSigNative(4) != contractLookSigNative(0) && contractLookSigNative(0) != 0);
  ContractUnitState k; k.ceil = 200;
  CHECK(contractLookGate(k, sigRed, 1000) && k.lookSig == sigRed && k.lookMs == 1000);
  CHECK(contractLookGate(k, sigRed, 1010));                          // the look that is up: always
  CHECK(!contractLookAllowed(k, sigBlue, 1169) && !contractLookGate(k, sigBlue, 1169) && k.lookSig == sigRed);
  CHECK(contractLookAllowed(k, sigBlue, 1170) && contractLookGate(k, sigBlue, 1170) && k.lookMs == 1170);
  // a look ARRIVES for as long as the field lights or gets brighter: a fade-in keeps the next
  // change of look away until 170 ms after it stopped rising
  contractFieldScale(k, 20, 2, 1180); CHECK(k.lookMs == 1170);                       // (under a step: texture, nothing new)
  contractFieldScale(k, 60, 6, 1300); CHECK(k.fieldLit && k.lookMs == 1300);         // the field lit: the look arrives
  contractFieldScale(k, 60, 6, 1350); CHECK(k.lookMs == 1300);                       // no brighter: nothing new
  contractFieldScale(k, 90, 9, 1400); CHECK(k.lookMs == 1400 && k.fieldOnsetMs == 1400);   // still rising
  CHECK(!contractLookGate(k, sigRed, 1500) && contractLookGate(k, sigRed, 1570));
  // a look nobody asked about (a button's native sequence) is recorded, never refused
  contractLookForced(k, contractLookSigNative(4), 1580);
  CHECK(k.lookSig == contractLookSigNative(4) && k.lookMs == 1580);
  contractLookForced(k, contractLookSigNative(4), 1700); CHECK(k.lookMs == 1580);    // the same one: nothing new
  // the look gate's state is a safety anchor too
  contractOnStop(k, 200); CHECK(k.lookSig == contractLookSigNative(4) && k.lookMs == 1580);
  contractOnShow(k, 200, 0); contractOnIdle(k, 200); CHECK(k.lookSig == contractLookSigNative(4) && k.lookMs == 1580);

  // CA1.6: a frame whose brightest LED is under 4 levels shows no colour and is not a look: it
  // is never held, and it does not take the gate. The first frame of 4 levels or more does.
  { ContractUnitState d; d.ceil = 255;
    CHECK(LOOK_MIN_LEVEL == 4 && contractLookGate(d, sigRed, 1000));
    CHECK(contractLookAllowedAt(d, sigBlue, 3, 1010) && contractLookGateAt(d, sigBlue, 3, 1010));   // too dim: drawn,
    CHECK(d.lookSig == sigRed && d.lookMs == 1000);                                                 // and the gate is where it was
    CHECK(!contractLookAllowedAt(d, sigBlue, 4, 1100) && !contractLookGateAt(d, sigBlue, 4, 1100) && d.lookSig == sigRed);
    CHECK(contractLookAllowedAt(d, sigBlue, 4, 1170) && d.lookSig == sigRed);                       // (the ask takes nothing)
    CHECK(contractLookGateAt(d, sigBlue, 200, 1200) && d.lookSig == sigBlue && d.lookMs == 1200);   // seen: from here
    CHECK(contractLookGateAt(d, sigRed, 0, 1210) && d.lookSig == sigBlue);                          // black is the dimmest of them
    CHECK(!contractLookGateAt(d, sigRed, 255, 1369) && contractLookGateAt(d, sigRed, 255, 1370)); }
  // CA1.6: a native look told with TWO figures. A rainbow at three quarters by level and a third
  // by luminance, then a contract frame that is dimmer by level and brighter by luminance: with
  // one figure that was no onset; with two it is, and it waits.
  { ContractUnitState one, two; one.ceil = two.ceil = 255;
    contractNativeStarted(one, 6, 191, 1000); contractNativeStarted(two, 6, 191, 84, 1000);
    CHECK(two.fieldLit && two.fieldRef == 191 && two.fieldLitY && two.fieldRefY == 84 && two.fieldOnsetMs == 1000);
    CHECK(two.lookSig == contractLookSigNative(6) && one.fieldRefY == 191);
    CHECK(contractFieldAsk(one, 128, 128, 1100) == 255);                 // one figure: the gate saw luminance fall
    CHECK(contractFieldAsk(two, 128, 128, 1100) == 255);                 // two: 84 to 128 is a rise while lit: the onset going on
    contractNativeLevel(two, 100, 20, 1200);                             // the fork dimmed it: both fall
    CHECK(!two.fieldLit && !two.fieldLitY && two.fieldRef == 100 && two.fieldRefY == 20);
    CHECK(contractFieldAsk(two, 100, 100, 1250) < 255 && contractFieldAsk(two, 100, 100, 1340) == 255);   // white at that level: an onset in luminance
    ContractUnitState dark; dark.ceil = 255;
    contractNativeStarted(dark, 14, 0, 0, 1000); CHECK(dark.lookSig == 0 && dark.fieldOnsetMs == 0); }

  // S11: re-applying the same look does not restart it
  ContractUnitState same; same.base = CB_LOOK; same.effect = CE_GRADIENT; same.color = red; same.speed = 90;
  CHECK(contractLookSame(same, CE_GRADIENT, -1, red, 90));
  CHECK(!contractLookSame(same, CE_GRADIENT, -1, blue, 90) && !contractLookSame(same, CE_GRADIENT, -1, red, 91));
  CHECK(!contractLookSame(same, CE_SOLID, -1, red, 90) && !contractLookSame(same, CE_NATIVE, 4, red, 90));
  same.base = CB_DARK; CHECK(!contractLookSame(same, CE_GRADIENT, -1, red, 90));     // nothing was showing: it starts
  same.base = CB_NONE; CHECK(!contractLookSame(same, CE_GRADIENT, -1, red, 90));
  same.base = CB_NATIVE; same.effect = CE_NATIVE; same.nativeCode = 4;
  CHECK(contractLookSame(same, CE_NATIVE, 4, red, 90) && !contractLookSame(same, CE_NATIVE, 7, red, 90));

  // A SECTION is applied 170 ms after the unit's last look change at the earliest, and is one
  ContractUnitState c2;
  CHECK(contractSectionMayApply(c2, 1000) && c2.lastApplyMs == 1000);
  CHECK(!contractSectionMayApply(c2, 1169) && c2.lastApplyMs == 1000);
  CHECK(contractSectionMayApply(c2, 1170) && c2.lastApplyMs == 1170);
  ContractLookPatch lp; lp.has = LP_COLOR; lp.color = red;
  CHECK(!liveLookOffer(c2, lp, 1200));                               // ...the same anchor a live A uses: held
  ContractLookPatch due;
  CHECK(!liveLookDue(c2, 1339, due) && liveLookDue(c2, 1340, due) && !contractSectionMayApply(c2, 1400));

  // S10, NATIVE LOOKS THAT RUN AS STOCK: ask, tell that it started, tell its level, tell that it ended
  ContractUnitState n; n.ceil = 160;
  contractFieldGate(n, 160, 1000); contractFieldGate(n, 0, 1010);   // a contract look, then dark
  CHECK(!contractNativeMayStart(n, 4, 160, 1100));                  // an onset 100 ms after the last: wait
  CHECK(contractNativeMayStart(n, 14, 0, 1100));                    // a native blackout never waits
  CHECK(contractNativeMayStart(n, 4, 160, 1340));
  contractNativeStarted(n, 4, 160, 1350);
  CHECK(n.fieldLit && n.fieldRef == 160 && n.fieldOnsetMs == 1350 && n.lookSig == contractLookSigNative(4) && n.lookMs == 1350);
  // a native look may clear the unit and light it at once: its start is an onset whatever the
  // unit shows now, lit or dark, and so is the hand-back to the board's own look
  CHECK(!contractNativeMayStart(n, 9, 160, 1400) && !contractNativeMayStart(n, 9, 160, 1689));
  CHECK(contractNativeMayStart(n, 9, 160, 1690) && contractNativeMayStart(n, 0, 160, 1690));
  CHECK(!contractNativeMayStart(n, 4, 160, 1400) && contractNativeMayStart(n, 14, 0, 1400));
  ContractUnitState q = n; contractLookForced(q, contractLookSigNative(7), 1600);     // a change of look that lit nothing
  CHECK(!contractNativeMayStart(q, 9, 160, 1700) && contractNativeMayStart(q, 9, 160, 1770));   // S4 still counts
  contractNativeLevel(n, 60, 1600); CHECK(!n.fieldLit && n.fieldRef == 60);          // the fork dimmed it: at once
  contractNativeLevel(n, 160, 1650); CHECK(n.fieldLit && n.fieldRef == 160 && n.fieldOnsetMs == 1650);   // told, never refused
  // it ends: the fork draws again. What it lights next keeps 340 ms from that frame...
  contractNativeEnded(n, true, 2000);
  CHECK(n.fieldOnsetMs == 2000 && n.fieldLit && n.fieldRef == 160);
  CHECK(contractFieldGate(n, 160, 2010) == 160);                    // ...a look as bright as the native one is no onset
  CHECK(contractFieldGate(n, 0, 2020) == 0 && contractFieldGate(n, 160, 2030) == st160 - 1 && contractFieldGate(n, 160, 2340) == 160);
  // a look that is NOT on the fork's list (a button's): the field is taken as dark and just lit
  contractNativeStarted(n, 6, 160, 3000);
  contractNativeEnded(n, false, 3200);
  CHECK(!n.fieldLit && n.fieldRef == 0 && n.fieldOnsetMs == 3200);
  CHECK(contractFieldGate(n, 160, 3210) == st160 - 1 && contractFieldGate(n, 160, 3540) == 160);
  // a native blackout tells the gate the field is dark, and changes no look
  const uint32_t sigBefore = n.lookSig;
  contractNativeStarted(n, 14, 0, 4000); CHECK(!n.fieldLit && n.fieldRef == 0 && n.lookSig == sigBefore);
}

// ---- CA1.6: verb L. A higher level at most once in 340 ms; a fall at once ----
// One pass of a fork's loop with an L line in it: the offer, then the due.
static bool levelLine(ContractUnitState& s, uint8_t v, uint32_t now) { contractLevelOffer(s, v); return contractLevelDue(s, now); }
static void test_level_hold() {
  ContractUnitState s;
  CHECK(s.level == 0 && s.levelWant == 0 && !contractLevelDue(s, 1000));
  // the first raise is taken (nothing went up in the last 340 ms), and so is every fall
  CHECK(levelLine(s, 255, 1000) && s.level == 255 && s.levelMs == 1000);
  CHECK(levelLine(s, 128, 1050) && s.level == 128 && s.levelMs == 1000);
  CHECK(!levelLine(s, 128, 1060) && s.level == 128);                       // the same again: nothing changed
  // a raise inside the window waits; the newest wins; a level no higher than the unit's cancels it
  CHECK(!levelLine(s, 255, 1100) && s.level == 128 && s.levelWant == 255);
  CHECK(!contractLevelDue(s, 1339) && s.level == 128);
  CHECK(!levelLine(s, 200, 1200) && s.levelWant == 200);
  CHECK(contractLevelDue(s, 1340) && s.level == 200 && s.levelMs == 1340);
  CHECK(!contractLevelDue(s, 1341) && !contractLevelDue(s, 5000));         // once
  CHECK(!levelLine(s, 255, 1400) && s.levelWant == 255);
  CHECK(levelLine(s, 90, 1450) && s.level == 90);                          // a fall while a raise waits: the raise is gone
  CHECK(!contractLevelDue(s, 1680) && !contractLevelDue(s, 5000) && s.level == 90);
  CHECK(!levelLine(s, 91, 1679) && contractLevelDue(s, 1680) && s.level == 91 && s.levelMs == 1680);   // a raise is any rise
  // the offer alone changes nothing a meter shows: the level moves in contractLevelDue()
  contractLevelOffer(s, 0); CHECK(s.level == 91); CHECK(contractLevelDue(s, 1681) && s.level == 0 && s.levelMs == 1680);
  // the PSI finding: 255 and 128 by turns every 50 ms. The level goes up at most once in 340 ms.
  { ContractUnitState m; uint32_t lastUp = 0; int ups = 0; uint8_t was = 0;
    for (uint32_t t = 1000; t < 11000; t += 10) {
      if (t % 50 == 0) contractLevelOffer(m, (t / 50) % 2 ? 255 : 128);
      contractLevelDue(m, t);
      if (m.level > was) { if (ups) CHECK(t - lastUp >= 2 * STROBE_MIN_STATE_MS); lastUp = t; ups++; }
      was = m.level;
    }
    CHECK(ups >= 20 && ups <= 30); }                                       // it does still move: 2 to 3 times a second
  // any stream of levels at any times: two rises of the level are never under 340 ms apart,
  // a level no higher than the unit's is always taken at once, and a raise waits only inside the window
  { ContractUnitState m; uint32_t now = 500, lastUp = 0; bool any = false; uint8_t was = 0;
    for (int i = 0; i < 200000; i++) {
      now += gateRnd(4) ? 1u + gateRnd(30) : 1u + gateRnd(900);
      if (gateRnd(3)) {
        const uint8_t v = (uint8_t)(gateRnd(4) ? gateRnd(256) : (gateRnd(2) ? 255 : 0));
        const uint8_t before = m.level;
        const bool changed = levelLine(m, v, now);
        if (v <= before) CHECK(m.level == v && changed == (v != before));
        else CHECK((m.level == v && changed) || (m.level == before && !changed && m.levelWant == v));
      } else contractLevelDue(m, now);
      if (m.levelWant > m.level) CHECK(now - m.levelMs < 2 * STROBE_MIN_STATE_MS);
      CHECK(m.levelWant >= m.level);
      if (m.level > was) { if (any) CHECK(now - lastUp >= 2 * STROBE_MIN_STATE_MS); lastUp = now; any = true; }
      was = m.level;
      if (gateRnd(500) == 0) {                                             // a show boundary: the level goes, the time stays
        const uint32_t ms = m.levelMs;
        if (gateRnd(2)) contractOnStop(m, 255); else contractOnShow(m, 255, 0);
        CHECK(m.level == 0 && m.levelWant == 0 && m.levelMs == ms);
        was = 0;
      }
    } }
  // the reset is not a way round it: X, then the same raise again
  ContractUnitState x;
  levelLine(x, 255, 1000); contractOnStop(x, 255);
  CHECK(x.level == 0 && !levelLine(x, 255, 1020) && x.level == 0);
  CHECK(!contractLevelDue(x, 1339) && contractLevelDue(x, 1340) && x.level == 255);
}

int main() {
  test_level_hold();
  test_integer_clock();
  test_field_gate();
  test_ca14();
  test_overlay_helpers();
  test_saturating_params();
  test_bpm_range_and_hold();
  test_seed_kinds_and_edge();
  test_rate_floor_and_every_n();
  test_show_reset_and_watchdog();
  test_live_hold();
  test_score_verify();
  test_query_reply();
  test_v1_effect_timing();
  test_native_allow_logics();
  test_native_allow_psi_flthy();
  test_name_tables();
  printf("%s  %d/%d checks passed\n", g_fail ? "FAILURES" : "OK", g_total - g_fail, g_total);
  return g_fail ? 1 : 0;
}
