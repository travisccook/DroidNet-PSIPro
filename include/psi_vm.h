#ifndef PSI_VM_H
#define PSI_VM_H

// Part of the DroidNet PSI Pro fork — an additive layer bolted onto Neil
// Hutchison's PSI Pro firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// include/psi_vm.h — the animation bytecode interpreter. "Animations are
// data, the interpreter is the only animation code." A program is a flat
// PROGMEM byte string (opcode, operand bytes, opcode, operand bytes, ...)
// that vmStep() walks one frame at a time and vmPlay() drives with the same
// firstTime / set_global_timeout / checkDelay / loopsDone / globalTimerDone
// lifecycle every hand-written native mode body in src/main.cpp already
// carries. It replaces those bodies one mode at a time, golden-frame-gated
// (see docs/superpowers/plans/2026-07-14-psi-animation-vm.md), never all at
// once.
//
// STATUS (this file, Task 6 of that plan): the opcode enum below is decided
// IN FULL — every op through the last conversion task (Task 13) is reserved
// now, in this order, so a program's byte layout never shifts under a later
// diff and the decode-walk test's arity table (test/host/test_psi_vm.cpp)
// never has to renumber anything it already pinned. What is NOT here yet:
// vmStep() case bodies for the ops no shipped program uses (OP_FILL_ROW/
// OP_FILL_COLR land in Task 8, OP_HALFCOLR in Task 9, OP_PIX/OP_FRAME in
// Task 10, OP_SPARKLE in Task 11, OP_SCALE_RAND/OP_MUL_RAND in Task 12), the
// bitmap tables (vmBitmaps/vmFramePals/BM_*/FP_*, Task 10 — introducing them
// now would revive PROGMEM bitmaps before any native code is deleted and
// squeeze the I2C env), every program but the two flash variants, and the
// contract shims (vmContractScan/vmContractSparkle, Task 14). vmPlay() is
// not called from anywhere yet either — src/main.cpp's runPattern() dispatch
// is untouched until Task 7. So: fully-specified interface, partially-built
// engine, zero behavior change.
//
// INCLUDE ORDER (src/main.cpp ~line 355): included AFTER the global state
// block (firstTime / patternRunning / globalPatternLoops / timingReceived /
// commandTiming / alwaysOn / lastPSIeventCode / defaultPattern / leds[] are
// all in scope, and so are primary_color()/secondary_color()/
// secondary_off_color() and the set_delay/checkDelay/set_global_timeout/
// loopsDonedoRestoreDefault/globalTimerDonedoRestoreDefault/brightness()
// helpers, all prototyped by functions.h well above) and BEFORE
// src/contract/ContractPSI.h.
//
// TABLES-ONLY MODE: define PSI_VM_TABLES_ONLY before including this header
// to get just the opcodes/macros/palette/programs/descriptors — no firmware
// globals required. test/host/test_psi_vm.cpp's decode-walk test uses this
// to structurally validate every program (known ops, in-bounds arities,
// reaches OP_END) against nothing but native_mocks/Arduino.h + FastLED.h.
// The interpreter, its SRAM state, and (later) the contract shims are fenced
// behind #ifndef PSI_VM_TABLES_ONLY below; everything else is not.
#include <FastLED.h>

// ---------------------------------------------------------------------------
// Color table. Low ids index a PROGMEM RGB table; the top three ids are ROLE
// colors resolved at runtime through the jumper-selected front/rear palette
// (primary_color()/secondary_color()/secondary_off_color(), config.h), so a
// program can say "primary" and survive a front<->rear move. The full 11-slot
// table (including colors no program shipped in Task 6 uses yet, e.g.
// VC_GREYCC/VC_SABBLUE/VC_PULSEFG) is decided now so later conversion tasks
// only ADD programs, never renumber an id an earlier program already baked
// into its PROGMEM bytes.
// ---------------------------------------------------------------------------
enum {
  VC_K = 0,      // 0x000000
  VC_W,          // 0xffffff
  VC_RED,        // 0xff0000
  VC_GOLD,       // 0xC8AA00 (StarWars gold)
  VC_GREYCC,     // 0xcccccc (Leia)
  VC_GREY,       // 0x808080 (CRGB::Grey, DiscoBall)
  VC_GREY99,     // 0x999999 (saber clash)
  VC_SABBLUE,    // 0x0000cc (saber blue)
  VC_GREYBG,     // 0x909497 (heart/letter background)
  VC_PULSEFG,    // 0x110000 (pulse bitmap fg)
  VC_PULSEBG,    // 0x555555 (pulse bitmap bg)
  VC__COUNT,
  VC_PRIMARY   = 0xFD,
  VC_SECONDARY = 0xFE,
  VC_SECOFF    = 0xFF,
};

