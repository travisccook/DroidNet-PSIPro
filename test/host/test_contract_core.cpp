// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// for droid lighting firmware. Copyright (c) 2026 Travis Cook.
// Shared verbatim (byte-identical) across the DroidNet RSeries/PSI/Flthy forks.
// The firmware this layer attaches to is the work of its original authors; see the
// README for full attribution. This file is licensed LGPL-2.1-only when distributed
// as part of the LGPL-2.1 RSeries fork, and MIT in the other two forks (see
// LICENSE-DroidNet-Contract). Travis Cook holds the copyright in this file and
// grants both.
// Host unit tests for contract_core.h — no external deps, tiny assert harness.
#include "../../src/contract/contract_core.h"
#include <cstdio>

static int g_fail = 0, g_total = 0;
#define CHECK(cond) do { g_total++; if(!(cond)){ g_fail++; \
  printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);} } while(0)

static void test_scope_and_verb() {
  ParsedContract p;
  CHECK(contractParse("LFA:i=solid,c=0080FF,d=0", p));
  CHECK(p.valid && p.cls == 'L' && p.unit == 'F' && p.verb == CV_ANIMATE);
}

static void test_verbs_and_units() {
  ParsedContract p;
  CHECK(contractParse("**P:c=FFFFFF,d=120", p) && p.cls == '*' && p.unit == '*' && p.verb == CV_PULSE);
  CHECK(contractParse("LRA", p) && p.cls == 'L' && p.unit == 'R' && p.verb == CV_ANIMATE);
  CHECK(contractParse("**C:bpm=120,ph=0,bpb=4", p) && p.verb == CV_CLOCK);
  CHECK(contractParse("**X", p) && p.verb == CV_STOP);
  CHECK(contractParse("**M:v=show", p) && p.verb == CV_MODE);
  CHECK(!contractParse("L", p));        // too short
  CHECK(!contractParse("LFZ", p));      // unknown verb
  CHECK(!contractParse("", p));         // empty
  CHECK(!contractParse("QFA", p));      // bad class
  CHECK(!contractParse("LZA", p));      // bad unit
}

static void test_params() {
  ParsedContract p;
  CHECK(contractParse("LFA:i=solid,c=0080FF,d=0", p));
  CHECK(p.params.hasEffect && p.params.effect == CE_SOLID);
  CHECK(p.params.hasColor && p.params.color.r == 0x00 && p.params.color.g == 0x80 && p.params.color.b == 0xFF);
  CHECK(p.params.hasDur && p.params.durMs == 0);

  CHECK(contractParse("**P:c=FFFFFF,d=120,b=200", p));
  CHECK(p.params.color.r == 0xFF && p.params.hasDur && p.params.durMs == 120 && p.params.bright == 200);

  CHECK(contractParse("LFA:i=flash,c=FF00FF,s=200,d=500,m=255,at=44,am=1", p));
  CHECK(p.params.effect == CE_FLASH && p.params.speed == 200 && p.params.durMs == 500);
  CHECK(p.params.beatMod == 255 && p.params.hasAt && p.params.atBeat == 44 && p.params.accentMode == 1);

  CHECK(contractParse("LFA:i=native:105", p) && p.params.effect == CE_NATIVE && p.params.nativeCode == 105);
  // six new effect names (Task 1): parse to their enum and round-trip through contractParse
  CHECK(contractParse("L*A:i=comet", p) && p.params.effect == CE_COMET);
  CHECK(contractParse("L*A:i=chase", p) && p.params.effect == CE_CHASE);
  CHECK(contractParse("L*A:i=wipe", p) && p.params.effect == CE_WIPE);
  CHECK(contractParse("L*A:i=gradient", p) && p.params.effect == CE_GRADIENT);
  CHECK(contractParse("L*A:i=colorcycle", p) && p.params.effect == CE_COLORCYCLE);
  CHECK(contractParse("L*A:i=twinkle", p) && p.params.effect == CE_TWINKLE);
  CHECK(contractParse("P*L:v=190", p) && p.verb == CV_LEVEL && p.params.hasLevel && p.params.level == 190);
  CHECK(contractParse("**C:bpm=128,ph=40,bpb=4,beat=64", p));
  CHECK(p.params.bpm == 128 && p.params.phMs == 40 && p.params.bpb == 4 && p.params.hasBeat && p.params.beatAnchor == 64);
  CHECK(contractParse("**M:v=show", p) && p.params.mode == 's');
  CHECK(contractParse("**M:v=idle", p) && p.params.mode == 'i');
  // at= and beat= are int32_t BEAT INDICES and must survive past 32767. They parse with
  // strtol (a 32-bit long everywhere we ship), not atoi — atoi returns a 16-BIT int on the
  // ATmega32U4/2560, which would wrap `at=40000` to a NEGATIVE beat that sorts to the front
  // of the score and fires its section immediately, at show start.
  // HONESTY NOTE: the host's int is 32-bit, so atoi would pass these too — they pin the
  // 32-bit field contract (they fail if the field or the parse is ever narrowed), but they
  // cannot reproduce the AVR wrap. That fix rests on inspection; no host test can gate it.
  CHECK(contractParse("LFA:i=solid,at=40000", p) && p.params.hasAt && p.params.atBeat == 40000);
  CHECK(contractParse("LFA:i=solid,at=1000000", p) && p.params.atBeat == 1000000);
  CHECK(contractParse("**C:bpm=120,beat=70000", p) && p.params.hasBeat && p.params.beatAnchor == 70000);
  CHECK(contractParse("LFA:i=solid,at=-8", p) && p.params.atBeat == -8);   // sign still parses
  // ...and a big at= must still sort to the END of the score, never the front:
  CHECK(p.params.atBeat < 0);   // (guards the line above against a silent unsigned parse)

  // unknown param ignored, still valid:
  CHECK(contractParse("LFA:i=solid,zz=9", p) && p.params.effect == CE_SOLID);
  // color rejects bad hex (leaves hasColor false):
  CHECK(contractParse("LFA:c=XYZ", p) && !p.params.hasColor);
}

