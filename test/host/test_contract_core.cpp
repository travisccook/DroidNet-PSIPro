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
  // fxGradientHue: base + (p*128)/span + elapsed/fxStepMs(speed).
  CHECK(fxGradientHue(0, 10, 50, 0, 255) == 50);   // p0 -> base
  CHECK(fxGradientHue(9, 10, 0, 0, 255) == 128);   // p last -> +128 span

  // fxCycleHue: base + elapsed/(fxStepMs(speed)*2).
  CHECK(fxCycleHue(0, 0, 255) == 0);

  // fxTwinkleBright: per-LED (fxHash16-seeded) triangle-wave brightness with a
  // hashed period + phase offset. Exact vectors captured by running the real
  // implementation (clang++ host build) — not hand-computed.
  CHECK(fxTwinkleBright(0, 0, 255) <= 255);                       // in range (uint8_t, always true)
  CHECK(fxTwinkleBright(0, 0, 255) != fxTwinkleBright(1, 0, 255)); // per-LED differs
  CHECK(fxTwinkleBright(0, 0, 255) == 0);
  CHECK(fxTwinkleBright(1, 0, 255) == 133);
}

int main() {
  test_scope_and_verb();
  test_verbs_and_units();
  test_params();
  test_beat_clock();
  test_accent_envelope();
  test_score();
  test_fx_helpers();
  test_fx_spatial_helpers();
  test_fx_hue_twinkle_helpers();
  printf("%s  %d/%d checks passed\n", g_fail ? "FAILURES" : "OK", g_total - g_fail, g_total);
  return g_fail ? 1 : 0;
}
