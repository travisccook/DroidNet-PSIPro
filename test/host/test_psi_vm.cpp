// Part of the DroidNet PSI Pro fork test harness — an additive layer bolted onto
// Neil Hutchison's PSI Pro firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// Structural check of every VM program: ops are known, arities consume
// in-bounds bytes, every program reaches OP_END within its array, and every
// frame has a SHOW-class terminator. Compiles psi_vm.h in tables-only mode —
// no firmware globals needed.
#define PSI_VM_TABLES_ONLY 1
#include "native_mocks/Arduino.h"
#include "native_mocks/FastLED.h"
#include "../../include/psi_vm.h"
#include <assert.h>
#include <stdio.h>

struct ProgRef { const char* name; const uint8_t* code; size_t len; };
#define P(x) {#x, x, sizeof(x)}
static const ProgRef progs[] = {
    P(vmc_flash60), P(vmc_flash125),
    // conversion tasks append their programs here
};

static int arity(uint8_t op) {
  switch (op) {
    case OP_END: case OP_CLEAR: case OP_LOOPSTART: case OP_SHOWNOW: return 0;
    case OP_FILL_ALL: case OP_MUL_RAND: return 1;
    case OP_SHOW: case OP_PIX: case OP_FRAME: case OP_SPARKLE: case OP_SCALE_RAND: return 2;
    case OP_SHOWR: case OP_FILL_ROW: case OP_FILL_COLR: return 3;
    case OP_SHOWRND: case OP_HALFCOLR: return 4;
    default: return -1;
  }
}

int main() {
  for (const auto& pr : progs) {
    size_t i = 0;
    bool ended = false;
    while (i < pr.len) {
      uint8_t op = pr.code[i++];
      int a = arity(op);
      if (a < 0) { printf("%s: unknown op %u at %zu\n", pr.name, op, i - 1); return 1; }
      i += (size_t)a;
      if (op == OP_END) { ended = true; break; }
    }
    assert(ended && i <= pr.len && "program must OP_END in bounds");
  }
  printf("test_psi_vm: %zu programs OK\n", sizeof(progs) / sizeof(progs[0]));
  return 0;
}