const uint8_t vmPalette[VC__COUNT][3] PROGMEM = {
  { 0x00, 0x00, 0x00 },
  { 0xff, 0xff, 0xff },
  { 0xff, 0x00, 0x00 },
  { 0xC8, 0xAA, 0x00 },
  { 0xcc, 0xcc, 0xcc },
  { 0x80, 0x80, 0x80 },
  { 0x99, 0x99, 0x99 },
  { 0x00, 0x00, 0xcc },
  { 0x90, 0x94, 0x97 },
  { 0x11, 0x00, 0x00 },
  { 0x55, 0x55, 0x55 },
};

// ---------------------------------------------------------------------------
// Opcodes. A program is a flat PROGMEM byte string: zero or more staging ops
// followed by a frame terminator (a SHOW* op, or OP_SHOWNOW which does not
// terminate), repeated; OP_END closes the program (loop wrap or oneshot
// stop). Staging ops only write leds[]; SHOW/SHOWR/SHOWRND push the frame
// (FastLED.show(brightness())) and arm the non-blocking delay, exactly like
// the native updateLed/set_delay contract every hand-written mode body used.
//
// New ops are appended after the ones the probe already measured (through
// OP_LOOPSTART) rather than inserted mid-list, so every probe opcode keeps
// its exact byte value and the probe's program listings stay valid byte-level
// seeds for the conversion tasks — which is why OP_SHOWNOW (new in this fork,
// not in the probe) sits at the END, after OP_LOOPSTART, even though it is
// SHOW-class and would read better grouped with the other three SHOW ops.
// ---------------------------------------------------------------------------
enum {
  OP_END = 0,      //                       end of program: loop wrap or oneshot stop
  OP_SHOW,         // d16                   show + set_delay(d)
  OP_SHOWR,        // rep, d16              show + set_delay(d), re-run this frame rep times
  OP_SHOWRND,      // lo16, hi16            show + set_delay(random(lo,hi))
  OP_CLEAR,        //                       stage all-black
  OP_FILL_ALL,     // color                 stage solid fill
  OP_FILL_ROW,     // row, color, scale     fill_row (scale: CRGB %= semantics, 0 = none)
  OP_FILL_COLR,    // start, count, color   fill a run of columns
  OP_HALFCOLR,     // start, count, half, color   fill a run of half-columns (radar)
  OP_PIX,          // idx, color            single LED write (color VC_K = knock-out)
  OP_FRAME,        // bitmapId, palId       blit 48 B PROGMEM bitmap via displayMatrixColor
  OP_SPARKLE,      // count, color          N random row/col pixels (DiscoBall body)
  OP_SCALE_RAND,   // lo, hi                per-LED nscale8_video(random(lo,hi)) (FadeOut p1)
  OP_MUL_RAND,     // max                   per-LED *= random(0,max)             (FadeOut p2)
  OP_LOOPSTART,    //                       loop wraps here instead of program start
  OP_SHOWNOW,      //                       show now, KEEP RUNNING (no set_delay, no return)
};

// Encoding helpers.
#define VB16(v)            (uint8_t)((v) & 0xFF), (uint8_t)(((v) >> 8) & 0xFF)
#define V_SHOW(d)          OP_SHOW, VB16(d)
#define V_SHOWR(n, d)      OP_SHOWR, (n), VB16(d)
#define V_FROW(r, c, s)    OP_FILL_ROW, (r), (c), (s)
#define V_FCOLS(st, n, c)  OP_FILL_COLR, (st), (n), (c)
#define V_HCOLS(st, n, h, c) OP_HALFCOLR, (st), (n), (h), (c)
#define V_PIX(i, c)        OP_PIX, (i), (c)
#define V_FRAME(b, p)      OP_FRAME, (b), (p)
#define V_SPARK(n, c)      OP_SPARKLE, (n), (c)

// ---------------------------------------------------------------------------
// Programs. Task 6 ships only the two flash variants; every other mode's
// program arrives with its own conversion task (Phase 3 of the plan), each
// golden-gated against the committed captures in test/host/golden/.
// ---------------------------------------------------------------------------

// Modes 2 (60 ms) and 3/5 (125 ms): flash(white, d, loops, 4 s).
const uint8_t vmc_flash60[] PROGMEM = {
  OP_FILL_ALL, VC_W, V_SHOW(60), OP_CLEAR, V_SHOW(60), OP_END,
};
const uint8_t vmc_flash125[] PROGMEM = {
  OP_FILL_ALL, VC_W, V_SHOW(125), OP_CLEAR, V_SHOW(125), OP_END,
};

