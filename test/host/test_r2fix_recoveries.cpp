// test/host/test_r2fix_recoveries.cpp — the flash recoveries of the R2 review fix (2026-11-21), held
// to the code they replaced, in the style of review_r2_differential.cpp. Each recovery drops a test
// of a ContractParams flag whose value is the default whenever the flag is not set:
//
//   verb L      contractLevelOffer(g_u, pr.hasLevel ? pr.level : 0)   ->  pr.level
//   verb M      _boundary('s', pr.hasWd ? pr.wd : 0)                 ->  pr.wd
//   verb A      pr.hasEffect && pr.effect == CE_NATIVE                ->  pr.effect == CE_NATIVE
//   verb P      pr.hasEffect && accentEffectAllowed(pr.effect)        ->  accentEffectAllowed(pr.effect)
//
// Checked at the parser (the flag and the value, for every line: the claim each rests on) and end to
// end (what the firmware keeps for L and M:v=show, against what the old expression gives).
//
// The firmware (src/main.cpp, verbatim) on the host mocks, as the timeline scenarios build it.
#include "native_mocks/Arduino.h"
#include "native_mocks/EEPROM.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include "../../src/main.cpp"

static long g_checks = 0, g_fail = 0;
#define EXPECT(c, ...) do { g_checks++; if (!(c)) { if (g_fail++ < 20) { printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)
static uint32_t gR = 0x6C8E9CF5u;
static uint32_t rnd() { gR ^= gR << 13; gR ^= gR >> 17; gR ^= gR << 5; return gR; }

static std::string randomParams() {
  static const char* K[] = { "v", "wd", "i", "v", "wd", "i", "b", "c", "m", "am", "at", "d", "vv", "w", "", "V", "WD", "I", "v ", "i " };
  static const char* V[] = { "", "0", "1", "2", "4", "9", "60", "128", "200", "255", "256", "300", "65535", "99999999999", "-1", "+5",
                             "12x", "x", " 7", "=", "show", "idle", "solid", "vu", "meter", "native:6", "native:2", "native:99",
                             "native:", "flash", "scan", "logic", "bogus", "ff0000" };
  std::string s;
  const int keys = 1 + (int)(rnd() % 6);
  for (int k = 0; k < keys; k++) {
    if (k) s += rnd() % 9 ? "," : ",,";
    s += K[rnd() % (sizeof K / sizeof K[0])];
    if (rnd() % 11) { s += "="; s += V[rnd() % (sizeof V / sizeof V[0])]; }
  }
  return s;
}

int main() {
  g_avr_next = 1; EEPROM.mem[alwaysOnAddress] = 0; EEPROM.mem[externalPOTAddress] = 1; EEPROM.mem[internalBrightnessAddress] = 20;
  setup();
  EXPECT(!accentEffectAllowed(CE_NONE), "CE_NONE is an accent");
  static const char* const kVerbs[] = { "P*L:", "P*M:", "P*A:", "P*P:", "P*M:v=show," };
  long levels = 0, shows = 0;
  for (long it = 0; it < 400000; it++) {
    const std::string line = std::string(kVerbs[rnd() % 5]) + randomParams();
    ParsedContract p;
    if (!contractParse(line.c_str(), p)) continue;
    const ContractParams& pr = p.params;
    EXPECT((pr.hasLevel ? pr.level : 0) == pr.level, "v= %u without its flag: %s", pr.level, line.c_str());
    EXPECT((pr.hasWd ? pr.wd : 0) == pr.wd, "wd= %u without its flag: %s", pr.wd, line.c_str());
    EXPECT(pr.hasEffect == (pr.effect != CE_NONE), "i= %d and its flag %d disagree: %s", (int)pr.effect, (int)pr.hasEffect, line.c_str());
    if (p.verb == CV_LEVEL) {                          // what L leaves for the meter, as the old line gave it
      g_u.effect = CE_METER; g_u.levelWant = 77;
      applyContract(p, 0, 1000);
      EXPECT(g_u.levelWant == (pr.hasLevel ? pr.level : 0), "L kept %u: %s", g_u.levelWant, line.c_str());
      levels++;
    } else if (p.verb == CV_MODE && pr.mode == 's') {  // the watchdog M:v=show arms, as the old line gave it
      applyContract(p, 0, 1000);
      EXPECT(g_u.wdSec == (pr.hasWd ? pr.wd : 0), "M:v=show armed wd %u: %s", g_u.wdSec, line.c_str());
      shows++;
    }
  }
  EXPECT(levels > 10000 && shows > 10000, "too few lines reached the firmware (%ld L, %ld M:v=show)", levels, shows);
  printf("%s  %ld/%ld checks: v=, wd= and i= are their defaults without their flags; %ld L and %ld M:v=show lines end to end\n",
         g_fail ? "FAIL" : "OK", g_checks - g_fail, g_checks, levels, shows);
  return g_fail ? 1 : 0;
}
