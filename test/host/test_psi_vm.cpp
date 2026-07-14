// Part of the DroidNet PSI Pro fork test harness — an additive layer bolted onto
// Neil Hutchison's PSI Pro firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// Structural check of every VM program: ops are known, arities consume
// in-bounds bytes, every program reaches OP_END within its array, and every
// frame has a SHOW-class terminator. Compiles psi_vm.h in tables-only mode —
// no firmware globals needed.
//
// It also enforces the RETURNING-SHOW LOOP INVARIANT that vmStep()'s OP_END
// continue-wrap (include/psi_vm.h) depends on: a non-oneshot OP_END wraps to
// the loop target and KEEPS EXECUTING within the same vmStep() call, so if no
// RETURNING show op — OP_SHOW, OP_SHOWR, or OP_SHOWRND — lay between the wrap
// target (the byte after OP_LOOPSTART if present, else the program start) and
// OP_END, vmStep() would spin in that wrap loop forever on a watchdogless
// AVR. OP_SHOWNOW does NOT count: it shows and falls through without arming a
// delay or returning, so it cannot break the spin. Also pinned here: at most
// one OP_LOOPSTART per program (the core keeps a single wrap target,
// g_vmLoopPtr), and every program contains at least one returning show op.
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
    P(vmc_leia), P(vmc_swscan), P(vmc_knight),
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

static bool returningShow(uint8_t op) {
  // OP_SHOWNOW deliberately excluded: it neither arms a delay nor returns
  // from vmStep(), so it cannot terminate the OP_END continue-wrap.
  return op == OP_SHOW || op == OP_SHOWR || op == OP_SHOWRND;
}

int main() {
  for (const auto& pr : progs) {
    size_t i = 0;
    bool ended = false;
    size_t loopStarts = 0;      // OP_LOOPSTART occurrences (core supports one wrap target)
    bool showAnywhere = false;  // >=1 returning show op in the whole program
    bool showInWrapBody = false;  // >=1 returning show op between wrap target and OP_END
    while (i < pr.len) {
      uint8_t op = pr.code[i++];
      int a = arity(op);
      if (a < 0) { printf("%s: unknown op %u at %zu\n", pr.name, op, i - 1); return 1; }
      i += (size_t)a;
      if (op == OP_LOOPSTART) {
        loopStarts++;
        // The wrap target moves here (vmStep sets g_vmLoopPtr to the byte
        // after OP_LOOPSTART): shows seen so far are prologue-only and do
        // not protect the wrap loop, so the wrap-body tally restarts.
        showInWrapBody = false;
      }
      if (returningShow(op)) {
        showAnywhere = true;
        showInWrapBody = true;  // walk order: everything from here on is >= wrap target
      }
      if (op == OP_END) { ended = true; break; }
    }
    assert(ended && i <= pr.len && "program must OP_END in bounds");
    if (loopStarts > 1) {
      printf("%s: %zu OP_LOOPSTARTs — the core keeps ONE wrap target (g_vmLoopPtr)\n",
             pr.name, loopStarts);
      return 1;
    }
    if (!showAnywhere) {
      printf("%s: no returning show op (OP_SHOW/OP_SHOWR/OP_SHOWRND) anywhere\n", pr.name);
      return 1;
    }
    if (!showInWrapBody) {
      printf("%s: no returning show op between the wrap target and OP_END —\n"
             "  vmStep()'s OP_END continue-wrap would spin forever (OP_SHOWNOW\n"
             "  does not return and does not count)\n", pr.name);
      return 1;
    }
  }
  printf("test_psi_vm: %zu programs OK\n", sizeof(progs) / sizeof(progs[0]));
  return 0;
}
