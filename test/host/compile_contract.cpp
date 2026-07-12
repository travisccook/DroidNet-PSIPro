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

  printf("ContractPSI.h type-check + score-native guard OK\n");
  return 0;
}