// ---------------------------------------------------------------------------
// Program descriptors.
// ---------------------------------------------------------------------------
enum {
  VMF_CLEAR         = 0x01,  // allOFF(true)-style blackout+show on entry
  VMF_ONESHOT       = 0x02,  // OP_END stops (hold last frame if alwaysOn, else restore)
  VMF_IGNORE_TIMING = 0x04,  // pattern ignores a received command timing (FadeOut)
};

struct VmProg {
  const uint8_t* code;
  uint8_t loops;    // full-program loops; 0 = indefinite (never restores by count)
  uint8_t runtime;  // seconds; nonzero = timer-driven lifecycle (as native)
  uint8_t flags;
};

enum {
  VMP_FLASH = 0, VMP_ALARM,
};

const VmProg vmProgs[] PROGMEM = {
  { vmc_flash60,  24, 4, 0 },   // VMP_FLASH  (mode 2)  — was flash(0xffffff, 60, 24, 4)
  { vmc_flash125, 15, 4, 0 },   // VMP_ALARM  (modes 3/5) — was flash(0xffffff, 125, 15, 4)
};

#ifndef PSI_VM_TABLES_ONLY

// vmColor resolves a color id to a live CRGB: low ids read the PROGMEM
// palette above; the three role ids defer to the jumper-selected front/rear
// palette so a program can say "primary" and survive a front<->rear board
// move. Lives inside the fence (not a "table") because it calls the
// firmware's primary_color()/secondary_color()/secondary_off_color(), which
// the tables-only decode test's mocks do not provide.
static CRGB vmColor(uint8_t id) {
  switch (id) {
    case VC_PRIMARY:   return primary_color();
    case VC_SECONDARY: return secondary_color();
    case VC_SECOFF:    return secondary_off_color();
    default: {
      const uint8_t* p = vmPalette[id];
      return CRGB(pgm_read_byte(p), pgm_read_byte(p + 1), pgm_read_byte(p + 2));
    }
  }
}

// ---------------------------------------------------------------------------
// Interpreter state (SRAM).
// ---------------------------------------------------------------------------
static const uint8_t* g_vmPC;        // current frame start
static const uint8_t* g_vmLoopPtr;   // loop wrap target
static uint8_t g_vmRepLeft;          // SHOWR repeats remaining for current frame
static uint8_t g_vmDescLoops;        // descriptor loop count (0 = indefinite)
static uint8_t g_vmRuntime;          // descriptor runtime seconds
static uint8_t g_vmFlags;
static bool    g_vmDone;             // oneshot finished (hold display)

static void vmShowDelay(uint16_t d) {
  FastLED.show(brightness());
  set_delay(d);
}

