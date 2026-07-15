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

// Panel geometry, mirrored from include/config.h's NUM_LEDS/COLUMNS/
// LEDS_PER_COLUMN — duplicated rather than #included, since this file builds
// psi_vm.h in PSI_VM_TABLES_ONLY mode against native_mocks/, not the real
// config.h (which the tables-only build has no other reason to pull in).
static const int PSI_NUM_LEDS = 48;
static const int PSI_COLUMNS  = 10;
static const int PSI_ROWS     = 6;   // config.h's LEDS_PER_COLUMN

struct ProgRef { const char* name; const uint8_t* code; size_t len; };
#define P(x) {#x, x, sizeof(x)}
static const ProgRef progs[] = {
    P(vmc_flash60), P(vmc_flash125),
    P(vmc_leia), P(vmc_swscan), P(vmc_knight),
    P(vmc_radar),
    P(vmc_rebel), P(vmc_pulse),
    P(vmc_disco),
    P(vmc_fadeout),
    P(vmc_iheartu), P(vmc_redheart), P(vmc_march),
    P(vmc_saber), P(vmc_swintro),
    P(vmc_process),
};

static int arity(uint8_t op) {
  switch (op) {
    case OP_END: case OP_CLEAR: case OP_LOOPSTART: case OP_SHOWNOW:
    case OP_CLEARWAIT: case OP_SHOWLIVE: return 0;
    case OP_FILL_ALL: case OP_MUL_RAND: return 1;
    case OP_SHOW: case OP_PIX: case OP_FRAME: case OP_SPARKLE: case OP_SCALE_RAND: return 2;
    case OP_SHOWR: case OP_FILL_ROW: case OP_FILL_COLR: return 3;
    case OP_SHOWRND: case OP_HALFCOLR: return 4;
    default: return -1;
  }
}

// Offset (0-based, from the first operand byte) of an op's color-id operand,
// or -1 for ops that carry none. Every color id a program bakes into PROGMEM
// eventually reaches vmColor(), whose default branch indexes vmPalette[id]
// UNCHECKED (a deliberate runtime-cost tradeoff flagged since Task 6) — so
// the bounds check lives HERE, at authoring time, where it costs nothing.
static int colorOperandIndex(uint8_t op) {
  switch (op) {
    case OP_FILL_ALL: return 0;                             // color
    case OP_PIX:      return 1;                             // idx, color
    case OP_FILL_ROW: return 1;                             // row, color, scale
    case OP_SPARKLE:  return 1;                             // count, color
    case OP_FILL_COLR: return 2;                            // start, count, color
    case OP_HALFCOLR:  return 3;                            // start, count, half, color
    default: return -1;
  }
}

static bool validColorId(uint8_t c) {
  return c < VC__COUNT || c == VC_PRIMARY || c == VC_SECONDARY || c == VC_SECOFF;
}

