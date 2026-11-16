// Scenario probe: the firmware (src/main.cpp verbatim) on the repo's own host mocks, with a
// scripted command timeline. Prints a compact frame log. Scratch only.
#include "native_mocks/Arduino.h"
#include "native_mocks/FastLED.h"
#include "native_mocks/EEPROM.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include "../../../../src/main.cpp"

struct Ev { uint32_t t; const char* line; };
struct Fr { uint32_t t; uint8_t scale; unsigned lit; unsigned sum; CRGB a; };
static std::vector<Fr> frames;
static void hook(const CRGB* l, int n, uint8_t scale) {
  Fr f; f.t = g_mock_millis; f.scale = scale; f.lit = 0; f.sum = 0; f.a = l[20];
  for (int i = 0; i < n; i++) { unsigned s = l[i].r + l[i].g + l[i].b; f.sum += s; if (s) f.lit++; }
  frames.push_back(f);
}
static void run(const char* name, std::vector<Ev> evs, uint32_t dur, int pot, unsigned ee0, unsigned ee1, unsigned ee2,
                uint32_t printFrom, uint32_t printTo, uint32_t every) {
  printf("=== %s\n", name);
  g_mock_analog = pot; g_mock_pins[JUMP_FRONT_REAR] = 1;
  EEPROM.mem[0] = ee0; EEPROM.mem[1] = ee1; EEPROM.mem[2] = ee2;
  g_frame_hook = hook; setup();
  size_t ei = 0; uint32_t lastPrint = 0; size_t fi = 0;
  for (uint32_t t = 1; t <= dur; t++) {
    g_mock_millis = t;
    while (ei < evs.size() && evs[ei].t == t) { printf("  t=%u  >> %s\n", t, evs[ei].line); Serial1.feed(evs[ei].line); Serial1.feed("\r"); ei++; }
    loop(); serialEventRun();
    for (; fi < frames.size(); fi++) {
      const Fr& f = frames[fi];
      if (f.t >= printFrom && f.t <= printTo && (every == 0 || f.t - lastPrint >= every)) {
        printf("  t=%6u scale=%3u lit=%2u sum=%5u led20=%02x%02x%02x armed=%d fx=%d idx=%d cnt=%d\n", f.t, f.scale, f.lit, f.sum,
               f.a.r, f.a.g, f.a.b, (int)g_contractArmed, (int)g_effect, g_scoreIndex, g_scoreCount);
        lastPrint = f.t;
      }
    }
  }
}
int main(int argc, char** argv) {
  int which = argc > 1 ? atoi(argv[1]) : 0;
  switch (which) {
    case 1: run("S1 default brightness vs pot (pot low => native scale ~10)", {{3000, "!P*A:i=solid,c=ffffff"}}, 3200, 51, 0, 0, 20, 2800, 3200, 100); break;
    case 2: run("S2 live d= deadline survives into a pushed show", {{1000, "!P*A:i=flash,c=ff0000,d=3000"}, {2000, "!**M:v=show"},
              {2001, "!P*A:i=solid,c=00ff00,at=0"}, {2002, "!P*A:i=solid,c=0000ff,at=8"}, {2003, "!**C:bpm=120,bpb=4,beat=0"}}, 8000, 512, 0, 1, 20, 1900, 8000, 500); break;
    case 3: run("S3 meter base + per-beat accent", {{1000, "!**M:v=show"}, {1001, "!P*A:i=meter,c=00ff00,at=0,am=2,ae=solid,ac=ffffff,ad=180"},
              {1002, "!P*L:v=200"}, {1003, "!**C:bpm=60,bpb=4,beat=0"}}, 4200, 512, 0, 1, 20, 1900, 3400, 0); break;
    case 4: run("S4 bpm=600 full-depth pump", {{1000, "!**C:bpm=600,bpb=4,beat=0"}, {1001, "!P*A:i=solid,c=ffffff,m=255,am=2"}}, 2100, 512, 0, 1, 20, 1500, 2100, 0); break;
    case 5: run("S5 X then M:v=show", {{1000, "!P*A:i=solid,c=ff0000"}, {2000, "!P*X"}, {3000, "!**M:v=show"}}, 3300, 512, 0, 1, 20, 900, 3300, 150); break;
    case 6: { std::vector<Ev> e = {{1000, "!**M:v=show"}};
              static char buf[10][64];
              const char* cols[10] = {"010000","020000","030000","040000","050000","060000","070000","080000","090000","0a0000"};
              for (int i = 0; i < 10; i++) { snprintf(buf[i], 64, "!P*A:i=solid,c=%s,at=%d", cols[i], i * 2); e.push_back({(uint32_t)(1001 + i), buf[i]}); }
              e.push_back({1020, "!**C:bpm=120,bpb=4,beat=0"});
              run("S6 ten sections pushed (cap 8)", e, 12500, 512, 0, 1, 20, 1000, 12500, 1000); break; }
    case 7: run("S7 host goes away; native 0T1 / 0D while armed", {{1000, "!P*A:i=solid,c=ff0000"}, {2000, "0T1"}, {3000, "0D"}, {4000, "0T6"}}, 5000, 512, 0, 1, 20, 900, 5000, 500); break;
    case 8: run("S8 scored native:1 then comet then native:1", {{1000, "!**M:v=show"}, {1001, "!P*A:i=native:1,at=0"}, {1002, "!P*A:i=comet,c=00ff00,at=4"}, {1003, "!P*A:i=native:15,at=8"},
              {1004, "!**C:bpm=120,bpb=4,beat=0"}}, 8000, 512, 0, 1, 20, 4900, 5900, 0); break;
    case 9: run("S9 native:15 in a show, then X (EEPROM blank => alwaysOn compiled default)", {{1000, "!P*A:i=native:15"}, {3000, "!P*X"}}, 9000, 512, 0xFF, 1, 20, 2900, 3700, 0); break;
    case 10: run("S10 same, EEPROM alwaysOn=0", {{1000, "!P*A:i=native:15"}, {3000, "!P*X"}}, 30000, 512, 0, 1, 20, 2900, 30000, 2600); break;
    case 11: run("S11 bare re-anchor C mid-beat (Studio drift ticker) with a scored every-beat accent", {{1000, "!**M:v=show"},
              {1001, "!P*A:i=solid,c=000040,at=0,am=2,ae=solid,ac=ffffff,ad=180"}, {2000, "!**C:bpm=60,bpb=4,beat=0"}, {4500, "!**C:ph=1500,beat=4"}}, 6400, 512, 0, 1, 20, 1990, 6400, 0); break;
    case 12: run("S12 b= on a scored line", {{1000, "!**M:v=show"}, {1001, "!P*A:i=solid,c=ffffff,b=40,at=0"}, {1002, "!**C:bpm=120,bpb=4,beat=0"}}, 1600, 512, 0, 1, 20, 1100, 1600, 200); break;
    case 13: run("S13 i=swipe / i=cylon (spec'd PSI ids)", {{1000, "!P*A:i=comet,c=00ff00"}, {1500, "!P*A:i=swipe,c=ff8800"}, {2000, "!P*A:i=cylon,c=ff0000"}}, 2400, 512, 0, 1, 20, 1100, 2400, 300); break;
    case 14: run("S14 Resend (M:v=show + new score, no X, no new C yet): stale clock keeps running", {{1000, "!**M:v=show"}, {1001, "!P*A:i=solid,c=101010,at=0"}, {2000, "!**C:bpm=120,bpb=4,beat=0"},
              {7000, "!**M:v=show"}, {7001, "!P*A:i=solid,c=ff0000,at=0"}, {7002, "!P*A:i=solid,c=00ff00,at=8"}, {7003, "!P*A:i=solid,c=0000ff,at=12"}}, 9000, 512, 0, 1, 20, 6900, 9000, 250); break;
  }
  return 0;
}