// Execute ops from the current frame start through its SHOW terminator (or END).
static void vmStep() {
  const uint8_t* pc = g_vmPC;
  for (;;) {
    uint8_t rep = 1;
    uint8_t op = pgm_read_byte(pc++);
    switch (op) {
      case OP_CLEAR:
        FastLED.clear();
        break;
      case OP_FILL_ALL:
        fill_solid(leds, NUM_LEDS, vmColor(pgm_read_byte(pc++)));
        break;
      case OP_LOOPSTART:
        g_vmLoopPtr = pc;
        g_vmPC = pc;
        break;
      case OP_SHOWNOW:
        FastLED.show(brightness());
        break;
      case OP_SHOWR:
        rep = pgm_read_byte(pc++);
        __attribute__((fallthrough));
      case OP_SHOW: {
        uint16_t d = pgm_read_byte(pc++);
        d |= (uint16_t)pgm_read_byte(pc++) << 8;
        vmShowDelay(d);
        if (g_vmRepLeft == 0) g_vmRepLeft = rep;
        if (--g_vmRepLeft == 0) g_vmPC = pc;  // else: re-run frame from g_vmPC next time
        return;
      }
      case OP_SHOWRND: {
        uint16_t lo = pgm_read_byte(pc++);
        lo |= (uint16_t)pgm_read_byte(pc++) << 8;
        uint16_t hi = pgm_read_byte(pc++);
        hi |= (uint16_t)pgm_read_byte(pc++) << 8;
        FastLED.show(brightness());
        set_delay(random(lo, hi));
        g_vmPC = pc;
        return;
      }
      // OP_FILL_ROW/OP_FILL_COLR (Task 8), OP_HALFCOLR (Task 9), OP_PIX/
      // OP_FRAME (Task 10), OP_SPARKLE (Task 11), OP_SCALE_RAND/OP_MUL_RAND
      // (Task 12): case bodies land with their conversion tasks, alongside
      // the programs that emit them — neither vmc_flash60 nor vmc_flash125
      // emits any of these bytes, so this fallthrough is unreachable today.
      // It exists so an opcode byte this build doesn't yet implement can
      // never be misread as extra operand bytes for whatever case follows
      // it in the switch: unknown/not-yet-landed ops end the program safely
      // (same as OP_END) instead of corrupting the frame-cursor walk.
      case OP_END:
      default:
        if (g_vmFlags & VMF_ONESHOT) {
          g_vmDone = true;                     // hold display; lifecycle below restores
          return;
        }
        if (g_vmDescLoops) globalPatternLoops--;
        // Wrap and KEEP EXECUTING this same vmStep() call (`continue`, not
        // `return`) — the loop body's first frame must show on THIS tick.
        // An indefinitely-looping program always ends with OP_END, and
        // vmPlay() only calls vmStep() once per checkDelay()-gated poll (25
        // ms in src/main.cpp's loop(), which quantizes to ~26 ms ticks in
        // the golden harness). If OP_END instead returned after wrapping
        // g_vmPC, the wrapped body wouldn't run until the FOLLOWING poll,
        // silently stealing one extra ~26 ms tick every lap — proven by
        // golden_compare.py on mode03_alarm: native's period is a steady
        // 130 ms, but with `return` here vmc_flash125 alternated 130/156 ms
        // (one poll-gate's worth of drift on every wrap-crossing toggle).
        pc = g_vmLoopPtr;
        continue;
    }
  }
}

// Tick a bytecode program with native pattern lifecycle semantics (mirrors the
// firstTime / set_global_timeout / loopsDone / globalTimerDone contract every
// native mode body carried). Marked `inline` (not `static`) even though
// nothing calls it yet in this task — src/main.cpp's runPattern() dispatch
// starts calling it in Task 7 — matching how ContractPSI.h's own externally
// -called entry points (runContractAnim, contractSetup) are declared, rather
// than `static`, which would make an as-yet-uncalled function a compiler
// warning (-Wunused-function) instead of ordinary header-only dead code.
inline void vmPlay(uint8_t progId) {
  if (firstTime) {
    firstTime = false;
    patternRunning = true;
    const VmProg* pd = &vmProgs[progId];
    // pgm_read_ptr, not pgm_read_word: pd->code is a real pointer, and
    // pgm_read_word reads only 16 bits — it silently truncates a 64-bit host
    // pointer (this only bites the host mocks; on AVR a pointer is 16 bits
    // and the truncation is invisible, which is exactly why it is easy to
    // get wrong here and not notice until something like this decode test,
    // or worse a host-side conversion tool, dereferences a garbage address).
    g_vmPC       = (const uint8_t*)pgm_read_ptr(&pd->code);
    g_vmLoopPtr  = g_vmPC;
    g_vmDescLoops = pgm_read_byte(&pd->loops);
    g_vmRuntime  = pgm_read_byte(&pd->runtime);
    g_vmFlags    = pgm_read_byte(&pd->flags);
    g_vmRepLeft  = 0;
    g_vmDone     = false;
    globalPatternLoops = g_vmDescLoops;
    if (g_vmFlags & VMF_IGNORE_TIMING) timingReceived = false;
    if ((g_vmRuntime != 0) && (!timingReceived)) set_global_timeout(g_vmRuntime);
    if (timingReceived) set_global_timeout(commandTiming);
    if (g_vmFlags & VMF_CLEAR) {
      FastLED.clear();
      FastLED.show();
    }
  }

  if (!g_vmDone && checkDelay()) vmStep();

  if (g_vmDone) {
    // oneshot end (lightsaberBattle semantics, arriving Task 13)
    if (!alwaysOn) {
      lastPSIeventCode = defaultPattern;
      patternRunning = false;
    }
  } else if ((g_vmRuntime == 0) && (!timingReceived)) {
    if (g_vmDescLoops) loopsDonedoRestoreDefault();
  } else {
    globalTimerDonedoRestoreDefault();
  }
}

// vmContractScan / vmContractSparkle (the ContractPSI.h CE_SCAN / CE_SPARKLE
// shims) land in Task 14, once scanCol()/DiscoBall() have no other caller.

#endif  // PSI_VM_TABLES_ONLY

#endif  // PSI_VM_H
