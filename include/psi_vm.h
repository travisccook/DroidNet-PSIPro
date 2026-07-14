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

// ---- forward declarations of render primitives defined later in main.cpp ----
// psi_vm.h is included (src/main.cpp ~line 359) before fill_row/fill_column are
// defined (~line 533/563), and functions.h (included well above, via preamble.h)
// does not prototype either. Same accumulate-defaults pattern ContractPSI.h/
// functions.h already use for allON/allOFF/scanCol: declare WITHOUT defaults
// here; main.cpp's definitions carry their own `scale_brightness=0` default, so
// existing call sites there (which rely on it) keep compiling unchanged.
void fill_column(uint8_t column, CRGB color, uint8_t scale_brightness);
void fill_row(uint8_t row, CRGB color, uint8_t scale_brightness);
// fill_half_column's real definition (main.cpp ~line 544) carries no default
// arguments at all, unlike fill_column/fill_row above — so, unlike those two,
// this forward declaration isn't "omitting a default the definition supplies
// elsewhere"; there simply isn't one to omit.
void fill_half_column(uint8_t column, uint8_t half, CRGB color);
// displayMatrixColor is the opposite accumulate-defaults case from
// fill_column/fill_row above: its color2..color8 defaults (all 0x000000)
// used to live on the DEFINITION (main.cpp ~line 681). C++ allows a default
// argument to be supplied on only ONE declaration seen by a given call site,
// so once OP_FRAME's vmStep() case (below) needed to call it from THIS
// header — included before that definition — the defaults had to move here
// instead; main.cpp's definition now repeats none of them. Every existing
// short-form call site (i_heart_u/red_heart's 5-arg calls, VUMeter's 8-arg
// call, lightsaberBattle's 6/7/8-arg calls) still resolves its omitted
// trailing colors against THIS declaration, since it precedes all of them.
void displayMatrixColor(const byte* matrix, CRGB fgcolor, CRGB bgcolor, bool displayMe, unsigned long runtime,
                        CRGB color2 = 0x000000, CRGB color3 = 0x000000, CRGB color4 = 0x000000,
                        CRGB color5 = 0x000000, CRGB color6 = 0x000000, CRGB color7 = 0x000000,
                        CRGB color8 = 0x000000);

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
// Bitmap ids + blit palettes for OP_FRAME. Task 10 introduces only what modes
// 14 (rebel) and 9-rear (Pulse) need — BM_REBEL/BM_PULSE and the FP_HEART/
// FP_PULSE palettes below. The lightsaber (22 frames) and I/Heart/U letter
// bitmaps arrive with the tasks that convert those modes (Task 13); pulling
// them in now would revive +1,056 B of PROGMEM the slim build currently
// keeps stripped, for no shipped program, and squeeze the I2C env's already
// tight headroom. These two enums are plain integer constants baked into
// vmc_rebel/vmc_pulse's PROGMEM bytes via V_FRAME() (they cost nothing to
// declare), so they live out here with the other tables; the vmBitmaps
// POINTER table itself — which must reference the real `rebel`/`pulse`
// PROGMEM byte arrays declared in matrices.h — lives inside the
// PSI_VM_TABLES_ONLY fence below, next to vmColor(). See that table's own
// comment for why: unlike a mere declaration, an initialized global pointer
// table is real data that needs `rebel`/`pulse` to actually be DEFINED in
// the same translation unit, which is true in the firmware build (matrices.h
// precedes this header, main.cpp ~line 289 vs ~line 359) but not in the
// decode-walk test (PSI_VM_TABLES_ONLY, no matrices.h).
enum { BM_REBEL = 0, BM_PULSE };

