// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Neil Hutchison's PSI Pro firmware. Copyright (c) 2026 Travis Cook.
// MIT-licensed (see LICENSE-DroidNet-Contract). The PSI Pro firmware this layer
// attaches to is the work of Neil Hutchison and contributors, is NOT covered by that
// license, and carries no license of its own; see the NOTICE in README.md.
// SPDX-License-Identifier: MIT
//
// Host check of the PSI firmware layer ContractPSI.h against mock_psi.h.
// Mostly a TYPE-CHECK (proves the firmware C++ compiles with the verified board API
// signatures — catches typos / wrong arity / type errors before the bench), plus a
// focused behavioral guard that a scored NATIVE section threads its native code into
// the ScoreEntry (the exact drift the 2026-07-12 fork audit fixed on this board).
#include "mock_psi.h"
#include "../../src/contract/ContractPSI.h"
#include <cstdio>

int main() {
  contractSetup();

  // exercise the parser + every verb path so the compiler instantiates them
  const char* cmds[] = {
    "!PFA:i=solid,c=00ff88,d=0",
    "!P*A:i=flash,c=ff0000,s=200",
    "!P*A:i=cylon,c=f97316",
    "!P*A:i=scan,c=00aaff,s=180",
    "!P*A:i=comet,c=3B82F6,s=200",
    "!P*A:i=chase,c=3B82F6,s=200",
    "!P*A:i=wipe,c=22C55E,s=150",
    "!P*A:i=gradient,s=120",
    "!P*A:i=colorcycle,s=90",
    "!P*A:i=twinkle,c=FFFFFF,s=100",
    "!P*A:i=sparkle,c=ffffff",
    "!P*A:i=rainbow",
    "!P*A:i=meter",
    "!P*A:i=native:15,c=ff0000",
    "!P*L:v=190",
    "!**P:c=ffffff,d=120",
    "!**C:bpm=128,ph=40,bpb=4",
    "!P*A:i=solid,c=0080ff,m=220",
    "!**B:v=60",
    "!P*A:i=solid,c=00ff00,at=8,am=1",   // Phase-2 scheduled section
    "!**X",
    "!PFM:v=show",
    "!PFM:v=idle",
    "!PFQ",
  };
  for (const char* c : cmds) parseContract(c);

  // replicate the serialEvent()/receiveEvent() dispatch body to type-check it
  char line[] = "!PFA:i=solid,c=0080ff,d=0";
  if (line[0] == '!') parseContract(line);

  // drive the loop hooks across a few ticks
  for (int t = 0; t < 4; t++) {
    _mock_millis += 25;
    contractPulseTick();
    contractLoopTick();
    runContractAnim();
  }

  // direct verb dispatch entry points
  ParsedContract p;
  if (contractParse("PFA:i=flash,c=112233,s=64,d=500", p)) applyContract(p);
  (void)contractAddressed('P', 'F');

  // behavioral guard: a scored NATIVE section must thread its native code into the
  // ScoreEntry (this is the exact drift the fork audit fixed — nativeCode stayed -1,
  // so scored native sections rendered the stale live g_nativeCode instead).
  { int before = g_scoreCount;
    parseContract("!P*A:i=native:22,c=FF0000,at=99");
    if (g_scoreCount <= before || g_score[g_scoreCount - 1].nativeCode != 22
        || g_score[g_scoreCount - 1].effect != CE_NATIVE) {
      printf("FAIL: PSI scored native section did not thread nativeCode\n");
      return 1;
    }
  }

  // behavioral guard (C1): EVERY contract effect must LATCH its frame.
  // leds[] is a staging buffer and there is no global FastLED.show() in loop(), so a
  // renderer built from fill_column() alone (comet/chase/wipe/gradient once did exactly
  // this) never pushes a pixel — the panel freezes on the last shown frame for the whole
  // section. mock_psi.h models which primitives latch; assert every effect reaches one.
  {
    struct Case { const char* cmd; const char* name; };
    const Case cases[] = {
      { "!P*A:i=off",                        "off" },
      { "!P*A:i=solid,c=0080ff",             "solid" },
      { "!P*A:i=flash,c=ff0000,s=200",       "flash" },
      { "!P*A:i=pulse,c=ff00ff",             "pulse" },
      { "!P*A:i=rainbow",                    "rainbow" },
      { "!P*A:i=scan,c=00aaff,s=180",        "scan" },
      { "!P*A:i=comet,c=3B82F6,s=200",       "comet" },
      { "!P*A:i=chase,c=3B82F6,s=200",       "chase" },
      { "!P*A:i=wipe,c=22C55E,s=150",        "wipe" },
      { "!P*A:i=gradient,s=120",             "gradient" },
      { "!P*A:i=colorcycle,s=90",            "colorcycle" },
      { "!P*A:i=twinkle,c=FFFFFF,s=100",     "twinkle" },
      { "!P*A:i=sparkle,c=ffffff",           "sparkle" },
      { "!P*A:i=meter",                      "meter" },
      { "!P*A:i=native:15,c=ff0000",         "native" },
    };
    for (const Case& tc : cases) {
      parseContract("!**X");                 // clear any pulse overlay / prior look
      parseContract(tc.cmd);
      for (int t = 0; t < 3; t++) {          // 3 ticks: enough for any time-gated look
        _mock_millis += 25;
        mock_resetLatch();
        runContractAnim();
        if (mock_showCount == 0) {
          printf("FAIL: effect '%s' rendered a frame but never latched it "
                 "(no FastLED.show) — the panel would freeze\n", tc.name);
          return 1;
        }
      }
    }
    // ...and the four column-built looks must ALSO stage a full frame first.
    const char* colLooks[] = { "!P*A:i=comet,c=3B82F6,s=200", "!P*A:i=chase,c=3B82F6,s=200",
                               "!P*A:i=wipe,c=22C55E,s=150",  "!P*A:i=gradient,s=120" };
    for (const char* cmd : colLooks) {
      parseContract("!**X");
      parseContract(cmd);
      _mock_millis += 25;
      mock_resetLatch();
      runContractAnim();
      if (mock_fillColumnCount != COLUMNS) {
        printf("FAIL: '%s' staged %d columns, expected %d\n", cmd, mock_fillColumnCount, COLUMNS);
        return 1;
      }
    }
  }

  // behavioral guard (I2): X (stop) and M:v=idle must CLEAR the Phase-2 score.
  // scoreInsert() drops silently at cap (8), so a score left populated means a second
  // show's sections are discarded and the board replays the first show's.
  {
    parseContract("!**X");
    if (g_scoreCount != 0 || g_scoreIndex != -1) {
      printf("FAIL: X did not clear the score (count=%d index=%d)\n", g_scoreCount, g_scoreIndex);
      return 1;
    }
    for (int i = 0; i < 8; i++) {                      // fill show #1 to cap
      char c[40]; snprintf(c, sizeof(c), "!P*A:i=solid,c=0000ff,at=%d", i * 4);
      parseContract(c);
    }
    if (g_scoreCount != 8) { printf("FAIL: score did not fill to cap\n"); return 1; }
    parseContract("!PFM:v=idle");
    if (g_scoreCount != 0 || g_scoreIndex != -1) {
      printf("FAIL: M:v=idle did not clear the score (count=%d index=%d)\n", g_scoreCount, g_scoreIndex);
      return 1;
    }
    parseContract("!P*A:i=flash,c=00ff00,at=900");     // show #2 must not be dropped at cap
    if (g_scoreCount != 1 || g_score[0].effect != CE_FLASH) {
      printf("FAIL: second show's section was dropped (count=%d)\n", g_scoreCount);
      return 1;
    }
    parseContract("!**X");
  }

  // behavioral guard (I5): an am=3 ("build") section must actually RAMP.
  // sectionProgress was hardcoded to 0.0f, so beatAccentAmount() returned exactly 0 for
  // every build frame and the board sat at the envelope floor — fully black at m=255 —
  // for the entire section. Score two 16-beat sections at 120 BPM and sample the accent
  // early vs. late in the first one.
  {
    parseContract("!**X");
    parseContract("!P*A:i=solid,c=0000ff,d=0");          // arm, no deadline
    parseContract("!P*A:i=solid,c=0000ff,at=0,am=3,m=255");
    parseContract("!P*A:i=solid,c=00ff00,at=16,am=3,m=255");
    uint32_t t0 = _mock_millis;
    parseContract("!**C:bpm=120,bpb=4,beat=0");          // anchor = now, 500 ms/beat

    // sample on a downbeat (barPos 0, phase ~0) at 1/4 and 3/4 through the section
    _mock_millis = t0 + 4 * 500 + 10;                    // beat 4 of 0..16 -> progress 0.25
    contractLoopTick();
    uint8_t early = tempGlobalBrightnessValue;
    _mock_millis = t0 + 12 * 500 + 10;                   // beat 12 of 0..16 -> progress 0.75
    contractLoopTick();
    uint8_t late = tempGlobalBrightnessValue;

    if (early == 0 || late == 0) {                       // the bug: floor at m=255 is black
      printf("FAIL: am=3 build section is parked at the envelope floor "
             "(early=%u late=%u) — sectionProgress is not being derived\n",
             (unsigned)early, (unsigned)late);
      return 1;
    }
    if (late <= early) {
      printf("FAIL: am=3 build does not ramp with section progress (early=%u late=%u)\n",
             (unsigned)early, (unsigned)late);
      return 1;
    }
    parseContract("!**X");
  }

  // _scaleC math guard. NOTE: this canNOT reproduce the AVR bug it accompanies — the
  // uncast `uint8_t * uint8_t` overflow only exists where int is 16-bit, and host int is
  // 32-bit, so the old code computes the right answer here. It pins the scaling contract
  // (full factor => unchanged, zero => black, half => ~half) so a future refactor of the
  // one place the widening cast now lives cannot silently change the math.
  {
    CRGB full = _scaleC(CRGB(255, 128, 0), 255);
    CRGB zero = _scaleC(CRGB(255, 128, 0), 0);
    CRGB half = _scaleC(CRGB(255, 128, 0), 128);
    if (full.r != 255 || full.g != 128 || full.b != 0) { printf("FAIL: _scaleC(v=255) is not identity\n"); return 1; }
    if (zero.r != 0 || zero.g != 0 || zero.b != 0)     { printf("FAIL: _scaleC(v=0) is not black\n"); return 1; }
    if (half.r != 128 || half.g != 64 || half.b != 0)  { printf("FAIL: _scaleC(v=128) mid-scale wrong (%u,%u,%u)\n",
                                                                (unsigned)half.r, (unsigned)half.g, (unsigned)half.b); return 1; }
  }

  printf("ContractPSI.h type-check + score-native/latch/score-clear/build-ramp/scale guards OK\n");
  return 0;
}