static bool approx(float a, float b) { float d = a - b; return (d < 0 ? -d : d) < 0.01f; }

static void test_beat_clock() {
  // 120 BPM (2 beats/sec), anchor downbeat at t=1000ms, beat 0, 4/4
  BeatClock bc; ContractParams pr;
  pr.hasBpm = true; pr.bpm = 120; pr.hasBpb = true; pr.bpb = 4; pr.hasPh = true; pr.phMs = 0; pr.hasBeat = true; pr.beatAnchor = 0;
  beatClockSeed(bc, pr, 1000);
  CHECK(bc.running && bc.bpm == 120 && bc.anchorMs == 1000 && bc.beatOffset == 0);

  BeatPos p0 = beatPosAt(bc, 1000);
  CHECK(p0.beatIndex == 0 && approx(p0.phase, 0.0f) && p0.barPos == 0);
  BeatPos p1 = beatPosAt(bc, 1500);              // +0.5s @120bpm = 1 beat
  CHECK(p1.beatIndex == 1 && approx(p1.phase, 0.0f) && p1.barPos == 1);
  BeatPos pHalf = beatPosAt(bc, 1250);           // +0.25s = 0.5 beat
  CHECK(pHalf.beatIndex == 0 && approx(pHalf.phase, 0.5f) && pHalf.barPos == 0);
  BeatPos pBar = beatPosAt(bc, 3000);            // +2s = 4 beats -> downbeat again
  CHECK(pBar.beatIndex == 4 && pBar.barPos == 0);

  // re-anchor via beat= (drift correction): beat 64 now at t=1000
  ContractParams pr2; pr2.hasBpm = true; pr2.bpm = 120; pr2.hasBpb = true; pr2.bpb = 4; pr2.hasPh = true; pr2.phMs = 0; pr2.hasBeat = true; pr2.beatAnchor = 64;
  beatClockSeed(bc, pr2, 1000);
  CHECK(beatPosAt(bc, 1000).beatIndex == 64 && beatPosAt(bc, 1500).beatIndex == 65);

  // not running when bpm=0
  BeatClock bc2;
  CHECK(!bc2.running && beatPosAt(bc2, 5000).beatIndex == 0);
}

static void test_accent_envelope() {
  BeatPos down; down.barPos = 0; down.phase = 0.0f;    // on the downbeat, phase 0 -> env 1
  BeatPos offbeat; offbeat.barPos = 1; offbeat.phase = 0.0f;
  BeatPos mid; mid.barPos = 0; mid.phase = 0.5f;       // decayed
  CHECK(beatAccentAmount(0, down, 255, 0) == 0);       // am=0 none
  CHECK(beatAccentAmount(1, down, 0, 0) == 0);         // beatMod=0 -> none
  CHECK(beatAccentAmount(1, down, 255, 0) > 200);      // am=1 fires strong on downbeat
  CHECK(beatAccentAmount(1, offbeat, 255, 0) == 0);    // am=1 silent off the downbeat
  CHECK(beatAccentAmount(2, offbeat, 255, 0) > 0);     // am=2 fires every beat
  CHECK(beatAccentAmount(2, mid, 255, 0) < beatAccentAmount(2, offbeat, 255, 0)); // decays with phase
  CHECK(beatAccentAmount(3, down, 255, 0.0f) == 0);    // build at 0 progress -> 0
  CHECK(beatAccentAmount(3, down, 255, 1.0f) > 200);   // build at full progress -> strong
}

// ===================== v1.2: the accent-effect overlay =====================
// The v1.1 fire predicate, TRANSCRIBED VERBATIM from the body of beatAccentAmount()
// as it stood BEFORE the v1.2 refactor extracted it:
//     bool fire = (am == 2) || ((am == 1 || am == 4) && bp.barPos == 0) || (am == 3);
// beatAccentFires() must agree with this on every reachable input, or the extraction
// changed behaviour for an existing show. This is the "unchanged by construction" proof,
// mechanised.
static bool v11_fire_reference(uint8_t am, uint8_t barPos) {
  return (am == 2) || ((am == 1 || am == 4) && barPos == 0) || (am == 3);
}

