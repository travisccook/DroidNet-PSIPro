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
  test_env_bright();
  test_score();
  test_fx_helpers();
  test_fx_spatial_helpers();
  test_fx_hue_twinkle_helpers();
  printf("%s  %d/%d checks passed\n", g_fail ? "FAILURES" : "OK", g_total - g_fail, g_total);
  return g_fail ? 1 : 0;
}
