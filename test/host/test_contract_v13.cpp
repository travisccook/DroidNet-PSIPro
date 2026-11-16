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
  CHECK(beatSectionProgress(8, 0, 16) == 0.5f && beatSectionProgress(8, 16, 16) == 0.0f);
  CHECK(beatSectionProgress(4, 8, 4) == 0.0f);
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
  CHECK(fxFlashHalfMs(255) == 170 && fxFlashHalfMs(0) == 425 && fxFlashHalfMs(128) == 297);
  CHECK(fxFlashOn(0, 255) && fxFlashOn(169, 255) && !fxFlashOn(170, 255) && !fxFlashOn(339, 255) && fxFlashOn(340, 255));
  for (int sp = 0; sp <= 255; sp++) CHECK(fxFlashHalfMs((uint8_t)sp) >= STROBE_MIN_STATE_MS);
  // S8: the pulse cycle is never shorter than 2 * STROBE_MIN_STATE_MS, at any speed
  CHECK(fxPulsePeriodMs(0) == 2000 && fxPulsePeriodMs(255) == 340);
  for (int sp = 0; sp <= 255; sp++) CHECK(fxPulsePeriodMs((uint8_t)sp) >= 2u * STROBE_MIN_STATE_MS);
  CHECK(fxPulseBright(0, 0) == 0 && fxPulseBright(1000, 0) == 255 && fxPulseBright(500, 0) == 127);
  CHECK(fxPulseBright(2000, 0) == 0 && fxPulseBright(1500, 0) == 127);
  CHECK(fxPulseBright(170, 255) == 255 && fxPulseBright(340, 255) == 0 && fxPulseBright(85, 255) == 127);
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
  CHECK(allowed == 14 + 6 + 5);
  CHECK(!nativeAllowedLogics(3));                                    // Leia sets its own brightness, past the ceiling (S6)
  CHECK(nativeAllowedLogics(2) && nativeAllowedLogics(4) && nativeAllowedLogics(12));
  CHECK(nativeAllowedLogics(0) && nativeAllowedLogics(14) && nativeAllowedLogics(19) && nativeAllowedLogics(24));
  CHECK(nativeAllowedLogics(101) && nativeAllowedLogics(105));
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
  // the step is a quarter of the ceiling (and never 0)
  CHECK(contractFieldStep(255) == 64 && contractFieldStep(160) == 41 && contractFieldStep(0) == 1 && contractFieldStep(3) == 1);

  // a first onset, well after power-on, passes untouched; so does everything while the field stays lit
  ContractUnitState s; s.ceil = 200;
  CHECK(contractFieldGate(s, 0, 1000) == 0 && !s.fieldLit);
  CHECK(contractFieldGate(s, 200, 1010) == 200 && s.fieldLit && s.fieldOnsetMs == 1010);
  CHECK(contractFieldGate(s, 180, 1020) == 180 && s.fieldLit);              // a dip smaller than a step: still lit
  CHECK(contractFieldGate(s, 200, 1030) == 200 && s.fieldOnsetMs == 1010);  // back to the old peak: not a new one
  // a blackout is never delayed
  CHECK(contractFieldGate(s, 0, 1040) == 0 && !s.fieldLit);
  // ...what follows it is: no more than a step less one above the trough until 340 ms after the onset
  CHECK(contractFieldGate(s, 200, 1050) == 50);
  CHECK(contractFieldGate(s, 40, 1100) == 40);                              // under a step: a look's own texture passes
  CHECK(contractFieldGate(s, 51, 1200) == 50);
  CHECK(contractFieldGate(s, 200, 1349) == 50 && !s.fieldLit);
  CHECK(contractFieldGate(s, 200, 1350) == 200 && s.fieldLit && s.fieldOnsetMs == 1350);
  // the window runs from when the field last got BRIGHTER, not from when it began to
  CHECK(contractFieldGate(s, 0, 1700) == 0);
  CHECK(contractFieldGate(s, 60, 1710) == 60 && s.fieldLit && s.fieldOnsetMs == 1710);   // a slow rise begins...
  CHECK(contractFieldGate(s, 120, 1900) == 120 && s.fieldOnsetMs == 1900);
  CHECK(contractFieldGate(s, 200, 2100) == 200 && s.fieldOnsetMs == 2100);               // ...and ends here
  CHECK(contractFieldGate(s, 0, 2110) == 0);
  CHECK(contractFieldGate(s, 200, 2400) == 50);                                          // 690 ms after it began, 300 after it ended
  CHECK(contractFieldGate(s, 200, 2440) == 200);
  // a fall to a trough that is not dark, and back: the same rule, from that trough
  CHECK(contractFieldGate(s, 120, 2450) == 120 && !s.fieldLit && s.fieldRef == 120);
  CHECK(contractFieldGate(s, 200, 2460) == 170);
  CHECK(contractFieldGate(s, 100, 2470) == 100 && s.fieldRef == 100);                    // the trough follows the level down
  CHECK(contractFieldGate(s, 200, 2480) == 150);
  // the gate's state is a safety anchor: no reset clears it
  contractFieldGate(s, 200, 3000); contractFieldGate(s, 0, 3010);
  contractOnStop(s, 200);     CHECK(contractFieldGate(s, 200, 3100) == 50);
  contractOnIdle(s, 200);     CHECK(contractFieldGate(s, 200, 3200) == 50);
  contractOnShow(s, 200, 0);  CHECK(contractFieldGate(s, 200, 3300) == 50);
  CHECK(contractFieldGate(s, 200, 3340) == 200);
  // millis() wrapping changes nothing
  ContractUnitState w; w.ceil = 255;
  CHECK(contractFieldGate(w, 255, 0xFFFFFF00u) == 255);
  CHECK(contractFieldGate(w, 0, 0xFFFFFF80u) == 0);
  CHECK(contractFieldGate(w, 255, 0x00000010u) == 63);          // 272 ms later, across the wrap
  CHECK(contractFieldGate(w, 255, 0x00000054u) == 255);         // 340 ms later
  // the first 340 ms after power-on are held (the anchor starts at 0): the fail-safe direction
  ContractUnitState boot; boot.ceil = 255;
  CHECK(contractFieldGate(boot, 255, 100) == 63 && contractFieldGate(boot, 255, 340) == 255);

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
  CHECK(contractFieldAllowed(n, 160, 1349) == 40 && contractFieldAllowed(n, 160, 1350) == 160 && contractFieldAllowed(n, 40, 1130) == 40);
  CHECK(contractFieldGate(n, 160, 1360) == 160 && n.fieldOnsetMs == 1360);
  CHECK(contractFieldGate(n, 0, 1370) == 0 && !n.fieldLit && n.fieldRef == 0);   // a native lights-out is level 0: dark at once
  CHECK(contractFieldGate(n, 60, 1380) == 40);                              // (dimmed, but still an onset from dark)
  CHECK(contractFieldGate(n, 100, 1710) == 100 && contractFieldGate(n, 50, 1720) == 50 && !n.fieldLit);   // lit, then dimmed by a step
  CHECK(!contractFieldMayLight(n, 1800));                                   // a native look at the ceiling is an onset from there...
  CHECK(contractFieldAllowed(n, 80, 1800) == 80);                           // ...one within a step of the trough is not
  // light nobody asked the gate about (a button): recorded, never refused, and the contract's
  // next onset keeps 340 ms from it
  ContractUnitState f; f.ceil = 200;
  contractFieldGate(f, 200, 1000); contractFieldGate(f, 0, 1010);
  CHECK(contractFieldGate(f, 200, 1100) == 50 && !f.fieldLit);              // the contract's own relight is held...
  contractFieldForced(f, 200, 1100);
  CHECK(f.fieldLit && f.fieldRef == 200 && f.fieldOnsetMs == 1100);         // ...a button's is on the LEDs: recorded
  CHECK(contractFieldGate(f, 0, 1110) == 0 && contractFieldGate(f, 200, 1400) == 50 && contractFieldGate(f, 200, 1440) == 200);
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

int main() {
  test_field_gate();
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