static void test_beat_accent_fires() {
  // exhaustive am 0..4 x barPos 0..7 against the transcribed v1.1 predicate
  int mismatches = 0, fired = 0, silent = 0;
  for (int am = 0; am <= 4; am++) {
    for (int bp = 0; bp < 8; bp++) {
      bool got = beatAccentFires((uint8_t)am, (uint8_t)bp);
      if (got != v11_fire_reference((uint8_t)am, (uint8_t)bp)) mismatches++;
      if (got) fired++; else silent++;
    }
  }
  CHECK(mismatches == 0);
  CHECK(fired > 0 && silent > 0);   // not vacuous: both outcomes really occur in the grid

  // Spot pins, so a predicate broken the SAME way in both places can't quietly agree.
  CHECK(beatAccentFires(0, 0) == false);   // am=0: never
  CHECK(beatAccentFires(1, 0) == true);    // am=1: downbeat only
  CHECK(beatAccentFires(1, 1) == false);
  CHECK(beatAccentFires(1, 3) == false);
  CHECK(beatAccentFires(2, 0) == true);    // am=2: every beat
  CHECK(beatAccentFires(2, 3) == true);
  CHECK(beatAccentFires(3, 0) == true);    // am=3: build — every beat (progress-scaled)
  CHECK(beatAccentFires(3, 5) == true);
  CHECK(beatAccentFires(4, 0) == true);    // am=4: drop -> downbeat only
  CHECK(beatAccentFires(4, 2) == false);
  CHECK(beatAccentFires(9, 0) == false);   // unknown mode: silent, never a surprise strobe

  // NON-DIVERGENCE: the brightness pump and the (firmware) effect-swap accent must never
  // disagree about WHICH beats carry an accent. beatAccentAmount() > 0 implies the predicate
  // fires; the predicate not firing implies a zero envelope. (One-directional: am=3 at
  // progress 0 fires the predicate yet yields amount 0 — that is the build ramping in.)
  int divergences = 0, sawPositive = 0, sawFiringZero = 0;
  for (int am = 0; am <= 4; am++) {
    for (int bpos = 0; bpos < 8; bpos++) {
      for (int mod = 0; mod < 256; mod += 17) {
        for (int ph = 0; ph < 10; ph++) {
          for (int pg = 0; pg <= 2; pg++) {
            BeatPos bp; bp.barPos = (uint8_t)bpos; bp.phase = (float)ph / 10.0f;
            uint8_t amt = beatAccentAmount((uint8_t)am, bp, (uint8_t)mod, (float)pg * 0.5f);
            bool f = beatAccentFires((uint8_t)am, (uint8_t)bpos);
            if (amt > 0 && !f) divergences++;      // pump fired on a beat the accent skips
            if (!f && amt != 0) divergences++;
            if (amt > 0) sawPositive++;
            if (f && amt == 0) sawFiringZero++;
          }
        }
      }
    }
  }
  CHECK(divergences == 0);
  CHECK(sawPositive > 0);      // the implication is not vacuously true (amounts do go > 0)
  CHECK(sawFiringZero > 0);    // ...and the one-directionality really is exercised
}

static void test_beat_edge() {
  // A show's FIRST beat is always an edge (the guard starts at the sentinel).
  int32_t last = BEAT_NONE;
  CHECK(beatEdge(last, 0) == true);
  CHECK(last == 0);
  // Same beat, later frames: NO re-fire. This is what stops a 180ms accent from being
  // re-armed on every render tick inside one beat.
  CHECK(beatEdge(last, 0) == false);
  CHECK(beatEdge(last, 0) == false);
  CHECK(beatEdge(last, 1) == true);
  CHECK(beatEdge(last, 2) == true);

  // The guard advances UNCONDITIONALLY — even when the caller then decides not to fire
  // (am says this beat is silent). If it did not, the next tick would re-test the same
  // beat forever.
  int32_t l2 = BEAT_NONE;
  CHECK(beatEdge(l2, 3) == true && l2 == 3);   // caller may now skip on the am= test
  CHECK(beatEdge(l2, 3) == false);             // ...and beat 3 is still consumed

  // A backward re-anchor (Play/seek moves the beat origin) IS an edge.
  CHECK(beatEdge(l2, 1) == true && l2 == 1);

  // Negative beat indices (before the clock anchor) are reachable and work.
  int32_t l3 = BEAT_NONE;
  CHECK(beatEdge(l3, -4) == true);
  CHECK(beatEdge(l3, -4) == false);
  CHECK(BEAT_NONE < -1000000);   // ...so the sentinel must sit outside any reachable index

  // Show boundary: a unit that ended the last show ON beat 0 must still fire the new
  // show's beat 0. That only works if stop/idle/show/clock-reseed reset to BEAT_NONE
  // (a reset to 0 or -1 would swallow the first accent of the next show).
  int32_t l4 = 0;
  CHECK(beatEdge(l4, 0) == false);   // stale guard: the new show's beat 0 is swallowed
  l4 = BEAT_NONE;                    // what CV_STOP / CV_MODE / CV_CLOCK / score-load must do
  CHECK(beatEdge(l4, 0) == true);
}

static void test_accent_effect_allowed() {
  // Allowed: the STATELESS renders — a pure function of (elapsed, colour), safe to swap
  // in for ~180ms and swap back out.
  const ContractEffect ok[] = { CE_OFF, CE_SOLID, CE_FLASH, CE_PULSE, CE_RAINBOW,
                                CE_COMET, CE_CHASE, CE_WIPE, CE_GRADIENT, CE_COLORCYCLE, CE_TWINKLE };
  for (unsigned i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) CHECK(accentEffectAllowed(ok[i]));

  // Denied — and these denials are the safety property, not a nicety:
  //  * scan/sparkle/meter keep per-unit frame counters SHARED with the base look; a
  //    swap-and-restore corrupts the base look's state machine mid-song.
  //  * native:<n> hands the frame to a renderer we do not own, so the contract's render —
  //    and hence the overlay's EXPIRY check — never runs: the board would latch forever.
  CHECK(!accentEffectAllowed(CE_SCAN));
  CHECK(!accentEffectAllowed(CE_SPARKLE));
  CHECK(!accentEffectAllowed(CE_METER));
  CHECK(!accentEffectAllowed(CE_NATIVE));
  CHECK(!accentEffectAllowed(CE_NONE));
}

