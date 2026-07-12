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
  CHECK(contractParse("P*L:v=190", p) && p.verb == CV_LEVEL && p.params.hasLevel && p.params.level == 190);
  CHECK(contractParse("**C:bpm=128,ph=40,bpb=4,beat=64", p));
  CHECK(p.params.bpm == 128 && p.params.phMs == 40 && p.params.bpb == 4 && p.params.hasBeat && p.params.beatAnchor == 64);
  CHECK(contractParse("**M:v=show", p) && p.params.mode == 's');
  CHECK(contractParse("**M:v=idle", p) && p.params.mode == 'i');
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
}

int main() {
  test_scope_and_verb();
  test_verbs_and_units();
  test_params();
  test_beat_clock();
  test_accent_envelope();
  test_score();
  printf("%s  %d/%d checks passed\n", g_fail ? "FAILURES" : "OK", g_total - g_fail, g_total);
  return g_fail ? 1 : 0;
}