// Bitmap blit palettes: {fg, bg, color2, color3, color4} color ids, indexed
// by OP_FRAME's palId operand.
enum { FP_HEART = 0, FP_PULSE };
const uint8_t vmFramePals[][5] PROGMEM = {
  { VC_RED,     VC_GREYBG,  VC_K, VC_K, VC_K },  // rebel (mode 14); also I/Heart/U's fg/bg (Task 13)
  { VC_PULSEFG, VC_PULSEBG, VC_K, VC_K, VC_K },  // pulse trace (mode 9 rear)
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
  // OP_CLEARWAIT is NOT part of the Task 6 plan's "decided IN FULL" opcode
  // set (this header's top STATUS comment) — it is a genuine, unplanned 17th
  // op, added here in Task 9 because no combination of the existing ones can
  // represent what radar() (mode 8) needs. See vmc_radar's comment, below,
  // for the full derivation; short version: native radar() spends a whole
  // extra checkDelay()-gated tick, PER quadrant transition, on an invisible
  // FastLED.clear() that never calls show() or arms a new delay (allOFF(false)
  // with no caller-side set_delay) — a real "do nothing visible, but still
  // consume exactly one tick" step every other converted program's structure
  // never needed (their native bodies clear-and-show a visible black frame
  // and the next content frame within ONE call, hence OP_CLEAR + OP_SHOWNOW
  // being enough). Nothing about vmStep()'s existing ops, loop mechanics, or
  // any shipped program changes; this is additive only.
  OP_CLEARWAIT,    //                       stage all-black, KEEP delay/loop state, RETURN (no show)
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

// Mode 6: Leia — Cylon_Row(0xcccccc, 74, type 3 = scanRow down), 57 loops, 34 s.
// Native scanRow's per-step body is allOFF(true) (FastLED.clear() + a no-arg
// FastLED.show(), which latches the STORED m_Scale from setup) immediately
// followed by fill_row(row, color) and FastLED.show(brightness()) — a BLACK
// frame and the row frame both land in the same tick, hence OP_CLEAR,
// OP_SHOWNOW ahead of every V_FROW/V_SHOW pair below (golden-confirmed).
const uint8_t vmc_leia[] PROGMEM = {
  OP_CLEAR, OP_SHOWNOW, V_FROW(0, VC_GREYCC, 0), V_SHOW(74),
  OP_CLEAR, OP_SHOWNOW, V_FROW(1, VC_GREYCC, 0), V_SHOW(74),
  OP_CLEAR, OP_SHOWNOW, V_FROW(2, VC_GREYCC, 0), V_SHOW(74),
  OP_CLEAR, OP_SHOWNOW, V_FROW(3, VC_GREYCC, 0), V_SHOW(74),
  OP_CLEAR, OP_SHOWNOW, V_FROW(4, VC_GREYCC, 0), V_SHOW(74),
  OP_CLEAR, OP_SHOWNOW, V_FROW(5, VC_GREYCC, 0), V_SHOW(74),
  OP_END,
};

// Mode 10: Star Wars title — Cylon_Row(0xC8AA00, 500, type 4 = scanRow up),
// 5 loops. Same shape as vmc_leia (scanRow's blackout-then-row body), rows
// 5 down to 0, gold.
const uint8_t vmc_swscan[] PROGMEM = {
  OP_CLEAR, OP_SHOWNOW, V_FROW(5, VC_GOLD, 0), V_SHOW(500),
  OP_CLEAR, OP_SHOWNOW, V_FROW(4, VC_GOLD, 0), V_SHOW(500),
  OP_CLEAR, OP_SHOWNOW, V_FROW(3, VC_GOLD, 0), V_SHOW(500),
  OP_CLEAR, OP_SHOWNOW, V_FROW(2, VC_GOLD, 0), V_SHOW(500),
  OP_CLEAR, OP_SHOWNOW, V_FROW(1, VC_GOLD, 0), V_SHOW(500),
  OP_CLEAR, OP_SHOWNOW, V_FROW(0, VC_GOLD, 0), V_SHOW(500),
  OP_END,
};

// Mode 15: Knight Rider — Cylon_Col(0xff0000, 250, type 1 = scanColLeftRight
// bounce), 5 loops. scanColLeftRight's per-step body is the same
// allOFF(true)-then-fill_column(single column) shape as scanRow, 18 states:
// columns 0..9 then 8..1.
const uint8_t vmc_knight[] PROGMEM = {
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(0, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(1, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(2, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(3, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(4, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(5, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(6, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(7, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(8, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(9, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(8, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(7, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(6, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(5, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(4, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(3, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(2, 1, VC_RED), V_SHOW(250),
  OP_CLEAR, OP_SHOWNOW, V_FCOLS(1, 1, VC_RED), V_SHOW(250),
  OP_END,
};

// Mode 8: Radar sweep — radar(0xff0000, 250, 6, 0). Four quadrants (cols 0-4
// top, cols 5-9 top, cols 5-9 bottom, cols 0-4 bottom half-columns), each lit
// then blanked, 6 loops.
//
// Entry blackout: unlike scanRow/scanColLeftRight (vmc_leia/vmc_swscan/
// vmc_knight above), native radar()'s allOFF(true) clear+show fires ONCE, in
// radar()'s OWN firstTime block, entirely separate from its 8-state
// ledPatternState loop — so VMF_CLEAR's entry clear+show (bare FastLED.show(),
// same frozen-scale mechanics as OP_SHOWNOW — see vmc_leia's comment) covers
// it exactly; no per-step OP_SHOWNOW is needed or wanted here.
//
// Inter-quadrant blanking IS a genuinely separate tick, and needs a genuinely
// new op (OP_CLEARWAIT — see the opcode enum, above): native's per-quadrant
// body is split across TWO checkDelay()-gated states, always, every quadrant
// — one that stages allOFF(false) (FastLED.clear(), no show, and no
// set_delay() call at all — doNext is left exactly where the last SHOW put
// it) and, one interval-gate poll later (~26 ms, src/main.cpp loop()'s
// `interval = 25` throttle — already satisfied doNext trivially clears
// again), one that fills the next quadrant's half-columns and shows. A first
// attempt collapsed each quadrant into one OP_CLEAR+OP_HALFCOLR+OP_SHOW step
// (matching vmc_leia/vmc_swscan/vmc_knight's shape) and compensated the
// delay operand (276 instead of native's 250) to land the SHOW on the right
// tick — which DOES reproduce all 24 quadrant frames' timestamps bit-for-bit
// (any delay in (260, 285] works), but silently breaks the LOOP-EXHAUSTION
// tick: vmMaybeCountLoop's peek-ahead decrement fires synchronously with a
// step's own SHOW, so with the compensated one-step shape it decremented
// synchronously with the 24th (last) quadrant's OWN show instead of, like
// native, one whole interval-compensated step later — 260 ms early (golden-
// caught: mode08_radar frame 25, the FIRST FRAME OF THE NEXT PATTERN
// (swipe) after radar hands back control, landed 4 ms early — a real, if
// small, downstream timing regression, not a frame-25-of-radar problem, since
// radar itself only ever renders 25 frames (1 entry blackout + 24 quadrants)
// either way). Every other converted program's native body clears-and-shows
// a visible black frame and the next content frame within ONE call (hence
// OP_CLEAR + OP_SHOWNOW being enough there), so this "invisible, no-show,
// still-costs-a-tick, still-participates-in-loop-counting" step never came up
// before Task 9. OP_CLEARWAIT is that step: it reuses vmMaybeCountLoop's
// existing returning-op peek (it returns, just without calling show()), so
// the decrement for a mid-loop wrap lands on the exact tick native's
// allOFF(false) state does, and for the LOOP-EXHAUSTING wrap, vmPlay()'s tail
// (called immediately after, same tick — see vmPlay()) sees the just-
// zeroed loop count and reverts the pattern BEFORE OP_END is ever reached,
// so no 25th quadrant (a phantom "start of pass 7") ever renders — exactly
// mirroring why native's own state0 never re-fires past the last pass.
// Encoded delay is native's literal 250 throughout; no compensation needed
// once the tick OP_CLEARWAIT was missing is actually represented.
const uint8_t vmc_radar[] PROGMEM = {
  V_HCOLS(0, 5, 0, VC_RED), V_SHOW(250), OP_CLEARWAIT,
  V_HCOLS(5, 5, 0, VC_RED), V_SHOW(250), OP_CLEARWAIT,
  V_HCOLS(5, 5, 1, VC_RED), V_SHOW(250), OP_CLEARWAIT,
  V_HCOLS(0, 5, 1, VC_RED), V_SHOW(250), OP_CLEARWAIT,
  OP_END,
};

// Mode 14: Rebel symbol — was displayMatrixColor(rebel, 0xff0000, 0x909497,
// true, 5). Unlike every program above, native mode 14's own body (case 14's
// dispatch calls displayMatrixColor directly — there is no dedicated
// rebel-symbol function) carries NO checkDelay()/set_delay() gate at all:
// every single call re-blits the bitmap and, because displayMe=true,
// unconditionally calls FastLED.show(brightness()) — live brightness, not
// the frozen boot-scale bare show() OP_SHOWNOW mirrors (displayMatrixColor's
// own tail is `if (displayMe) FastLED.show(brightness());`). Since runPattern
// (and so this call) fires every ~26 ms loop() interval tick for as long as
// patternRunning stays true, the golden (mode14_rebel.psig) shows 194
// identical-content frames at that steady ~26 ms cadence — t=1 through
// t=5018 — before reverting: displayMatrixColor's tail also unconditionally
// calls globalTimerDonedoRestoreDefault() every tick since runtime=5 != 0, so
// the revert fires the instant millis() >= the 5 s deadline armed at t=1
// (globalTimeout = 5001), which lands on the first tick >= that point
// (t=5018 given the ~26 ms grid) — same tick as that tick's own (unchanged)
// content show, exactly like every other displayMatrixColor caller's
// per-call tail. golden's frame 194 (t=5044) is the FIRST FRAME OF THE NEXT
// PATTERN (default swipe) after the revert, not a rebel frame.
//
// A bytecode program only gets ticked when checkDelay() succeeds
// (vmPlay()'s `if (!g_vmDone && checkDelay()) vmStep();`), so reproducing
// "every single loop() tick" needs a SHOW delay short enough that
// checkDelay() is already satisfied again well before the next ~26 ms outer
// tick arrives — 1 ms does that (any value well under the ~25 ms interval
// throttle would); the exact value is otherwise invisible; the outer
// interval gate, not this delay, is what sets the observed ~26 ms cadence.
// OP_END then wraps (no OP_LOOPSTART — the whole 3-byte program is the wrap
// body) and re-runs the same FRAME+SHOW next tick. Loops is 0 (indefinite):
// native never manages globalPatternLoops for this mode at all (nothing in
// displayMatrixColor touches it) and the runtime!=0 path in vmPlay()'s tail
// never reads it either, so there is no loop count to encode. No VMF_CLEAR:
// native's first frame is the full rebel bitmap immediately (golden frame 0,
// t=1) — there is no separate blackout tick preceding it, unlike Pulse below.
const uint8_t vmc_rebel[] PROGMEM = {
  V_FRAME(BM_REBEL, FP_HEART), V_SHOW(1), OP_END,
};

// Mode 9 (rear): Pulse(100, 3, 0) — heartbeat trace, one extra red pixel a
// frame over the pulse bitmap, 13 states/loop x 3 loops = 39 shows. Pixel
// order verified against native Pulse()'s own switch (src/main.cpp, deleted
// by this task): 33,32,16,30,36,45,38,28,20,8,5,6,23. Entry: Pulse()'s
// firstTime block calls allOFF(true) (FastLED.clear()+bare FastLED.show(),
// frozen boot scale) BEFORE its first checkDelay()-gated state — same tick
// (golden frame 0: t=1, scale=10, all-black; frame 1: t=1, scale=20, full
// content) — exactly the VMF_CLEAR shape vmc_radar/vmc_iheartu already use.
// Each state re-reads (stages, displayMe=false) the pulse bitmap via
// OP_FRAME THEN knocks out one pixel red via OP_PIX, mirroring native's
// `displayMatrixColor(pulse, ..., false, 0)` immediately followed by that
// state's `leds[i] = 0xff0000` — the pixel write must land AFTER the frame
// blit, same order as native, or the FRAME stage would clobber it. Native
// passes runtime=0, so reversion is loop-driven (globalPatternLoops, seeded
// from loops=3 here) via loopsDonedoRestoreDefault(), not timer-driven.
const uint8_t vmc_pulse[] PROGMEM = {
  V_FRAME(BM_PULSE, FP_PULSE), V_PIX(33, VC_RED), V_SHOW(100),
  V_FRAME(BM_PULSE, FP_PULSE), V_PIX(32, VC_RED), V_SHOW(100),
  V_FRAME(BM_PULSE, FP_PULSE), V_PIX(16, VC_RED), V_SHOW(100),
  V_FRAME(BM_PULSE, FP_PULSE), V_PIX(30, VC_RED), V_SHOW(100),
  V_FRAME(BM_PULSE, FP_PULSE), V_PIX(36, VC_RED), V_SHOW(100),
  V_FRAME(BM_PULSE, FP_PULSE), V_PIX(45, VC_RED), V_SHOW(100),
  V_FRAME(BM_PULSE, FP_PULSE), V_PIX(38, VC_RED), V_SHOW(100),
  V_FRAME(BM_PULSE, FP_PULSE), V_PIX(28, VC_RED), V_SHOW(100),
  V_FRAME(BM_PULSE, FP_PULSE), V_PIX(20, VC_RED), V_SHOW(100),
  V_FRAME(BM_PULSE, FP_PULSE), V_PIX(8,  VC_RED), V_SHOW(100),
  V_FRAME(BM_PULSE, FP_PULSE), V_PIX(5,  VC_RED), V_SHOW(100),
  V_FRAME(BM_PULSE, FP_PULSE), V_PIX(6,  VC_RED), V_SHOW(100),
  V_FRAME(BM_PULSE, FP_PULSE), V_PIX(23, VC_RED), V_SHOW(100),
  OP_END,
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
  VMP_FLASH = 0, VMP_ALARM, VMP_LEIA, VMP_SWSCAN, VMP_KNIGHT, VMP_RADAR,
  VMP_REBEL, VMP_PULSE,
};

const VmProg vmProgs[] PROGMEM = {
  { vmc_flash60,  24, 4,  0 },          // VMP_FLASH  (mode 2)  — was flash(0xffffff, 60, 24, 4)
  { vmc_flash125, 15, 4,  0 },          // VMP_ALARM  (modes 3/5) — was flash(0xffffff, 125, 15, 4)
  { vmc_leia,     57, 34, 0 },          // VMP_LEIA   (mode 6)  — was Cylon_Row(0xcccccc, 74, 3, 57, 34)
  { vmc_swscan,    5, 0,  0 },          // VMP_SWSCAN (mode 10) — was Cylon_Row(0xC8AA00, 500, 4, 5, 0)
  { vmc_knight,    5, 0,  0 },          // VMP_KNIGHT (mode 15) — was Cylon_Col(0xff0000, 250, 1, 5, 0)
  { vmc_radar,     6, 0,  VMF_CLEAR },  // VMP_RADAR  (mode 8)  — was radar(0xff0000, 250, 6, 0)
  { vmc_rebel,     0, 5,  0 },          // VMP_REBEL  (mode 14) — was displayMatrixColor(rebel, ..., true, 5)
  { vmc_pulse,     3, 0,  VMF_CLEAR },  // VMP_PULSE  (mode 9 rear) — was Pulse(100, 3, 0)
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

// OP_FRAME's bitmap pointer table — indexed by BM_REBEL/BM_PULSE (declared
// with the rest of the bitmap/palette tables, above). Lives inside this
// fence, unlike vmFramePals/BM_*/FP_*, because it is a POINTER table: each
// entry is the real address of `rebel`/`pulse`, the PROGMEM byte arrays
// matrices.h declares — real data, not just a declaration, so linking it
// requires those two symbols to actually be defined in this translation
// unit. The firmware build satisfies that (matrices.h precedes this header's
// include point, main.cpp ~line 289 vs ~line 359); the decode-walk test
// (PSI_VM_TABLES_ONLY) does not include matrices.h at all, which is exactly
// why this table — like vmColor() above, for the same "needs firmware-only
// symbols" reason — sits behind the fence instead of with the other tables.
const byte* const vmBitmaps[] PROGMEM = { rebel, pulse };

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

// Every native loop-scanning body (scanRow/scanRowDownUp/scanCol/
// scanColLeftRight, and their kin in later conversion tasks) advances and
// wraps its position state — decrementing globalPatternLoops on wrap —
// SYNCHRONOUSLY, in the SAME checkDelay()-gated call that stages and shows
// the pass's final frame, BEFORE that frame's own FastLED.show(brightness())
// even runs. A bytecode program instead spends a whole extra tick getting
// from "the final frame's SHOW" to "the OP_END byte that would do the
// decrement" — because OP_END is a separate step, only reached once
// checkDelay() succeeds again, i.e. one full set_delay(d) later.
//
// For a MID-loop wrap this extra tick is invisible: nothing reads
// globalPatternLoops until it hits exactly zero (Task 7's OP_END
// continue-wrap already keeps the SHOW-to-SHOW cadence itself steady across
// the wrap). But for the LOOP-EXHAUSTING wrap it is not invisible: vmPlay()'s
// tail calls loopsDonedoRestoreDefault() every tick regardless of
// checkDelay(), exactly mirroring native's Cylon_Row/Cylon_Col tail — so if
// the decrement-to-zero happens a whole delay interval late, a bytecode
// program renders one whole extra loop pass that native NEVER shows before
// the pattern reverts. Golden-caught: mode10_swtitle frame 60 (an unwanted
// 6th row-sweep loop, delaying the revert-to-swipe transition by ~500 ms).
//
// The fix: peek one byte past every returning SHOW's new cursor. If that
// byte is OP_END, this SHOW is the pass's final frame (true of every shipped
// program — the decode-walk test's RETURNING-SHOW LOOP INVARIANT already
// requires a returning show ahead of every OP_END; every program besides
// vmc_flash60/vmc_flash125/vmc_leia/vmc_swscan/vmc_knight in the Task 6-13
// probe listings also places that show immediately before OP_END) — so do
// the decrement HERE, synchronously with the frame, instead of waiting for
// OP_END's own later tick. OP_END no longer decrements at all: by the time a
// loop-exhausting OP_END would be reached, vmPlay()'s tail has already seen
// globalPatternLoops hit zero and (if !alwaysOn) reverted the pattern, so
// that tick of vmStep() never runs — exactly like native, where scanRow's
// wrap-and-decrement branch for the exhausted pass is likewise never
// followed by another switch-case.
static void vmMaybeCountLoop(const uint8_t* pc) {
  if (!(g_vmFlags & VMF_ONESHOT) && g_vmDescLoops && pgm_read_byte(pc) == OP_END) {
    globalPatternLoops--;
  }
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
        // No-arg show(), NOT show(brightness()) — mirrors native allOFF(true)'s
        // bare FastLED.show() call, which latches whatever scale setup()
        // passed to the ONE-TIME FastLED.setBrightness(brightness()) call
        // (src/main.cpp ~line 377), taken BEFORE the EEPROM brightness cells
        // are read. Nothing calls setBrightness() again afterward, so that
        // boot-time scale (compiled default: globalPOTaverage=10) is frozen
        // for the process lifetime, diverging from the LIVE brightness() used
        // by every OP_SHOW/OP_SHOWR/OP_SHOWRND. Golden-forced (mode06_leia
        // frame 0: scale 10 committed vs 20 from an earlier show(brightness())
        // draft here) — do not "fix" this to show(brightness()).
        FastLED.show();
        break;
      case OP_FILL_ROW: {
        uint8_t r = pgm_read_byte(pc++);
        uint8_t c = pgm_read_byte(pc++);
        uint8_t s = pgm_read_byte(pc++);
        fill_row(r, vmColor(c), s);
        break;
      }
      case OP_FILL_COLR: {
        uint8_t st = pgm_read_byte(pc++);
        uint8_t n  = pgm_read_byte(pc++);
        CRGB col = vmColor(pgm_read_byte(pc++));
        while (n--) fill_column(st++, col, 0);
        break;
      }
      case OP_HALFCOLR: {
        uint8_t st   = pgm_read_byte(pc++);
        uint8_t n    = pgm_read_byte(pc++);
        uint8_t half = pgm_read_byte(pc++);
        CRGB col = vmColor(pgm_read_byte(pc++));
        while (n--) fill_half_column(st++, half, col);
        break;
      }
      case OP_SHOWR:
        rep = pgm_read_byte(pc++);
        __attribute__((fallthrough));
      case OP_SHOW: {
        uint16_t d = pgm_read_byte(pc++);
        d |= (uint16_t)pgm_read_byte(pc++) << 8;
        vmShowDelay(d);
        if (g_vmRepLeft == 0) g_vmRepLeft = rep;
        if (--g_vmRepLeft == 0) {
          g_vmPC = pc;
          vmMaybeCountLoop(pc);  // see vmMaybeCountLoop's comment
        }  // else: re-run frame from g_vmPC next time
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
        vmMaybeCountLoop(pc);  // see vmMaybeCountLoop's comment
        return;
      }
      case OP_CLEARWAIT:
        // Mirrors native allOFF(false): FastLED.clear(), no show, no
        // set_delay() call at all — doNext is left exactly as the PRIOR
        // returning op set it. That prior op already made checkDelay()
        // succeed (that is why vmStep() is running this tick), so the very
        // next tick (one interval-gate poll later, ~26 ms) satisfies it
        // again trivially, giving this step exactly one silent tick before
        // whatever follows — the same shape as vmc_radar's own steps, so
        // reusing vmMaybeCountLoop's returning-op peek here (this op DOES
        // return, just without showing) puts the loop decrement on the same
        // tick native's allOFF(false)-then-wrap state does, not one tick
        // early (synchronous with the prior visible SHOW) or a whole
        // set_delay(d) late (OP_END's own tick — see vmMaybeCountLoop's
        // comment for why that specific lateness is the one case that
        // renders a whole extra, unwanted pass).
        FastLED.clear();
        g_vmPC = pc;
        vmMaybeCountLoop(pc);  // see vmMaybeCountLoop's comment
        return;
      case OP_PIX: {
        uint8_t idx = pgm_read_byte(pc++);
        leds[idx] = vmColor(pgm_read_byte(pc++));
        break;
      }
      case OP_FRAME: {
        // Stage only (displayMe=false, runtime=0) — never shows itself, same
        // as every other staging op above; a program's own OP_SHOW/OP_SHOWR/
        // OP_SHOWRND pushes the frame. pgm_read_ptr, not pgm_read_word: see
        // vmPlay()'s pd->code read, below, for why the distinction matters.
        uint8_t b = pgm_read_byte(pc++);
        const uint8_t* pal = vmFramePals[pgm_read_byte(pc++)];
        const byte* bmp = (const byte*)pgm_read_ptr(&vmBitmaps[b]);
        displayMatrixColor(bmp,
                           vmColor(pgm_read_byte(&pal[0])), vmColor(pgm_read_byte(&pal[1])),
                           false, 0,
                           vmColor(pgm_read_byte(&pal[2])), vmColor(pgm_read_byte(&pal[3])),
                           vmColor(pgm_read_byte(&pal[4])));
        break;
      }
      // OP_SPARKLE (Task 11), OP_SCALE_RAND/OP_MUL_RAND (Task 12): case
      // bodies land with their conversion tasks, alongside the programs that
      // emit them — no shipped program emits either of these bytes yet, so
      // this fallthrough is unreachable today. It exists so an opcode byte
      // this build doesn't yet implement can never be misread as extra
      // operand bytes for whatever case follows it in the switch:
      // unknown/not-yet-landed ops end the program safely (same as OP_END)
      // instead of corrupting the frame-cursor walk.
      // (OP_FILL_ROW/OP_FILL_COLR landed Task 8 above — vmc_leia/vmc_swscan/
      // vmc_knight exercise both. OP_HALFCOLR landed Task 9 — vmc_radar.
      // OP_PIX/OP_FRAME landed Task 10 above — vmc_rebel/vmc_pulse.)
      case OP_END:
      default:
        if (g_vmFlags & VMF_ONESHOT) {
          g_vmDone = true;                     // hold display; lifecycle below restores
          return;
        }
        // NOT globalPatternLoops-- here: vmMaybeCountLoop() already did that,
        // synchronously with whichever returning SHOW's cursor landed on this
        // OP_END byte (see its comment, above vmStep()). By the time a
        // loop-EXHAUSTING OP_END would otherwise be reached, vmPlay()'s tail
        // has already seen globalPatternLoops hit zero and (if !alwaysOn)
        // reverted the pattern — so this tick never runs at all, matching
        // native. A MID-loop OP_END still lands here every wrap, same as
        // before; it just no longer duplicates a decrement already made.
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
        //
        // RETURNING-SHOW LOOP INVARIANT: because this wrap CONTINUES instead
        // of returning, a program whose wrap body (from the wrap target — the
        // byte after OP_LOOPSTART, else program start — through OP_END) holds
        // no RETURNING show op (OP_SHOW/OP_SHOWR/OP_SHOWRND; OP_SHOWNOW does
        // not return and does not count) would spin in this loop forever on a
        // watchdogless AVR. Every shipped program is checked for this
        // mechanically — and for having at most one OP_LOOPSTART, since
        // g_vmLoopPtr is a single wrap target — by the decode-walk test
        // (test/host/test_psi_vm.cpp, run.sh stage 2).
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