static void test_accent_params() {
  ParsedContract p;

  // ---- ABSENT => defaults. A v1.1 line is bit-identical after the new parse branches.
  CHECK(contractParse("L*A:i=solid,c=3b82f6,at=64,am=2,m=200", p));
  CHECK(!p.params.hasAccentFx && p.params.accentFx == CE_NONE);
  CHECK(!p.params.hasAccentColor);
  CHECK(p.params.accentColor.r == 0 && p.params.accentColor.g == 0 && p.params.accentColor.b == 0);
  CHECK(!p.params.hasAccentDur && p.params.accentDurMs == 0);
  CHECK(p.params.hasAt && p.params.atBeat == 64 && p.params.accentMode == 2 && p.params.beatMod == 200);

  // ---- A fully-loaded v1.2 scored entry.
  CHECK(contractParse("L*A:i=colorcycle,c=3b82f6,at=1234,am=2,m=200,ae=flash,ac=ffffff,ad=250", p));
  CHECK(p.params.hasEffect && p.params.effect == CE_COLORCYCLE);
  CHECK(p.params.hasAccentFx && p.params.accentFx == CE_FLASH);
  CHECK(p.params.hasAccentColor && p.params.accentColor.r == 0xFF
        && p.params.accentColor.g == 0xFF && p.params.accentColor.b == 0xFF);
  CHECK(p.params.hasAccentDur && p.params.accentDurMs == 250);
  // COLLISION GUARD: key() is an exact-LENGTH compare, so ae/ac/ad cannot alias a/at/am/c/d.
  // The pre-existing keys must still land on their own fields alongside the new ones.
  CHECK(p.params.hasAt && p.params.atBeat == 1234);
  CHECK(p.params.hasAm && p.params.accentMode == 2);
  CHECK(p.params.hasBeatMod && p.params.beatMod == 200);
  CHECK(p.params.hasColor && p.params.color.r == 0x3b && p.params.color.g == 0x82 && p.params.color.b == 0xf6);
  CHECK(!p.params.hasDur && p.params.durMs == 0);   // ad= must NOT be read as d=

  // ...and key ORDER must not matter (ae before at, ac before am, ad before i).
  CHECK(contractParse("L*A:ae=comet,at=7,ac=00ff00,am=1,ad=170,i=solid", p));
  CHECK(p.params.accentFx == CE_COMET && p.params.atBeat == 7 && p.params.accentMode == 1);
  CHECK(p.params.accentColor.r == 0x00 && p.params.accentColor.g == 0xFF && p.params.accentColor.b == 0x00);
  CHECK(p.params.accentDurMs == 170 && p.params.effect == CE_SOLID);

  // ---- every ALLOWED accent effect name parses through ae=
  CHECK(contractParse("L*A:ae=off", p)        && p.params.accentFx == CE_OFF);
  CHECK(contractParse("L*A:ae=solid", p)      && p.params.accentFx == CE_SOLID);
  CHECK(contractParse("L*A:ae=flash", p)      && p.params.accentFx == CE_FLASH);
  CHECK(contractParse("L*A:ae=pulse", p)      && p.params.accentFx == CE_PULSE);
  CHECK(contractParse("L*A:ae=rainbow", p)    && p.params.accentFx == CE_RAINBOW);
  CHECK(contractParse("L*A:ae=comet", p)      && p.params.accentFx == CE_COMET);
  CHECK(contractParse("L*A:ae=chase", p)      && p.params.accentFx == CE_CHASE);
  CHECK(contractParse("L*A:ae=wipe", p)       && p.params.accentFx == CE_WIPE);
  CHECK(contractParse("L*A:ae=gradient", p)   && p.params.accentFx == CE_GRADIENT);
  CHECK(contractParse("L*A:ae=colorcycle", p) && p.params.accentFx == CE_COLORCYCLE);
  CHECK(contractParse("L*A:ae=twinkle", p)    && p.params.accentFx == CE_TWINKLE);

  // ---- REJECTED accents leave hasAccentFx FALSE => no accent at all. Fail-safe: a
  // rejected ae= can never arm an overlay that would corrupt the base look or latch.
  CHECK(contractParse("L*A:ae=native:3", p) && !p.params.hasAccentFx && p.params.accentFx == CE_NONE);
  CHECK(contractParse("L*A:ae=scan", p)     && !p.params.hasAccentFx && p.params.accentFx == CE_NONE);
  CHECK(contractParse("L*A:ae=sparkle", p)  && !p.params.hasAccentFx && p.params.accentFx == CE_NONE);
  CHECK(contractParse("L*A:ae=meter", p)    && !p.params.hasAccentFx && p.params.accentFx == CE_NONE);
  CHECK(contractParse("L*A:ae=bogus", p)    && !p.params.hasAccentFx && p.params.accentFx == CE_NONE);
  // ...and a rejected ae= must not poison the rest of the line.
  CHECK(contractParse("L*A:ae=scan,i=solid,at=9", p) && !p.params.hasAccentFx
        && p.params.effect == CE_SOLID && p.params.hasAt && p.params.atBeat == 9);
  // ...nor may an ae=native:<n> leak its code into nativeCode — i= owns that field.
  CHECK(contractParse("L*A:i=native:105,ae=native:3", p));
  CHECK(p.params.effect == CE_NATIVE && p.params.nativeCode == 105 && !p.params.hasAccentFx);

  // ---- bad accent hex is rejected the same way c= is (leaves hasAccentColor false).
  CHECK(contractParse("L*A:ae=flash,ac=ZZZZZZ", p) && p.params.hasAccentFx && !p.params.hasAccentColor);
  CHECK(contractParse("L*A:ae=flash,ac=fff", p) && !p.params.hasAccentColor);   // too short

  // ---- ad= clamp. The score stores the duration in 10ms units in ONE byte, so 2550ms is
  // the ceiling; a bigger value must SATURATE, not wrap (atoi would wrap a 16-bit AVR int).
  CHECK(contractParse("L*A:ae=flash,ad=2550", p)  && p.params.accentDurMs == 2550);
  CHECK(contractParse("L*A:ae=flash,ad=2551", p)  && p.params.accentDurMs == 2550);
  CHECK(contractParse("L*A:ae=flash,ad=99999", p) && p.params.accentDurMs == 2550);
  CHECK(contractParse("L*A:ae=flash,ad=0", p) && p.params.hasAccentDur && p.params.accentDurMs == 0);
  CHECK(contractParse("L*A:ae=flash,ad=180", p) && p.params.accentDurMs == 180);

  // ---- WIRE BUDGET. The smallest command buffer on the three boards gives 63 usable
  // chars today (RSeries Marcduino BUFFER_SIZE 64 / PSI CMD_MAX_LENGTH 64), and both
  // TRUNCATE SILENTLY on overflow. A white flash accent over a colorcycle base is the
  // most obvious thing a user will author and it does not fit — the firmware layers must
  // raise those buffers to 96 (Studio also drops d=0 and elides default ae/ac/ad).
  // Pins the worst emitted line against the bumped budget so a future key can't blow it.
  const char* worst = "!L*A:i=colorcycle,c=3b82f6,at=1234,am=2,m=200,ae=colorcycle,ac=ffffff,ad=250";
  CHECK(strlen(worst) <= 95);                          // <= 96-byte buffer, minus the NUL
  CHECK(contractParse(worst + 1, p) && p.params.accentFx == CE_COLORCYCLE);   // ...and it parses whole
  const char* common = "!L*A:i=colorcycle,c=3b82f6,at=1234,am=2,m=200,ae=flash,ac=ffffff";
  CHECK(strlen(common) > 63);                          // THE line that busts today's 64B buffer
}