static bool returningShow(uint8_t op) {
  // OP_SHOWNOW deliberately excluded: it neither arms a delay nor returns
  // from vmStep(), so it cannot terminate the OP_END continue-wrap.
  // OP_SHOWLIVE excluded for the identical reason (it is OP_SHOWNOW's
  // live-brightness twin — see its enum comment in psi_vm.h — same
  // show-and-fall-through shape, same inability to break the wrap spin).
  // OP_CLEARWAIT also excluded: it DOES return (so it cannot spin the wrap
  // loop forever either), but it never calls show(), so a wrap body relying
  // on it alone would render no visible frame — every shipped program's wrap
  // body (vmc_radar included) also carries a real OP_SHOW/OP_SHOWR/
  // OP_SHOWRND, so this exclusion is conservative, not a gap being papered
  // over.
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
      if (i + (size_t)a > pr.len) break;  // truncated operands: the ended/in-bounds assert below fires
      // Table-indexing operands: OP_FRAME's vmStep case indexes vmBitmaps[]
      // and vmFramePals[] with these bytes UNCHECKED (like vmColor, a
      // deliberate zero-runtime-cost tradeoff), so bound them here instead.
      if (op == OP_FRAME) {
        uint8_t bmp = pr.code[i], pal = pr.code[i + 1];
        if (bmp >= BM__COUNT) {
          printf("%s: OP_FRAME at %zu names bitmap id %u, but vmBitmaps has only "
                 "BM__COUNT=%d entries — vmStep would blit from a garbage pointer\n",
                 pr.name, i - 1, bmp, (int)BM__COUNT);
          return 1;
        }
        if (pal >= FP__COUNT) {
          printf("%s: OP_FRAME at %zu names palette id %u, but vmFramePals has only "
                 "FP__COUNT=%d rows — vmStep would read colors out of bounds\n",
                 pr.name, i - 1, pal, (int)FP__COUNT);
          return 1;
        }
      }
      int ci = colorOperandIndex(op);
      if (ci >= 0) {
        uint8_t c = pr.code[i + (size_t)ci];
        if (!validColorId(c)) {
          printf("%s: op %u at %zu names color id %u — not < VC__COUNT (%d) and not a "
                 "role id (0xFD/0xFE/0xFF); vmColor would read past vmPalette\n",
                 pr.name, op, i - 1, c, (int)VC__COUNT);
          return 1;
        }
      }
      // Spatial operands: OP_PIX/OP_FILL_ROW/OP_FILL_COLR/OP_HALFCOLR index
      // leds[]/fill_row/fill_column geometry UNCHECKED at runtime, the same
      // zero-cost tradeoff as OP_FRAME's table ids and the color ids above —
      // so bound them here instead.
      if (op == OP_PIX) {
        uint8_t idx = pr.code[i];
        if (idx >= PSI_NUM_LEDS) {
          printf("%s: OP_PIX at %zu names LED index %u, but the panel has only %d "
                 "LEDs — a runtime write past leds[]\n", pr.name, i - 1, idx, PSI_NUM_LEDS);
          return 1;
        }
      }
      if (op == OP_FILL_ROW) {
        uint8_t row = pr.code[i];
        if (row >= PSI_ROWS) {
          printf("%s: OP_FILL_ROW at %zu names row %u, but the panel has only %d "
                 "rows — fill_row would index out of bounds\n", pr.name, i - 1, row, PSI_ROWS);
          return 1;
        }
      }
      if (op == OP_FILL_COLR) {
        uint8_t start = pr.code[i], count = pr.code[i + 1];
        if ((int)start + (int)count > PSI_COLUMNS) {
          printf("%s: OP_FILL_COLR at %zu spans columns %u..%u, past the panel's %d "
                 "columns\n", pr.name, i - 1, start, start + count - 1, PSI_COLUMNS);
          return 1;
        }
      }
      if (op == OP_HALFCOLR) {
        uint8_t start = pr.code[i], count = pr.code[i + 1], half = pr.code[i + 2];
        if ((int)start + (int)count > PSI_COLUMNS) {
          printf("%s: OP_HALFCOLR at %zu spans columns %u..%u, past the panel's %d "
                 "columns\n", pr.name, i - 1, start, start + count - 1, PSI_COLUMNS);
          return 1;
        }
        if (half >= 2) {
          printf("%s: OP_HALFCOLR at %zu names half %u, but only 0 (top) / 1 (bottom) "
                 "exist\n", pr.name, i - 1, half);
          return 1;
        }
      }
      if (op == OP_SHOWR) {
        uint8_t rep = pr.code[i];
        if (rep == 0) {
          printf("%s: OP_SHOWR at %zu has rep=0 — g_vmRepLeft (uint8_t) underflows to "
                 "255 on the first decrement instead of hitting 0, so the frame would "
                 "silently re-run 255 extra ticks\n", pr.name, i - 1);
          return 1;
        }
      }
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

  // Task 10 reviewer follow-up (noted in progress.md, closed here): OP_FRAME's
  // bmp/pal operands are bounds-checked against BM__COUNT/FP__COUNT above, but
  // nothing previously walked vmFramePals' OWN five color-id columns through
  // validColorId() — a hand-typed VC_* row (like FP_SABER/FP_CLASH, Task 13)
  // could silently bake an out-of-range id that vmColor() would read past
  // vmPalette[] for, unchecked, exactly the class of gap colorOperandIndex()
  // already closes for program bytes.
  for (size_t r = 0; r < FP__COUNT; r++) {
    for (size_t c = 0; c < 5; c++) {
      uint8_t id = vmFramePals[r][c];
      if (!validColorId(id)) {
        printf("vmFramePals[%zu][%zu]: color id %u — not < VC__COUNT (%d) and not a "
               "role id (0xFD/0xFE/0xFF); vmColor would read past vmPalette\n",
               r, c, id, (int)VC__COUNT);
        return 1;
      }
    }
  }

  // Completeness: every vmProgs[] descriptor's code pointer must appear in
  // progs[] above, or a shipped program could ship without ever passing
  // through this decode-walk at all (a new VmProg row added without a
  // matching P() entry here). vmProgs[] and VmProg::code both live outside
  // the PSI_VM_TABLES_ONLY fence (include/psi_vm.h), and PROGMEM/
  // pgm_read_ptr are no-ops on this host build (native_mocks/Arduino.h — see
  // its PROGMEM section), so a plain pointer comparison against progs[] is
  // safe here without going through pgm_read_ptr.
  {
    const size_t nVmProgs = sizeof(vmProgs) / sizeof(vmProgs[0]);
    for (size_t k = 0; k < nVmProgs; k++) {
      bool found = false;
      for (const auto& pr : progs) {
        if (pr.code == vmProgs[k].code) { found = true; break; }
      }
      if (!found) {
        printf("vmProgs[%zu]: code pointer not covered by any progs[] entry above — "
               "this program ships but the decode-walk above never checked it\n", k);
        return 1;
      }
    }
    printf("test_psi_vm: %zu/%zu vmProgs[] entries covered by progs[]\n", nVmProgs, nVmProgs);
  }

  printf("test_psi_vm: %zu programs OK, %d vmFramePals rows OK\n",
         sizeof(progs) / sizeof(progs[0]), (int)FP__COUNT);
  return 0;
}
