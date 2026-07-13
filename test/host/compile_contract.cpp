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

// ---- v1.2 accent-overlay probe ----------------------------------------------
// The mock is a LATCH model, so the accent guards below assert on (a) the overlay state
// the render path keys off — g_pulseActive / g_pulseFx / g_pulseColor / g_pulseStartMs —
// and (b) what actually reached the strip (mock_fillColumnCount / mock_lastShowBright).
// A "fire" is a NEW g_pulseStartMs stamp while the overlay is up: _fireAccent() is the one
// and only place that stamps it, and every fire in a show lands on a distinct millis.
struct AccentLog {
  int     count = 0;
  int32_t beat[64]   = {0};
  uint8_t barPos[64] = {0};
};
// Drive `beats` beats of a running show in stepMs increments, logging every accent fire.
// t0 MUST be the clock anchor (the millis at which the C command with beat=0 was parsed).
static void runShow(uint32_t t0, uint32_t beatMs, int beats, uint32_t stepMs, uint8_t bpb,
                    AccentLog& log) {
  uint32_t prevStart = g_pulseStartMs;
  for (uint32_t t = t0; t < t0 + beatMs * (uint32_t)beats; t += stepMs) {
    _mock_millis = t;
    mock_resetLatch();
    contractPulseTick();
    contractLoopTick();
    if (g_pulseActive && g_pulseStartMs != prevStart) {     // a NEW accent was armed this frame
      prevStart = g_pulseStartMs;
      if (log.count < 64) {
        int32_t b = (int32_t)((t - t0) / beatMs);
        log.beat[log.count]   = b;
        log.barPos[log.count] = (uint8_t)(b % (int32_t)bpb);
        log.count++;
      }
    }
  }
}

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

  // ===================== v1.2 accent-overlay guards (A1..A6) =====================
  // The gap these close: in Phase 2 the board's only beat expression was the brightness
  // envelope — it could not fire the effect-swap accent Phase 1 gets from verb P. A scored
  // entry now carries ae=/ac=/ad= and the board arms the SAME overlay on its own beat edge.

  // A1: am=1 (downbeat) — an accent fires on EVERY downbeat and on NO other beat.
  {
    parseContract("!**X");
    parseContract("!P*A:i=solid,c=0000ff,b=200,d=0");                       // arm the base look
    parseContract("!P*A:i=solid,c=0000ff,at=0,am=1,m=200,ae=flash,ac=ffffff,ad=200");
    uint32_t t0 = _mock_millis + 500;   // clear of any prior test's 340 ms strobe cool-down
    _mock_millis = t0;
    parseContract("!**C:bpm=120,bpb=4,beat=0");                             // 500 ms/beat
    AccentLog log;
    runShow(t0, 500, 12, 25, 4, log);                                       // beats 0..11
    if (log.count != 3) {
      printf("FAIL: am=1 scored accent fired %d times over 12 beats, expected 3 "
             "(one per downbeat)\n", log.count);
      return 1;
    }
    for (int i = 0; i < log.count; i++) {
      if (log.barPos[i] != 0) {
        printf("FAIL: am=1 scored accent fired on beat %ld (barPos %u) — downbeats only\n",
               (long)log.beat[i], (unsigned)log.barPos[i]);
        return 1;
      }
    }
    if (log.beat[0] != 0 || log.beat[1] != 4 || log.beat[2] != 8) {
      printf("FAIL: am=1 accents landed on beats %ld/%ld/%ld, expected 0/4/8\n",
             (long)log.beat[0], (long)log.beat[1], (long)log.beat[2]);
      return 1;
    }
    if (g_pulseFx != CE_FLASH || g_pulseColor.r != 255 || g_pulseColor.g != 255 || g_pulseColor.b != 255) {
      printf("FAIL: scored accent did not carry ae=flash/ac=ffffff into the overlay "
             "(fx=%d rgb=%u,%u,%u)\n", (int)g_pulseFx,
             (unsigned)g_pulseColor.r, (unsigned)g_pulseColor.g, (unsigned)g_pulseColor.b);
      return 1;
    }
  }

  // A2: am=2 (every beat) — EXACTLY one accent per beat, not one per 25 ms frame.
  // 60 BPM keeps the beat (1000 ms) well clear of the 340 ms strobe cool-down, so a
  // per-frame re-fire would show up as ~3 fires/beat rather than being coalesced away.
  {
    parseContract("!**X");
    parseContract("!P*A:i=solid,c=0000ff,b=200,d=0");
    parseContract("!P*A:i=solid,c=0000ff,at=0,am=2,ae=flash,ad=200");
    uint32_t t0 = _mock_millis + 500;   // clear of any prior test's 340 ms strobe cool-down
    _mock_millis = t0;
    parseContract("!**C:bpm=60,bpb=4,beat=0");                              // 1000 ms/beat
    AccentLog log;
    runShow(t0, 1000, 6, 25, 4, log);
    if (log.count != 6) {
      printf("FAIL: am=2 scored accent fired %d times over 6 beats, expected 6 "
             "(once per beat, edge-triggered)\n", log.count);
      return 1;
    }
    for (int i = 0; i < log.count; i++) {
      if (log.beat[i] != i) {
        printf("FAIL: am=2 accent #%d landed on beat %ld, expected %d\n",
               i, (long)log.beat[i], i);
        return 1;
      }
    }
    // ac= was OMITTED above, so the accent colour must be RESOLVED (at insert) to the
    // entry's own colour — blue. The score deliberately carries no has-colour flag, so a
    // resolution that never happened shows up as a BLACK accent (an invisible "flash").
    if (g_pulseColor.b != 255 || g_pulseColor.r != 0 || g_pulseColor.g != 0) {
      printf("FAIL: an accent with no ac= did not inherit the entry's colour "
             "(got %u,%u,%u, expected 0,0,255)\n", (unsigned)g_pulseColor.r,
             (unsigned)g_pulseColor.g, (unsigned)g_pulseColor.b);
      return 1;
    }
    // ad=200 must survive the /10 round-trip through the score (10 ms units).
    if (g_pulseDeadline - g_pulseStartMs != 200) {
      printf("FAIL: ad=200 became a %lu ms accent\n",
             (unsigned long)(g_pulseDeadline - g_pulseStartMs));
      return 1;
    }
  }

  // A3: BACKWARD-COMPAT ANCHOR — a v1.1 scored entry (no ae=) must NEVER arm the overlay,
  // however hot its accent mode is. This is the "a score entry without the new keys behaves
  // exactly as it does today" contract, and it is what the CE_NONE guard in the edge
  // trigger buys. Also covers a REJECTED ae= (native/stateful) falling back to no accent.
  {
    const char* entries[] = {
      "!P*A:i=solid,c=0000ff,at=0,am=2,m=200",              // pure v1.1 (pump, no accent)
      "!P*A:i=solid,c=0000ff,at=0,am=2,m=200,ae=native:3",  // rejected: would latch the board
      "!P*A:i=solid,c=0000ff,at=0,am=2,m=200,ae=scan",      // rejected: stateful, corrupts base
      "!P*A:i=solid,c=0000ff,at=0,am=2,m=200,ae=bogus",     // unknown effect name
    };
    for (const char* entry : entries) {
      parseContract("!**X");
      parseContract("!P*A:i=solid,c=0000ff,b=200,d=0");
      parseContract(entry);
      uint32_t t0 = _mock_millis + 500;   // clear of any prior test's 340 ms strobe cool-down
      _mock_millis = t0;
      parseContract("!**C:bpm=60,bpb=4,beat=0");
      AccentLog log;
      runShow(t0, 1000, 6, 25, 4, log);
      if (log.count != 0 || g_pulseActive) {
        printf("FAIL: '%s' armed the accent overlay %d time(s) — a v1.1 (or rejected-ae=) "
               "entry must never self-accent\n", entry, log.count);
        return 1;
      }
    }
  }

  // A4: the accent is an EFFECT SWAP, at the accent COLOUR, at the un-pumped CEILING.
  // Base = solid blue (stages 0 columns, allON only); accent = comet red (stages all
  // COLUMNS through fill_column). So mock_fillColumnCount is a direct, non-vacuous witness
  // that the overlay rendered a DIFFERENT effect and not v1.1's solid fill of pulseColor.
  {
    parseContract("!**X");
    parseContract("!P*A:i=solid,c=0000ff,b=200,d=0");
    parseContract("!P*A:i=solid,c=0000ff,at=0,am=2,m=200,ae=comet,ac=ff0000,ad=250");
    uint32_t t0 = _mock_millis + 500;   // clear of any prior test's 340 ms strobe cool-down
    _mock_millis = t0;
    parseContract("!**C:bpm=60,bpb=4,beat=0");

    _mock_millis = t0 + 20;                       // inside beat 0's accent window (250 ms)
    mock_resetLatch();
    contractPulseTick();
    contractLoopTick();
    if (!g_pulseActive || g_pulseFx != CE_COMET) {
      printf("FAIL: ae=comet did not arm a comet overlay (active=%d fx=%d)\n",
             (int)g_pulseActive, (int)g_pulseFx);
      return 1;
    }
    if (mock_fillColumnCount != COLUMNS) {
      printf("FAIL: the accent overlay staged %d columns, expected %d — it rendered a solid "
             "fill instead of the ae= effect\n", mock_fillColumnCount, COLUMNS);
      return 1;
    }
    bool sawAccentColor = false;                  // ac=ff0000 red, never the blue base
    for (int i = 0; i < COLUMNS; i++)
      if (mock_column[i].r > 0 && mock_column[i].b == 0) sawAccentColor = true;
    if (!sawAccentColor) {
      printf("FAIL: the accent overlay did not render the accent colour (ac=ff0000)\n");
      return 1;
    }
    if (mock_lastShowBright != 200) {
      printf("FAIL: the accent overlay latched at %u, expected the un-pumped ceiling 200 "
             "(the accent must not dip with the envelope)\n", (unsigned)mock_lastShowBright);
      return 1;
    }

    _mock_millis = t0 + 400;                      // past the 250 ms accent: the base look is back
    mock_resetLatch();
    contractPulseTick();
    contractLoopTick();
    if (g_pulseActive || mock_fillColumnCount != 0) {
      printf("FAIL: the accent overlay did not expire back to the base look "
             "(active=%d columns=%d)\n", (int)g_pulseActive, mock_fillColumnCount);
      return 1;
    }
    if (mock_lastShowBright >= 200) {
      printf("FAIL: the base look is not beat-pumped after the accent (%u)\n",
             (unsigned)mock_lastShowBright);
      return 1;
    }
  }

  // A5: a clock RE-SEED must not inherit a stale beat edge. Studio re-anchors on every
  // Play/seek, so a seek back to the top replays beat 0 — with the guard still holding the
  // beat 0 it consumed before the seek, the new show's very first accent is swallowed.
  {
    parseContract("!**X");
    parseContract("!P*A:i=solid,c=0000ff,b=200,d=0");
    parseContract("!P*A:i=solid,c=0000ff,at=0,am=2,ae=flash,ad=200");
    uint32_t t0 = _mock_millis + 500;   // clear of any prior test's 340 ms strobe cool-down
    _mock_millis = t0;
    parseContract("!**C:bpm=60,bpb=4,beat=0");
    _mock_millis = t0 + 10;
    contractPulseTick();
    contractLoopTick();
    if (!g_pulseActive) { printf("FAIL: A5 setup — beat 0 did not accent\n"); return 1; }
    uint32_t firstStamp = g_pulseStartMs;

    uint32_t t1 = t0 + 5000;                      // a seek: re-anchor beat 0 to a new millis
    _mock_millis = t1;
    parseContract("!**C:bpm=60,bpb=4,beat=0");
    _mock_millis = t1 + 10;
    contractPulseTick();
    contractLoopTick();
    if (!g_pulseActive || g_pulseStartMs == firstStamp) {
      printf("FAIL: after a clock re-seed, beat 0 did not accent again — a stale beat edge "
             "survived the re-anchor\n");
      return 1;
    }
  }

  // A5b: loading a scored section must also drop the beat edge. Same trap, other door: a
  // show pushed while the clock is already running would otherwise inherit the guard from
  // whatever beat the last show consumed. (The X / M:v=show / M:v=idle resets are the same
  // invariant, but they are not independently observable — an accent needs a RUNNING clock,
  // and a running clock needs a C, which resets the edge on its own. They stay in the code
  // as belt-and-braces; this guard and A5 pin the two doors that can actually be walked.)
  {
    parseContract("!**X");
    parseContract("!P*A:i=solid,c=0000ff,b=200,d=0");
    parseContract("!P*A:i=solid,c=0000ff,at=0,am=2,ae=flash");
    uint32_t t0 = _mock_millis + 500;
    _mock_millis = t0;
    parseContract("!**C:bpm=60,bpb=4,beat=0");
    AccentLog log;
    runShow(t0, 1000, 4, 25, 4, log);                    // beats 0..3, edge now sits on beat 3
    if (log.count != 4) { printf("FAIL: A5b setup — %d accents, expected 4\n", log.count); return 1; }
    uint32_t lastStamp = g_pulseStartMs;

    _mock_millis = t0 + 3400;                            // still inside beat 3, past the cool-down
    parseContract("!P*A:i=solid,c=00ff00,at=8,am=2,ae=flash");   // a new section arrives
    _mock_millis = t0 + 3450;
    contractPulseTick();
    contractLoopTick();
    if (!g_pulseActive || g_pulseStartMs == lastStamp) {
      printf("FAIL: a score load did not drop the beat edge — the beat the PREVIOUS show "
             "consumed still gates the new one\n");
      return 1;
    }
  }

  // A6: CE_FLASH must ride the beat envelope, like every other sustained look (on this
  // board the flash latches through brightness(), i.e. the volatile 3P value contractLoopTick
  // sets to envBright()). Sample the LIT frames of one beat: early (phase 0..0.2) must be
  // brighter than late (phase 0.7..0.95). The strobe cap makes a lit state 170 ms long, so
  // each 200+ ms window is guaranteed to contain at least one lit frame.
  {
    parseContract("!**X");
    parseContract("!P*A:i=flash,c=00ff00,b=200,d=0");
    parseContract("!P*A:i=flash,c=00ff00,at=0,am=2,m=200");     // no ae=: base look only
    uint32_t t0 = _mock_millis + 500;   // clear of any prior test's 340 ms strobe cool-down
    _mock_millis = t0;
    parseContract("!**C:bpm=60,bpb=4,beat=0");                  // 1000 ms/beat

    uint8_t earlyMax = 0, lateMax = 0;
    for (uint32_t off = 0; off < 1000; off += 5) {              // one full beat (beat 2)
      _mock_millis = t0 + 2000 + off;
      mock_resetLatch();
      contractPulseTick();
      contractLoopTick();
      if (mock_showCount == 0 || mock_lastShowBright == 0) continue;   // an unlit flash frame
      if (off <= 200 && mock_lastShowBright > earlyMax) earlyMax = mock_lastShowBright;
      if (off >= 700 && off <= 950 && mock_lastShowBright > lateMax) lateMax = mock_lastShowBright;
    }
    if (earlyMax == 0 || lateMax == 0) {
      printf("FAIL: A6 setup — no lit CE_FLASH frame sampled (early=%u late=%u)\n",
             (unsigned)earlyMax, (unsigned)lateMax);
      return 1;
    }
    if (earlyMax <= lateMax) {
      printf("FAIL: CE_FLASH does not beat-pump (early=%u late=%u) — it is latching at a "
             "fixed brightness while every other look rides the envelope\n",
             (unsigned)earlyMax, (unsigned)lateMax);
      return 1;
    }
    parseContract("!**X");
  }

  // A7: CE_FLASH must actually STROBE — alternate a LIT and a DARK frame. A6 above pins only
  // that the flash BEAT-PUMPS, and on this board a plain allON() pumps too (every primitive
  // latches through brightness()), so swapping the toggle for a solid fill left A6 green and
  // the strobe itself untested. Sample the LATCHED FRAME (leds[] — allON fills it, allOFF
  // clears it) across three strobe periods: both states must occur, and every complete run of
  // a state must last >= STROBE_MIN_STATE_MS (the photosensitivity cap, <= ~2.9 Hz).
  {
    parseContract("!**X");
    parseContract("!P*A:i=flash,c=00ff00,b=200,s=255,d=0");   // s=255 => the CAP sets the period
    uint32_t t0 = g_effectStartMs;                            // the base look's timeline origin
    bool     sawLit = false, sawDark = false, prevLit = false, started = false;
    uint32_t runStart = 0, shortestRun = 0xFFFFFFFFul;
    for (uint32_t off = 0; off <= 1020; off += 5) {           // 3 x (2 * 170 ms)
      _mock_millis = t0 + off;
      mock_resetLatch();
      contractPulseTick();
      contractLoopTick();
      if (mock_showCount == 0) { printf("FAIL: A7 setup — CE_FLASH latched no frame\n"); return 1; }
      bool lit = (leds[0].r || leds[0].g || leds[0].b);
      if (lit) sawLit = true; else sawDark = true;
      if (!started) { started = true; prevLit = lit; runStart = off; }
      else if (lit != prevLit) {                              // a complete run just ended
        uint32_t len = off - runStart;
        if (len < shortestRun) shortestRun = len;
        prevLit = lit; runStart = off;
      }
    }
    if (!sawLit || !sawDark) {
      printf("FAIL: CE_FLASH never alternates (saw a lit frame=%d, saw a dark frame=%d) — it "
             "holds ONE state for the whole strobe period, i.e. it is not strobing at all\n",
             (int)sawLit, (int)sawDark);
      return 1;
    }
    if (shortestRun < STROBE_MIN_STATE_MS) {
      printf("FAIL: CE_FLASH held a state for only %lu ms — the strobe cap requires every "
             "state to last >= %lu ms (<= ~3 Hz, photosensitivity)\n",
             (unsigned long)shortestRun, (unsigned long)STROBE_MIN_STATE_MS);
      return 1;
    }
    parseContract("!**X");
  }

  // A8: the strobe cool-down must not be BYPASSABLE by re-arming while an overlay is still up.
  // The gate used to read `!g_pulseActive && ...`, so a second accent arriving INSIDE the first
  // accent's window skipped the cap entirely and restarted the overlay immediately — precisely
  // the case the cap exists for (a fast Pi mirroring verb P, or an every-beat score above
  // ~176 BPM, could restart the flash every few ms and strobe the panel far past ~3 Hz).
  {
    parseContract("!**X");
    parseContract("!P*A:i=solid,c=0000ff,b=200,d=0");
    _mock_millis += 1000;                                     // clear of any prior cool-down
    uint32_t t = _mock_millis;
    parseContract("!**P:i=flash,c=ffffff,d=300");             // fire: a 300 ms overlay
    if (!g_pulseActive || g_pulseStartMs != t) {
      printf("FAIL: A8 setup — verb P did not arm the overlay\n");
      return 1;
    }
    _mock_millis = t + 100;                                   // overlay STILL UP, 100 ms in
    parseContract("!**P:i=flash,c=ff0000,d=300");             // a too-fast re-arm
    if (g_pulseStartMs != t) {
      printf("FAIL: an accent re-armed %lu ms after the last one was ACCEPTED because the "
             "overlay was still active — the %lu ms strobe cool-down is bypassable and the "
             "panel can strobe past the photosensitivity cap\n",
             (unsigned long)(g_pulseStartMs - t), (unsigned long)(2 * STROBE_MIN_STATE_MS));
      return 1;
    }
    if (g_pulseColor.r != 255 || g_pulseColor.g != 255 || g_pulseColor.b != 255) {
      printf("FAIL: a cool-down-rejected accent still mutated the live overlay (colour is now "
             "%u,%u,%u, expected the first accent's ffffff)\n", (unsigned)g_pulseColor.r,
             (unsigned)g_pulseColor.g, (unsigned)g_pulseColor.b);
      return 1;
    }
    // ...and the gate is TIME-based, not activity-based: once one cool-down has elapsed an
    // accent fires again (this is what keeps the guard above from being satisfied by a blanket
    // "never re-arm while active", which would silently drop every fast-section accent).
    _mock_millis = t + 2 * STROBE_MIN_STATE_MS;
    parseContract("!**P:i=flash,c=ff0000,d=300");
    if (!g_pulseActive || g_pulseStartMs != _mock_millis) {
      printf("FAIL: an accent %lu ms after the last one was REJECTED — the cool-down must only "
             "coalesce accents closer together than %lu ms\n",
             (unsigned long)(2 * STROBE_MIN_STATE_MS), (unsigned long)(2 * STROBE_MIN_STATE_MS));
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

  printf("ContractPSI.h type-check + score-native/latch/score-clear/build-ramp/scale "
         "+ v1.2 accent-overlay guards (A1-A8: A7 flash strobes, A8 strobe cap unbypassable) OK\n");
  return 0;
}