static void test_score_accent_fields() {
  // A default-constructed entry — what a v1.1 line (no ae=) builds — carries NO accent.
  // This is the bit-identical-to-v1.1 anchor: the firmware's beat-edge trigger tests
  // `accentFx == CE_NONE` and bails, so such an entry can never arm the overlay.
  ScoreEntry v11;
  CHECK(v11.accentFx == CE_NONE);
  CHECK(v11.accentDur10 == 18);   // 180ms default, in the score's 10ms quantum
  CHECK(v11.accentColor.r == 0 && v11.accentColor.g == 0 && v11.accentColor.b == 0);

  // The accent fields must survive scoreInsert's sorted shift, its replace-on-exact-match
  // path, and a scoreActiveIndex lookup — the board reads them straight off the entry the
  // active index points at, so a field lost in the shift is a silently accent-less section.
  ScoreEntry s[8]; int n = 0; const int cap = 8;
  ScoreEntry a; a.atBeat = 0;  a.effect = CE_SOLID;    a.color = RGB{1, 2, 3};   // v1.1 entry
  ScoreEntry b; b.atBeat = 64; b.effect = CE_FLASH;
  b.accentFx = CE_COMET; b.accentColor = RGB{0xFF, 0xEE, 0xDD}; b.accentDur10 = 25;
  ScoreEntry c; c.atBeat = 32; c.effect = CE_RAINBOW;
  c.accentFx = CE_PULSE; c.accentColor = RGB{0x11, 0x22, 0x33}; c.accentDur10 = 18;
  n = scoreInsert(s, n, cap, a);
  n = scoreInsert(s, n, cap, b);
  n = scoreInsert(s, n, cap, c);                       // shifts b one slot right
  CHECK(n == 3 && s[0].atBeat == 0 && s[1].atBeat == 32 && s[2].atBeat == 64);
  CHECK(s[0].accentFx == CE_NONE);                     // the v1.1 entry still has no accent
  CHECK(s[1].accentFx == CE_PULSE && s[1].accentDur10 == 18);
  CHECK(s[1].accentColor.r == 0x11 && s[1].accentColor.g == 0x22 && s[1].accentColor.b == 0x33);
  CHECK(s[2].accentFx == CE_COMET && s[2].accentDur10 == 25);          // survived the shift
  CHECK(s[2].accentColor.r == 0xFF && s[2].accentColor.g == 0xEE && s[2].accentColor.b == 0xDD);

  // scoreActiveIndex hands back the entry WITH its accent intact.
  int i = scoreActiveIndex(s, n, 40);
  CHECK(i == 1 && s[i].accentFx == CE_PULSE && s[i].accentDur10 == 18 && s[i].accentColor.g == 0x22);
  i = scoreActiveIndex(s, n, 5);
  CHECK(i == 0 && s[i].accentFx == CE_NONE);           // v1.1 section active => trigger bails

  // Replace-on-exact-match must replace the ACCENT too — including replacing a v1.2 entry
  // with a v1.1 one, which must turn the accent OFF (not leave the old one armed).
  ScoreEntry c2; c2.atBeat = 32; c2.effect = CE_METER;   // no accent keys
  n = scoreInsert(s, n, cap, c2);
  CHECK(n == 3 && s[1].effect == CE_METER);
  CHECK(s[1].accentFx == CE_NONE && s[1].accentDur10 == 18 && s[1].accentColor.r == 0);
  CHECK(s[2].accentFx == CE_COMET);                    // ...and its neighbour is untouched
}
// =================== END v1.2 accent-effect overlay =======================

// envBright() is THE shared beat-pump envelope — all three forks (Logics/PSI/HPs)
// render through it, so these vectors are the cross-board level contract. Pinning
// them here is what stops one board drifting back to its own ad-hoc math (the
// RSeries fork used to hardcode a 40% floor: b=200,m=64 rendered 80..110 while the
// other two rendered ~149..161 — the SAME cue at HALF the level).
static void test_env_bright() {
  // beatMod = 0 -> pump disabled: bright passes through verbatim, for ANY amt.
  CHECK(envBright(200, 0, 0) == 200);
  CHECK(envBright(200, 0, 128) == 200);
  CHECK(envBright(200, 0, 255) == 200);

  // amt = 255 (full accent) -> reaches EXACTLY bright, at every depth. b= is the
  // ceiling and it is reachable (the old RSeries 40%-floor math could not do this
  // at any m<255 given amt is itself m-scaled).
  CHECK(envBright(200, 64, 255) == 200);
  CHECK(envBright(200, 128, 255) == 200);
  CHECK(envBright(200, 255, 255) == 200);
  CHECK(envBright(255, 255, 255) == 255);

  // m = depth knob: amt=0 is the between-beat floor = bright*(255-m)/255.
  CHECK(envBright(200, 64, 0) == 149);    // ~25% dip
  CHECK(envBright(200, 128, 0) == 99);    // ~50% dip
  CHECK(envBright(200, 255, 0) == 0);     // full dip to black between accents

  // mid-accent ride-back (exact bytes from the real integer implementation).
  CHECK(envBright(200, 64, 64) == 161);   // the (b=200,m=64) on-beat peak: am=2 amt maxes at m
  CHECK(envBright(200, 128, 128) == 149);
  CHECK(envBright(200, 255, 128) == 100);
  CHECK(envBright(100, 32, 16) == 87);
  CHECK(envBright(255, 64, 64) == 207);

  // bright = 0 stays black regardless.
  CHECK(envBright(0, 128, 255) == 0);

  // Invariants across the whole (bright, beatMod, amt) space: never exceeds the
  // b= ceiling, never decreases as the accent rises, amt=255 lands on b=, m=0 is
  // a no-op. Guards the AVR-safe integer math against overflow/truncation regressions.
  int violations = 0;
  for (int b = 0; b < 256; b++) {
    for (int m = 0; m < 256; m++) {
      uint8_t prev = 0;
      for (int a = 0; a < 256; a++) {
        uint8_t v = envBright((uint8_t)b, (uint8_t)m, (uint8_t)a);
        if (v > b) violations++;               // ceiling
        if (a && v < prev) violations++;       // monotonic in amt
        prev = v;
      }
      if (envBright((uint8_t)b, (uint8_t)m, 255) != (uint8_t)b) violations++;  // full accent == b
      if (envBright((uint8_t)b, 0, (uint8_t)m) != (uint8_t)b) violations++;    // m=0 no-op
    }
  }
  CHECK(violations == 0);
}

static void test_score() {
  ScoreEntry s[8]; int n = 0, cap = 8;
  ScoreEntry a; a.atBeat = 0;  a.effect = CE_SOLID;
  ScoreEntry b; b.atBeat = 32; b.effect = CE_FLASH;
  ScoreEntry c; c.atBeat = 16; c.effect = CE_RAINBOW;
  n = scoreInsert(s, n, cap, a);
  n = scoreInsert(s, n, cap, b);
  n = scoreInsert(s, n, cap, c);                       // inserted between a and b
  CHECK(n == 3 && s[0].atBeat == 0 && s[1].atBeat == 16 && s[2].atBeat == 32);
  CHECK(s[1].effect == CE_RAINBOW);
  // replace on exact atBeat match
  ScoreEntry c2; c2.atBeat = 16; c2.effect = CE_METER;
  n = scoreInsert(s, n, cap, c2);
  CHECK(n == 3 && s[1].effect == CE_METER);
  // active index
  CHECK(scoreActiveIndex(s, n, -1) == -1);             // before first
  CHECK(scoreActiveIndex(s, n, 0) == 0);
  CHECK(scoreActiveIndex(s, n, 20) == 1);
  CHECK(scoreActiveIndex(s, n, 999) == 2);

  // ---- scoreClear: a show must not leak its sections into the next one ----
  int active = scoreActiveIndex(s, n, 20);
  scoreClear(n, active);
  CHECK(n == 0);
  CHECK(active == -1);                                 // "no section yet", not section 0
  CHECK(scoreActiveIndex(s, n, 999) == -1);            // empty table: nothing is active

  // The bug scoreClear exists to prevent: scoreInsert() drops silently at cap, so a
  // table that is never cleared between shows keeps the FIRST show's sections and
  // discards the second's. Fill to cap, then "stop" and load a new show.
  ScoreEntry old;
  for (int i = 0; i < cap; i++) { old.atBeat = i * 4; old.effect = CE_SOLID; n = scoreInsert(s, n, cap, old); }
  CHECK(n == cap);
  ScoreEntry dropped; dropped.atBeat = 900; dropped.effect = CE_FLASH;
  CHECK(scoreInsert(s, n, cap, dropped) == cap);       // at cap => silently dropped
  CHECK(scoreActiveIndex(s, n, 900) == cap - 1);       // ...so beat 900 replays the old tail

  scoreClear(n, active);                               // what verb X / M:v=idle must do
  ScoreEntry fresh; fresh.atBeat = 900; fresh.effect = CE_FLASH;
  n = scoreInsert(s, n, cap, fresh);
  CHECK(n == 1);
  CHECK(scoreActiveIndex(s, n, 900) == 0 && s[0].effect == CE_FLASH);   // new show plays
}

static void test_fx_helpers() {
  // fxStepMs: 30 + (255-speed)/2. Exact vectors captured by running the real
  // implementation (clang++ host build) — not hand-computed.
  CHECK(fxStepMs(255) == 30);
  CHECK(fxStepMs(0) == 157);
  CHECK(fxStepMs(128) == 93);

  // fxHsv2rgb: standard 6-sextant HSV->RGB, integer math. Exact bytes captured
  // by running the real implementation (integer division makes the greenish
  // sextant land at (3,255,0), not the ideal (0,255,0)).
  { RGB c = fxHsv2rgb(0, 255, 255);   CHECK(c.r == 255 && c.g == 0 && c.b == 0); }      // red
  { RGB c = fxHsv2rgb(85, 255, 255);  CHECK(c.g == 255); CHECK(c.r <= 8 && c.b <= 8);
                                       CHECK(c.r == 3 && c.b == 0); }                    // ~green, pinned exact
  { RGB c = fxHsv2rgb(0, 0, 120);     CHECK(c.r == 120 && c.g == 120 && c.b == 120); }   // greyscale

  // fxHash16: distinct inputs -> distinct outputs (xorshift hash). Exact
  // outputs captured by running the real implementation.
  CHECK(fxHash16(1) != fxHash16(2));
  CHECK(fxHash16(1) == 8225);
  CHECK(fxHash16(2) == 16450);
}

static void test_fx_spatial_helpers() {
  // fxHead: (elapsed / fxStepMs(speed)) % N. At speed=255, fxStepMs==30, so
  // head advances one position every 30ms and wraps at N. Exact vectors
  // captured by running the real implementation (clang++ host build).
  CHECK(fxHead(0, 255, 10) == 0);
  CHECK(fxHead(30, 255, 10) == 1);
  CHECK(fxHead(299, 255, 10) == 9);   // just before the 10th wrap
  CHECK(fxHead(300, 255, 10) == 0);   // wrapped exactly 10 steps
  CHECK(fxHead(310, 255, 10) == 0);   // still within step 10's window

  // fxCometBright: 255 at the head, linear falloff to 0 across
  // trail=max(2,2N/5); for N=10, trail=4. Exact bytes captured by running
  // the real implementation (integer division: 255-(dist*255)/trail).
  CHECK(fxCometBright(5, 5, 10) == 255);  // at head
  CHECK(fxCometBright(4, 5, 10) == 192);  // 1 behind
  CHECK(fxCometBright(3, 5, 10) == 128);  // 2 behind
  CHECK(fxCometBright(2, 5, 10) == 64);   // 3 behind
  CHECK(fxCometBright(1, 5, 10) == 0);    // 4 behind == trail -> 0
  CHECK(fxCometBright(0, 5, 10) == 0);    // beyond trail
  CHECK(fxCometBright(6, 5, 10) == 0);    // ahead of head (dist wraps to N-1 >= trail)
  CHECK(fxCometBright(9, 0, 10) == 192);  // wraps around N: 1 behind head=0
  CHECK(fxCometBright(8, 0, 10) == 128);  // wraps around N: 2 behind head=0

  // fxChaseLit: (p + elapsed/fxStepMs) % 3 == 0.
  CHECK(fxChaseLit(0, 0, 255) == true);   // p+phase=0 -> lit
  CHECK(fxChaseLit(1, 0, 255) == false);
  CHECK(fxChaseLit(2, 0, 255) == false);
  CHECK(fxChaseLit(3, 0, 255) == true);   // 3 % 3 == 0 -> lit
  CHECK(fxChaseLit(0, 30, 255) == false); // one step elapsed -> phase 1

  // fxWipeLit: ping-pong fill/drain over 2N steps. Fill half: p<=front.
  // Drain half (ph>=N): p>(ph-N). Exact vectors captured by running the
  // real implementation.
  CHECK(fxWipeLit(0, 0, 255, 10) == true);    // front at 0, p<=front
  CHECK(fxWipeLit(9, 0, 255, 10) == false);
  CHECK(fxWipeLit(1, 0, 255, 10) == false);
  CHECK(fxWipeLit(0, 300, 255, 10) == false); // ph==N: drain phase begins, p=0 not lit
  CHECK(fxWipeLit(9, 300, 255, 10) == true);  // ph==N: drain phase, far end still lit
  CHECK(fxWipeLit(5, 300, 255, 10) == true);
}

static void test_fx_hue_twinkle_helpers() {
  // fxGradientHue: base + (p*128)/span + elapsed/fxStepMs(speed), span = N-1.
  // Static (elapsed = 0): the strand spreads exactly 128 hue steps end to end.
  CHECK(fxGradientHue(0, 10, 50, 0, 255) == 50);    // p0 -> base
  CHECK(fxGradientHue(9, 10, 0, 0, 255) == 128);    // p last -> +128 span
  CHECK(fxGradientHue(3, 10, 0, 0, 255) == 42);     // mid-span: (3*128)/9
  CHECK(fxGradientHue(5, 6, 0, 0, 255) == 128);     // Flthy's 6-jewel strand, last pos
  CHECK(fxGradientHue(0, 1, 7, 0, 255) == 7);       // N=1: span guard (no divide by zero)

  // ...and it MOVES with elapsed. Without these the whole time term is untested and a
  // stub that ignores `elapsed` passes. Exact bytes from the real build (speed=255 ->
  // fxStepMs=30, so 3000ms == 100 hue steps of drift).
  CHECK(fxGradientHue(0, 10, 0, 3000, 255) == 100);   // pure drift at p0
  CHECK(fxGradientHue(5, 10, 0, 3000, 255) == 171);   // drift + span
  CHECK(fxGradientHue(9, 10, 0, 3000, 255) == 228);
  CHECK(fxGradientHue(0, 10, 200, 3000, 255) == 44);  // base+drift wraps the uint8 hue wheel
  CHECK(fxGradientHue(9, 10, 0, 7680, 255) == 128);   // drift of exactly one full wheel
  CHECK(fxGradientHue(0, 10, 0, 3000, 128) == 32);    // slower knob -> less drift (fxStepMs=93)
  CHECK(fxGradientHue(0, 10, 0, 3000, 0) == 19);      // slowest (fxStepMs=157)

  // fxCycleHue: base + elapsed/(fxStepMs(speed)*2). This used to be pinned ONLY at its
  // identity point fxCycleHue(0,0,255)==0 — so a stub `return baseHue;` passed the ENTIRE
  // suite. These vectors (real build) gate the time term, the speed knob and the wrap.
  CHECK(fxCycleHue(0, 0, 255) == 0);         // identity
  CHECK(fxCycleHue(0, 60, 255) == 1);        // one step == fxStepMs(255)*2 == 60ms
  CHECK(fxCycleHue(0, 3000, 255) == 50);
  CHECK(fxCycleHue(0, 6000, 255) == 100);
  CHECK(fxCycleHue(100, 3000, 255) == 150);  // base offsets the rotation
  CHECK(fxCycleHue(0, 15360, 255) == 0);     // exactly one full wheel -> back to base
  CHECK(fxCycleHue(200, 6000, 255) == 44);   // wraps the uint8 hue wheel
  CHECK(fxCycleHue(0, 3000, 128) == 16);     // speed knob (fxStepMs=93 -> /186)
  CHECK(fxCycleHue(0, 3000, 0) == 9);        // slowest (fxStepMs=157 -> /314)

  // THE *2u DIVISOR RELATIONSHIP between the two hue effects: fxCycleHue divides elapsed
  // by fxStepMs(speed) * 2 — exactly TWICE fxGradientHue's divisor — so a colorcycle
  // rotates at half the rate the gradient's own hue drifts at the same speed knob. Pin it
  // as a relationship, not just as vectors, so the coupling survives a refactor of either.
  //
  // At p=0 the gradient's span term is 0, so fxGradientHue(0,N,0,e,sp) is PURE drift e/s
  // and fxCycleHue(0,e,sp) is e/(2s). Integer division truncates independently, so the
  // exact relation is  drift == 2*cycle + t  with t in {0,1} (NOT plain equality — the
  // odd-remainder case really does occur; both t=0 and t=1 are exercised below). Modular
  // reduction to uint8 is homomorphic under *2, so this holds on the visible bytes too.
  int relViolations = 0, sawT0 = 0, sawT1 = 0;
  for (int sp = 0; sp < 256; sp++) {
    for (uint32_t e = 0; e < 200000u; e += 137u) {
      uint8_t g = fxGradientHue(0, 10, 0, e, (uint8_t)sp);   // pure drift
      uint8_t c = fxCycleHue(0, e, (uint8_t)sp);
      uint8_t t = (uint8_t)(g - (uint8_t)(2u * (uint32_t)c));
      if (t > 1) relViolations++;
      if (t == 0) sawT0++; else if (t == 1) sawT1++;
    }
  }
  CHECK(relViolations == 0);
  CHECK(sawT0 > 0 && sawT1 > 0);   // both truncation cases really occur (relation is tight,
                                   // not vacuously satisfied by one branch)

  // Large-strand formula pins. These positions/distances are where the AVR's 16-BIT int
  // would wrap the p*128 and dist*255 products, which is why the core widens to uint32_t
  // BEFORE those multiplies. HONESTY NOTE: the host has a 32-bit int, so it computes the
  // right answer with or without the widening — these checks pin the FORMULA (a stub or a
  // wrong span/trail divisor fails them) but they CANNOT reproduce the AVR overflow. That
  // fix rests on inspection; no host test can gate it.
  CHECK(fxGradientHue(300, 500, 0, 0, 255) == 76);    // p*128 = 38400 (> 16-bit int max)
  CHECK(fxGradientHue(256, 257, 0, 0, 255) == 128);   // first position past the 16-bit edge
  CHECK(fxCometBright(200, 400, 1000) == 128);        // dist*255 = 51000 (> 16-bit int max)
  CHECK(fxCometBright(271, 400, 1000) == 173);        // dist=129: the first wrapping distance

  // fxTwinkleBright: per-LED (fxHash16-seeded) triangle-wave brightness with a
  // hashed period + phase offset. Exact vectors captured by running the real
  // implementation (clang++ host build) — not hand-computed.
  CHECK(fxTwinkleBright(0, 0, 255) != fxTwinkleBright(1, 0, 255)); // per-LED differs
  CHECK(fxTwinkleBright(0, 0, 255) == 0);
  CHECK(fxTwinkleBright(1, 0, 255) == 133);

  // Boundary regression: odd-period midpoint (phase == half) can raw-compute
  // (period-half)*255/half > 255, wrapping a uint8_t cast to a wrong low
  // value right at what should be peak brightness. idx=2, now=327, speed=255
  // -> period=403 (odd), half=201, phase=201 -> raw triangle 256, previously
  // wrapped to 0. Must be clamped to 255 (the intended peak).
  // (A `<= 255` companion check used to sit here reading like the guard, but the return
  // type is uint8_t and the overflow WRAPS DOWN (256 -> 0), so it could never fail. Only
  // the `== 255` below actually gates the clamp.)
  CHECK(fxTwinkleBright(2, 327, 255) == 255);
}

int main() {
  test_scope_and_verb();
  test_verbs_and_units();
  test_params();
  test_beat_clock();
  test_accent_envelope();
  test_beat_accent_fires();
  test_beat_edge();
  test_accent_effect_allowed();
  test_accent_params();
  test_env_bright();
  test_score();
  test_score_accent_fields();
  test_fx_helpers();
  test_fx_spatial_helpers();
  test_fx_hue_twinkle_helpers();
  printf("%s  %d/%d checks passed\n", g_fail ? "FAILURES" : "OK", g_total - g_fail, g_total);
  return g_fail ? 1 : 0;
}
