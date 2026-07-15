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
// STATUS (this file, Task 6 of that plan): the opcode enum below was decided
// IN FULL through Task 13's original scope — but Task 13 itself found it
// needed one more, OP_SHOWLIVE (see its own comment, below), for the same
// reason Task 9 needed OP_CLEARWAIT: no combination of the existing ops could
// represent what red_heart/lightsaberBattle's native bodies actually do.
// Task 14 added the last two pieces, the contract shims (vmContractScan/
// vmContractSparkle, below) — every mode conversion AND the two contract
// shims are now landed. Task 15 did the docs sweep: CONTRACT_SLIM itself is
// deleted (include/config.h), and the stale comments elsewhere that still
// named scanCol()/DiscoBall() as live primitives now point at these shims.
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
// Bitmap ids + blit palettes for OP_FRAME. Task 10 introduced BM_REBEL/
// BM_PULSE + FP_HEART/FP_PULSE. Task 13 Part 1 appended BM_I/BM_HEART/BM_U —
// I/Heart/U reuse FP_HEART unchanged (displayMatrixColor(LetterI/Heart/
// LetterU, 0xff0000, 0x909497, ...) is the same fg/bg pair rebel already
// uses, verified against i_heart_u/red_heart's own call sites in
// src/main.cpp before that commit deleted them). Task 13 Part 2 (this
// commit) appends BM_LS0..BM_LS21 (the lightsaber's 22 frames) + FP_SABER/
// FP_CLASH — deferred to its own commit so the +1,056 B PROGMEM it revives
// lands only after Part 1's native deletions (i_heart_u/red_heart/march)
// had already shrunk both envs, making the I2C squeeze visible as its own
// step. New ids are always APPENDED (never inserted), same append-only rule
// as the opcode enum below — a shipped program's PROGMEM bytes already bake
// in the numeric id, so renumbering an earlier id would silently corrupt it.
// BM__COUNT / FP__COUNT are zero-cost sentinels (compile-time constants only,
// never stored): the decode-walk test bounds-checks every OP_FRAME operand
// against them, and the static_asserts below pin each table's row count to
// its enum — so adding an enumerator without its table row (or vice versa)
// fails the BUILD, not a later golden run. The vmBitmaps POINTER table
// itself — which must reference the real PROGMEM byte arrays declared in
// matrices.h — lives inside the PSI_VM_TABLES_ONLY fence below, next to
// vmColor(). See that table's own comment for why: unlike a mere
// declaration, an initialized global pointer table is real data that needs
// those symbols to actually be DEFINED in the same translation unit, which
// is true in the firmware build (matrices.h precedes this header, main.cpp
// ~line 289 vs ~line 359) but not in the decode-walk test
// (PSI_VM_TABLES_ONLY, no matrices.h).
enum {
  BM_REBEL = 0, BM_PULSE, BM_I, BM_HEART, BM_U,
  BM_LS0, BM_LS1, BM_LS2, BM_LS3, BM_LS4, BM_LS5, BM_LS6, BM_LS7, BM_LS8,
  BM_LS9, BM_LS10, BM_LS11, BM_LS12, BM_LS13, BM_LS14, BM_LS15, BM_LS16,
  BM_LS17, BM_LS18, BM_LS19, BM_LS20, BM_LS21,
  BM__COUNT
};

// Bitmap blit palettes: {fg, bg, color2, color3, color4} color ids, indexed
// by OP_FRAME's palId operand. FP_SABER/FP_CLASH verified against
// lightsaberBattle's own displayMatrixColor call sites (src/main.cpp, before
// this commit deleted it): every state but the two clashes calls
// displayMatrixColor(lightsaberN, 0xffffff, 0x000000, true, 0, 0xff0000,
// 0x0000cc) — fg=white, bg=black, color2=red, color3=blue(SABBLUE); the two
// clash states (native cases 6/15) add a 7th arg, 0x999999 (GREY99), as
// color4.
enum { FP_HEART = 0, FP_PULSE, FP_SABER, FP_CLASH, FP__COUNT };
const uint8_t vmFramePals[][5] PROGMEM = {
  { VC_RED,     VC_GREYBG,  VC_K,   VC_K,       VC_K },       // rebel (mode 14); also I/Heart/U's fg/bg
  { VC_PULSEFG, VC_PULSEBG, VC_K,   VC_K,       VC_K },       // pulse trace (mode 9 rear)
  { VC_W,       VC_K,       VC_RED, VC_SABBLUE, VC_K },       // lightsaber (mode 19)
  { VC_W,       VC_K,       VC_RED, VC_SABBLUE, VC_GREY99 },  // lightsaber clash (states 6/15)
};
static_assert(sizeof(vmFramePals) / sizeof(vmFramePals[0]) == FP__COUNT,
              "vmFramePals must have exactly one row per FP_* id (FP__COUNT)");

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
                   // — reserved: no shipped program uses this op yet (swipe,
                   // the one native mode with a randomized delay, stayed native)
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
  // OP_SHOWLIVE is the second genuine, unplanned addition (an 18th op),
  // added here in Task 13 for the same class of reason OP_CLEARWAIT was:
  // no combination of the existing ops represents what red_heart() and
  // lightsaberBattle() actually do. Both call displayMatrixColor(...,
  // displayMe=true, ...) — which itself calls FastLED.show(brightness())
  // — and THEN, unconditionally, their own shared
  // `if (updateLed) { FastLED.show(brightness()); set_delay(time_delay); }`
  // tail fires too, since updateLed is set regardless of displayMe. That is
  // a real double show(brightness()) in the SAME checkDelay()-gated tick —
  // proven against the golden, not assumed: mode09_heart_f.psig frames 1-2
  // are bit-identical, same t_ms, both scale 20 (LIVE brightness, i.e. the
  // EEPROM-derived value, not the frozen boot-time scale OP_SHOWNOW's bare
  // FastLED.show() would render — this golden's replay row uses an internal
  // brightness of 20 while the compiled boot default is 10, so the two are
  // NOT numerically interchangeable here, ruling out reusing OP_SHOWNOW for
  // this shape). OP_SHOWLIVE closes that gap: like OP_SHOWNOW it shows and
  // falls through without arming a delay or returning, but with LIVE
  // brightness() instead of a bare call — mirroring displayMatrixColor's own
  // displayMe=true branch exactly. A program pairs it with a normal OP_SHOW
  // right after (see vmc_redheart/vmc_saber) to reproduce the second,
  // delay-arming show. Nothing about vmStep()'s existing ops, loop
  // mechanics, or any shipped program changes; this is additive only.
  OP_SHOWLIVE,     //                       show(brightness()) now, KEEP RUNNING (no set_delay, no return)
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
// once the tick OP_CLEARWAIT was missing is actually represented. The
// golden's observed steady-state cadence (286 ms, not native's literal 250)
// is exactly two grid quantizations, not one: the 250 ms V_SHOW delay isn't
// satisfied until the next ~26 ms poll on or after it elapses, i.e. it
// quantizes UP to 260 ms (10 x 26); OP_CLEARWAIT's own silent tick costs one
// more full poll on top of that (+26); 260 + 26 = 286. This is the same
// arithmetic Dead end 2's brute-force search over `V_SHOW(D)` rediscovered
// empirically, per step, before OP_CLEARWAIT made it literal.
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

// Modes 12/13: DiscoBall(150, ., 3, CRGB::Grey, .) — shared program, two
// descriptors (mode 12: loops=30, runtime=4; mode 13: loops=0, runtime=0).
// Full native trace read line by line before any bytecode was written (per
// the Task 9 process lesson) — this is the FIRST random()-consuming
// conversion, so the golden's exact Park-Miller draw sequence is
// load-bearing (see vmSparkleDraw, above, for the row/col draw order).
//
// Entry — firstTime block: globalPatternLoops = loops!=0 ? loops*2 : 2;
// ledPatternState = 0; allOFF(true) (clear + bare show, frozen boot scale) —
// same VMF_CLEAR shape vmc_radar/vmc_pulse already use (golden frame 0: t=1,
// scale=10, all-black; frame 1: SAME t=1, scale=20, first sparkle content —
// both land in vmPlay()'s one firstTime call).
//
// Steady-state — every checkDelay()-gated tick (native delay 150 ms,
// quantized to the ~26 ms poll grid; golden shows a steady +156 ms cadence
// throughout both modes, no drift): ledPatternState toggles 0/1 every tick.
// State 0 draws numSparkles=3 random pixels (vmSparkleDraw) and sets
// updateLed=1; state 1 is `allOFF(false)` (FastLED.clear(), no show of its
// own) and ALSO sets updateLed=1 — so, unlike radar's inter-quadrant blank
// (Task 9's OP_CLEARWAIT), DiscoBall's blank tick is NOT invisible: native's
// shared `if (updateLed) { FastLED.show(brightness()); set_delay(time_delay);
// }` tail fires for BOTH states every time, so a plain OP_CLEAR + OP_SHOW(150)
// reproduces the blank tick exactly, no new opcode needed. Verified against
// the golden dump directly before writing bytecode: lit/blank frames
// alternate at a steady +156 ms cadence throughout mode12_disco4.psig (26
// disco frames: 1 entry blackout + 13 sparkle/blank pairs) and
// mode13_discoinf.psig (53 frames, full 8 s capture, never diverging) — no
// extra invisible tick between states, confirming this simpler two-state
// shape (unlike radar's) needs nothing beyond OP_CLEAR/OP_SHOW.
//
// LOOP ACCOUNTING (verified, not assumed — worked an example rather than
// eyeballed, but this is the GENERAL consequence of the VM's counting
// mechanism, not a divergence special to DiscoBall): native decrements
// globalPatternLoops on EVERY tick (sparkle AND blank), unconditionally — a
// full sparkle+blank cycle costs TWO decrements — and the firstTime block's
// `loops * 2` seed exists to cancel that out (loops=30 native param ->
// globalPatternLoops=60 -> 60 ticks / 2 per cycle = 30 full cycles).
// vmMaybeCountLoop instead decrements ONCE per full lap (it peeks the byte
// after a returning SHOW; only the blank tick's SHOW here is immediately
// followed by OP_END, so only that tick's cursor trips the peek) — so the
// descriptor's loops field is encoded as 30, native's raw `loops` parameter
// UNDOUBLED, not 60. That is not a special case: for any native mode whose
// raw-tick counter is seeded `loops * N` for an N-tick lap, a once-per-lap
// counter seeded with the same raw, undoubled `loops` reaches zero on the
// identical lap by construction — vmc_march's 2-tick lap (mode 11, see its
// own comment) is encoded the exact same way, with no fanfare. This has ZERO
// observable effect on mode 12 either way: runtime=4 is nonzero, so vmPlay()'s tail
// ALWAYS takes the globalTimerDonedoRestoreDefault branch — native's own
// epilogue has the identical `if ((runtime==0) && !timingReceived) { if
// (loops) ... } else { globalTimerDonedoRestoreDefault(); }` shape, so the
// loop-count branch is UNREACHABLE for mode 12 in native too. Mode 12 always
// reverts on the 4 s wall-clock timeout, never on loop exhaustion (60 native
// ticks * 150 ms =~9 s, well past the 4 s deadline) — confirmed against the
// golden: mode12_disco4.psig frame 27 (t=4030) is the first frame of the
// REVERTED default pattern (swipe), not a 27th disco frame; only 26 disco
// frames ever render, matching set_global_timeout(4)'s deadline (armed at
// t=1, expires 4001, first satisfied on the ~26 ms grid at t=4030) exactly.
// Mode 13 (loops=0, runtime=0) sets g_vmDescLoops=0, so vmMaybeCountLoop's
// decrement never fires at all (its own `g_vmDescLoops &&` guard) and
// vmPlay()'s tail never calls loopsDonedoRestoreDefault() either (`if
// (g_vmDescLoops)`) — indefinite, exactly mirroring native's own `if (loops)`
// gate around loopsDonedoRestoreDefault(), which is likewise always false
// when the loops PARAMETER is 0 (native's uint8_t globalPatternLoops counter
// still silently decrements/underflows forever under the hood; ours simply
// never decrements at all — both are unobservable no-ops for reversion,
// since neither is ever consulted when loops==0). Golden confirms:
// mode13_discoinf.psig never diverges from the steady sparkle/blank cadence
// for the full 8 s capture (53 frames), no reversion.
const uint8_t vmc_disco[] PROGMEM = {
  V_SPARK(3, VC_GREY), V_SHOW(150), OP_CLEAR, V_SHOW(150), OP_END,
};

// Mode 4: Short circuit — was FadeOut(257, 3). Native (src/main.cpp, deleted by
// this task — line numbers below are current, NOT the stale 1633-1702 an
// earlier note cited; Tasks 8-11 each deleted code above this point and
// shifted everything down) does two things per checkDelay()-gated tick,
// alternating on `ledPatternState`: state 0 dims every LED
// (`leds[x].nscale8_video(random(220,250))`, x = 0..NUM_LEDS-1, ONE random()
// call per LED, draw-then-write in index order — this is the FIRST
// SCALE_RAND/MUL_RAND conversion, so that order is load-bearing exactly like
// DiscoBall's row/col draw order was in Task 11); state 1 brightens every LED
// (`leds[x] *= random(0,6)`, same per-LED order — CRGB::operator*= is
// per-channel qmul8, NOT nscale8_video, so this pass can push a channel UP,
// unlike the dim pass, which is monotonic non-increasing by construction).
// No entry clear — FadeOut fades whatever is ALREADY on the panel (the golden
// row's 7th column feeds `0T4` at t=3000ms into a live default-swipe buffer,
// mode04_shortcirc.psig frame 115 is the first FadeOut-touched frame, not an
// all-black one). No runtime/timeout either — native never calls
// set_global_timeout, and ignores any command timing it was sent (the
// firstTime block force-clears `timingReceived`) — hence VMF_IGNORE_TIMING.
//
// THE QUIRK — "the really ugly way of counting loops" (native's own comment,
// main.cpp ~line 1127): with case0count=4, case1count=8, loops=3,
// totalLoopCount = 3*(4+8) = 36. globalPatternLoops seeds to 36 and
// decrements ONCE PER TICK, unconditionally. ledPatternState toggles via four
// `if (globalPatternLoops == totalLoopCount - K)` checks (K = 4, 12, 16, 24)
// — evaluated AFTER that tick's dim/bright pass already ran but BEFORE that
// tick's own decrement — so a threshold match toggles the state for the
// *next* tick, not the current one. Only four thresholds are coded, covering
// cumulative offsets 4/12/16/24 out of a "clean" 3x(4+8)=36 total: they carry
// the state machine through 1.5 of the intended 3 cycles (dim4, bright8,
// dim4, bright8) and then simply STOP — no threshold exists for offsets
// 28/36, so once the 4th toggle fires (at globalPatternLoops==12) the state
// flips to dim and never flips back; it just keeps dimming every remaining
// tick until globalPatternLoops hits 0. Simulated exactly against the source
// (not eyeballed) and cross-checked against every one of the golden's 36
// FadeOut frames (mode04_shortcirc.psig, frames 115-150 inclusive — frame
// 115 lands at t=3000, the exact tick the `0T4` command was fed, because
// checkDelay()'s leftover `doNext` from the prior swipe pattern had already
// expired; frame 151, t=12142, is the FIRST FRAME OF THE REVERTED DEFAULT
// SWIPE, back at a steady 26 ms cadence, sum=12240 = fully lit again — not a
// 37th FadeOut frame): the ACTUAL pass sequence is five blocks, not six —
//   ticks  1- 5 (frames 115-119): DIM   x5   (not 4 — the off-by-one above)
//   ticks  6-13 (frames 120-127): BRIGHT x8
//   ticks 14-17 (frames 128-131): DIM   x4
//   ticks 18-25 (frames 132-139): BRIGHT x8
//   ticks 26-36 (frames 140-150): DIM   x11  (the un-thresholded tail —
//                                              replaces what a clean reading
//                                              would have rendered as a
//                                              third dim4+bright8 cycle)
// 5+8+4+8+11 = 36 total ticks, matching totalLoopCount exactly; 20 dim + 16
// bright passes overall, vs. the clean (4 dim + 8 bright)x3 = 12 dim + 24
// bright a straight LOOPSTART/loops=3 encoding would produce (which is
// exactly the probe's shape, and exactly what golden_compare.py rejects).
// Golden frame content confirms the block shape independent of the tick
// count too: every predicted DIM block (115-119, 128-131, 140-150) is
// strictly non-increasing in total LED byte-sum tick over tick — the only
// possible signature of nscale8_video, which can never raise a channel — and
// the two BRIGHT blocks are exactly where that monotonicity breaks.
//
// Encoding: rather than reproduce native's threshold arithmetic (the VM's
// own loop accounting is structurally different — vmMaybeCountLoop decrements
// once per LAP via an OP_END peek, not once per raw tick, so there is no
// "globalPatternLoops mid-sequence value" to threshold-compare against inside
// the VM at all), this bakes the five observed blocks straight into the
// program as five OP_SCALE_RAND/OP_MUL_RAND + OP_SHOWR(rep) pairs.
// OP_SHOWR's rep mechanism does not advance g_vmPC until its last rep, so
// vmStep() restarts each intervening tick from the block's OWN staging op —
// i.e. every rep re-runs OP_SCALE_RAND/OP_MUL_RAND fresh, drawing 48 NEW
// random() values and re-applying them to the (already-mutated) buffer —
// exactly mirroring native's per-tick redraw-in-place. loops=1 in the
// descriptor below: the ONLY point in this straight-line program where a
// returning SHOW's peek lands on OP_END is the last rep of the final
// (11-tick) dim block, so a single decrement (1 -> 0) there is exactly
// enough to trip loopsDonedoRestoreDefault() on that same tick — matching
// the golden's immediate revert at frame 151, one tick later, no extra pass.
const uint8_t vmc_fadeout[] PROGMEM = {
  OP_SCALE_RAND, 220, 250, V_SHOWR(5, 257),
  OP_MUL_RAND, 6,          V_SHOWR(8, 257),
  OP_SCALE_RAND, 220, 250, V_SHOWR(4, 257),
  OP_MUL_RAND, 6,          V_SHOWR(8, 257),
  OP_SCALE_RAND, 220, 250, V_SHOWR(11, 257),
  OP_END,
};

// Mode 7: I <heart> U — was i_heart_u(500, 3, 0). Native (src/main.cpp,
// deleted by this task): firstTime sets globalPatternLoops = loops (3, NOT
// doubled — contrast vmc_march below) and allOFF(true) (VMF_CLEAR shape,
// golden frame 0: t=1, scale=10, all-black). Six ledPatternState steps per
// lap, each a SINGLE show (unlike red_heart below): 0=LetterI, 1=blank,
// 2=Heart, 3=blank, 4=LetterU, 5=blank — every displayMatrixColor call here
// passes displayMe=false (`displayMatrixColor(LetterI, 0xff0000, 0x909497,
// false, 0)`), so only the shared `if (updateLed) { FastLED.show(brightness());
// set_delay(time_delay); }` tail shows, once per state — no OP_SHOWLIVE
// needed here (contrast red_heart, which passes displayMe=true). time_delay
// is the constant 500 param throughout; no per-state override exists in this
// function. Loop decrements once per 6-state lap (state wraps 5->0), matching
// VMF_CLEAR's default wrap-point (program start — no OP_LOOPSTART needed,
// the whole 6-state program IS the loop body) with loops=3 directly.
// Golden-verified: mode07_iheartu.psig has 18 content frames (6 states x 3
// loops) after the entry blackout, then frame 19 (t=8866) is the first
// reverted swipe frame.
const uint8_t vmc_iheartu[] PROGMEM = {
  V_FRAME(BM_I, FP_HEART),     V_SHOW(500),
  OP_CLEAR,                    V_SHOW(500),
  V_FRAME(BM_HEART, FP_HEART), V_SHOW(500),
  OP_CLEAR,                    V_SHOW(500),
  V_FRAME(BM_U, FP_HEART),     V_SHOW(500),
  OP_CLEAR,                    V_SHOW(500),
  OP_END,
};

// Mode 9 (front): flashing red heart — was red_heart(500, 3, 0). Same
// six-state skeleton as vmc_iheartu (globalPatternLoops = loops directly, no
// doubling; VMF_CLEAR entry), but EVERY displayMatrixColor call here passes
// displayMe=true (`displayMatrixColor(Heart, 0xff0000, 0x909497, true, 0)`)
// on states 0/2/4 — so those three states show TWICE in the same tick: once
// inside displayMatrixColor itself (displayMe=true's own `FastLED.show
// (brightness())`), then again via the shared updateLed tail, exactly like
// every other state's single show. This is genuinely a live-brightness
// double show, not OP_SHOWNOW's frozen-boot-scale one (see OP_SHOWLIVE's
// enum comment for the golden proof) — encoded as OP_SHOWLIVE immediately
// after the frame stage, then a normal V_SHOW(500) for the second show +
// delay. States 1/3/5 (allOFF(false), displayMe N/A) show once, same as
// vmc_iheartu's blanks. Golden-verified: mode09_heart_f.psig frames 1-2 are
// bit-identical duplicates (t=1, scale=20 — LIVE, not the frozen boot value
// 10), and the full capture has 27 content frames (9 double-shows + 9
// single blanks, 3 loops x 3 heart-states) before frame 28 (t=8866) reverts
// to swipe — matching loops=3 directly, same accounting as vmc_iheartu.
const uint8_t vmc_redheart[] PROGMEM = {
  V_FRAME(BM_HEART, FP_HEART), OP_SHOWLIVE, V_SHOW(500),
  OP_CLEAR,                                 V_SHOW(500),
  V_FRAME(BM_HEART, FP_HEART), OP_SHOWLIVE, V_SHOW(500),
  OP_CLEAR,                                 V_SHOW(500),
  V_FRAME(BM_HEART, FP_HEART), OP_SHOWLIVE, V_SHOW(500),
  OP_CLEAR,                                 V_SHOW(500),
  OP_END,
};

// Mode 11: Imperial March — was march(0xffffff, 552, 42, 47). Native
// (src/main.cpp, deleted by this task): firstTime sets globalPatternLoops =
// loops * 2 (84) and allOFF(true) (VMF_CLEAR, golden frame 0: t=1, scale=10,
// all-black, followed SAME TICK by the first content frame — golden frame 1
// is also t=1). Two ledPatternState steps, toggling every tick: state 0
// lights columns 0-4 white / 5-9 black; state 1 the mirror image. Unlike
// every other converted mode, globalPatternLoops decrements EVERY tick here
// (both states), unconditionally, right after the toggle — a full 2-state
// lap costs native TWO decrements, so the native `loops` PARAMETER (42) is
// what vmMaybeCountLoop's once-per-lap accounting should encode directly
// (42 laps x 2 ticks/lap = 84 ticks, matching native's raw 84-tick budget
// exactly) — no OP_LOOPSTART needed, the whole 2-state program is the loop
// body. In practice this loop count is UNOBSERVABLE either way: runtime=47
// is nonzero, so both native's tail (`if ((runtime==0)&&(!timingReceived))
// ... else globalTimerDonedoRestoreDefault();`) and vmPlay()'s identical-
// shape tail always take the timer branch, never the loop-count branch —
// confirmed against the golden: mode11_march.psig shows content through
// frame 83 (t=46,...) and reverts at frame 84 (t=47034), well short of a
// theoretical 84-tick loop exhaustion (which 552 ms/tick would reach around
// t=46,368 give or take — close, but it's the 47 s wall clock that actually
// fires first, same shape as mode 12/14's timer-always-wins precedent).
const uint8_t vmc_march[] PROGMEM = {
  V_FCOLS(0, 5, VC_W), V_FCOLS(5, 5, VC_K), V_SHOW(552),
  V_FCOLS(0, 5, VC_K), V_FCOLS(5, 5, VC_W), V_SHOW(552),
  OP_END,
};

// Mode 19: Lightsaber Battle — was lightsaberBattle(250). Native
// (src/main.cpp, deleted by this task): firstTime sets ledPatternState=0,
// allOFF(true) (VMF_CLEAR), and unconditionally forces timingReceived=false
// (this sequence never honors a received command timing — same shape as
// FadeOut's VMF_IGNORE_TIMING, though moot here since g_vmRuntime is 0 and
// never read for a oneshot's reversion; omitted, see below). 23 states
// (cases 0-22), each a displayMatrixColor(lightsaberN, 0xffffff, 0x000000,
// true, 0, 0xff0000, 0x0000cc[, 0x999999]) call — displayMe=true, so EVERY
// state double-shows exactly like red_heart (OP_SHOWLIVE then a normal
// V_SHOW), golden-proven: mode19_saber.psig's first 46 frames are 23
// bit-identical same-t_ms pairs. Bitmap sequence: ls0..ls5, ls6(CLASH,
// FP_CLASH, this state's frame ALSO overrides time_delay to 100 for the
// gap to the next frame), ls4 again (reused, not ls6's neighbor — verified
// against case 7's literal `lightsaber4` argument), ls7..ls13, ls14(CLASH,
// same 100 ms override), ls15..ls21 (this last state also overrides
// time_delay, to 500). Case 23 (ledPatternState=99, updateLed=0 — no show,
// no set_delay call at all) is a native "do nothing visible, but still
// consume exactly one tick" step, same shape as StarWarsIntro's case 8
// below — but unlike that one, lightsaberBattle has no loop to wrap back
// into: vmStep()'s own OP_END/VMF_ONESHOT branch (`g_vmDone=true; return;`,
// already landed in the core — see vmPlay()'s tail) already shows nothing
// and returns on this same tick, so no new opcode is needed here; the plain
// OP_END after the last V_SHOW(500) reproduces case 23 exactly for free.
// Golden-verified down to the exact transition tick: mode19_saber.psig's
// pair-22 (ls21, native case 22) lands at t=5408, and the first reverted
// swipe frame lands at t=5954 — 546 ms later (500 native delay + ~20 ms
// grid quantization + one genuine extra ~26 ms tick for the no-op OP_END
// call, exactly mirroring native's case-23 tick before its own
// `if ((ledPatternState==99)&&(!alwaysOn))` revert). VMF_ONESHOT's hold
// semantics (g_vmDone latched, vmStep() never called again once alwaysOn
// keeps the revert from firing) reproduce mode19_alwayson.psig exactly:
// that capture's LAST frame pair also lands at t=5408 and the .psig simply
// has no more frame records after it — no more show() calls, ever, matching
// "hold = no more show() calls" precisely.
const uint8_t vmc_saber[] PROGMEM = {
  V_FRAME(BM_LS0,  FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS1,  FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS2,  FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS3,  FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS4,  FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS5,  FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS6,  FP_CLASH), OP_SHOWLIVE, V_SHOW(100),
  V_FRAME(BM_LS4,  FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS7,  FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS8,  FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS9,  FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS10, FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS11, FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS12, FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS13, FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS14, FP_CLASH), OP_SHOWLIVE, V_SHOW(100),
  V_FRAME(BM_LS15, FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS16, FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS17, FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS18, FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS19, FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS20, FP_SABER), OP_SHOWLIVE, V_SHOW(250),
  V_FRAME(BM_LS21, FP_SABER), OP_SHOWLIVE, V_SHOW(500),
  OP_END,
};

// Mode 20: Star Wars Intro Text — was StarWarsIntro(500, 4, 0xC8AA00, 10).
// Native (src/main.cpp, deleted by this task): firstTime sets
// globalPatternLoops=loops (4), allOFF(true) (VMF_CLEAR), and
// ledPatternState=2 — cases 0/1 are DEAD CODE (unreachable: nothing ever
// sets the state to 0 or 1). Real flow is a 4-frame prologue (cases 2-5,
// each shown once) then a 2-frame loop (cases 6-7) separated by a silent
// no-show "case 8" tick that decrements the loop and jumps back to case 6 —
// structurally identical to lightsaberBattle's case 23 above, EXCEPT this
// one loops (case 8 sets ledPatternState back to 6, not a terminal state),
// so it genuinely needs a returning, no-show, delay-preserving tick between
// laps: OP_CLEARWAIT (Task 9), reused here for a second, unrelated purpose
// (its own FastLED.clear() is harmless — case 6, the very next thing to
// run, clears again itself before drawing anything, so no frame ever
// observes case 8's intermediate buffer state). Golden-verified: the
// case7->case6 (via case 8) transition is consistently ~546 ms
// (500 + ~20 ms grid quantization + one genuine extra ~26 ms tick for
// OP_CLEARWAIT), while every other transition (prologue-to-prologue,
// case6->case7) is ~520 ms (500 + quantization only, no extra tick) —
// mode20_swintro.psig frames 5->6 (2080->2600, 520) vs 6->7 (2600->3146,
// 546) prove the asymmetry directly. Per-row scales (fill_row's third arg,
// CRGB %= semantics) and knock-out pixel lists transcribed directly from
// the native switch, case by case, in source order (draw-then-knock-out,
// matching native's own sequential leds[] writes). loops=4 is native's raw
// parameter, encoded for fidelity though unobservable: runtime=10 is
// nonzero, so vmPlay()'s tail always takes the timer branch (same
// timer-always-wins shape as vmc_march/vmc_rebel/vmc_disco4), confirmed
// against the golden — content runs through frame 19 (t=9542, mid-case-6)
// and reverts at frame 20 (t=10036), matching the 10 s deadline, not a
// loop-count boundary.
const uint8_t vmc_swintro[] PROGMEM = {
  V_FROW(4, VC_GOLD, 0), V_SHOW(500),                                     // case 2
  OP_CLEAR, V_SHOW(500),                                                  // case 3
  V_FROW(3, VC_GOLD, 0), V_PIX(33, VC_K), V_PIX(24, VC_K), V_SHOW(500),   // case 4
  OP_CLEAR, V_FROW(4, VC_GOLD, 0), V_FROW(2, VC_GOLD, 100),               // case 5
    V_PIX(14, VC_K), V_PIX(15, VC_K), V_PIX(22, VC_K), V_PIX(23, VC_K), V_SHOW(500),
  OP_LOOPSTART,
  OP_CLEAR, V_FROW(3, VC_GOLD, 0), V_PIX(33, VC_K), V_PIX(24, VC_K),      // case 6
    V_FROW(2, VC_GOLD, 100),
    V_PIX(14, VC_K), V_PIX(15, VC_K), V_PIX(22, VC_K), V_PIX(23, VC_K),
    V_FROW(1, VC_GOLD, 20),
    V_PIX(13, VC_K), V_PIX(12, VC_K), V_PIX(7, VC_K), V_PIX(6, VC_K), V_SHOW(500),
  OP_CLEAR, V_FROW(4, VC_GOLD, 0),                                        // case 7
    V_FROW(2, VC_GOLD, 100),
    V_PIX(14, VC_K), V_PIX(15, VC_K), V_PIX(22, VC_K), V_PIX(23, VC_K),
    V_FROW(1, VC_GOLD, 20),
    V_PIX(13, VC_K), V_PIX(12, VC_K), V_PIX(7, VC_K), V_PIX(6, VC_K),
    V_FROW(0, VC_GOLD, 12),
    V_PIX(0, VC_K), V_PIX(1, VC_K), V_PIX(4, VC_K), V_PIX(5, VC_K), V_SHOW(500),
  OP_CLEARWAIT,                                                          // case 8 (silent, loops to case 6)
  OP_END,
};

// Mode 22 (fork addition, unused by upstream — see the dispatch case in
// src/main.cpp): "processing sweep" — a gold column wipe left-to-right, then
// a white full-panel blink. Not a conversion of any native mode; there is no
// native reference to trace, so THIS PROGMEM array + its descriptor below
// (five bytes: code pointer, loops, runtime, flags) ARE the reference —
// captured straight into test/host/golden/ as a regression lock, same as
// every converted program's golden, just without a native golden to diff
// against first. Exists to make the marginal-cost claim in README's "The
// animation VM: modes are data now" section measurable, not just asserted:
// the flash delta THIS commit produces (this array + the descriptor row +
// one dispatch case, nothing else touched) is the price of one new
// animation. loops=3 (three full column-wipe-then-blink laps; VMF_CLEAR's
// entry blackout only fires once, on firstTime, same as every other
// VMF_CLEAR program above — not repeated per lap) and runtime=0 (no timer;
// the descriptor's loops field alone drives reversion via
// loopsDonedoRestoreDefault(), exactly like vmc_pulse/vmc_iheartu/
// vmc_redheart above), both authored choices, not measurements of anything
// native. VMF_CLEAR gives it the same "blackout, then first content frame"
// entry shape every other converted program above already has, so it reads
// as one more member of the family rather than a special case.
const uint8_t vmc_process[] PROGMEM = {
  OP_CLEAR, V_FCOLS(0, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(1, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(2, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(3, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(4, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(5, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(6, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(7, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(8, 1, VC_GOLD), V_SHOW(120),
  V_FCOLS(9, 1, VC_GOLD), V_SHOW(120),
  OP_FILL_ALL, VC_W, V_SHOW(150), OP_CLEAR, V_SHOW(150), OP_END,
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
  VMP_REBEL, VMP_PULSE, VMP_DISCO4, VMP_DISCOINF, VMP_FADEOUT,
  VMP_IHEARTU, VMP_REDHEART, VMP_MARCH, VMP_SABER, VMP_SWINTRO,
  // VMP_PROCESS (Task 16, fork addition — not an upstream mode; see vmc_process's
  // own comment above and the mode 22 dispatch case in src/main.cpp) is appended
  // last, after every conversion task's id, same append-only rule as VC_*/BM_*/
  // FP_*/the opcode enum: a shipped id's numeric value must never move.
  VMP_PROCESS,
};

const VmProg vmProgs[] PROGMEM = {
  { vmc_flash60,  24, 4,  0 },                       // VMP_FLASH    (mode 2)  — was flash(0xffffff, 60, 24, 4)
  { vmc_flash125, 15, 4,  0 },                       // VMP_ALARM    (modes 3/5) — was flash(0xffffff, 125, 15, 4)
  { vmc_leia,     57, 34, 0 },                       // VMP_LEIA     (mode 6)  — was Cylon_Row(0xcccccc, 74, 3, 57, 34)
  { vmc_swscan,    5, 0,  0 },                       // VMP_SWSCAN   (mode 10) — was Cylon_Row(0xC8AA00, 500, 4, 5, 0)
  { vmc_knight,    5, 0,  0 },                       // VMP_KNIGHT   (mode 15) — was Cylon_Col(0xff0000, 250, 1, 5, 0)
  { vmc_radar,     6, 0,  VMF_CLEAR },               // VMP_RADAR    (mode 8)  — was radar(0xff0000, 250, 6, 0)
  { vmc_rebel,     0, 5,  0 },                       // VMP_REBEL    (mode 14) — was displayMatrixColor(rebel, ..., true, 5)
  { vmc_pulse,     3, 0,  VMF_CLEAR },               // VMP_PULSE    (mode 9 rear) — was Pulse(100, 3, 0)
  { vmc_disco,    30, 4,  VMF_CLEAR },               // VMP_DISCO4   (mode 12) — was DiscoBall(150, 30, 3, CRGB::Grey, 4)
  { vmc_disco,     0, 0,  VMF_CLEAR },               // VMP_DISCOINF (mode 13) — was DiscoBall(150, 0, 3, CRGB::Grey, 0)
  { vmc_fadeout,   1, 0,  VMF_IGNORE_TIMING },       // VMP_FADEOUT  (mode 4)  — was FadeOut(257, 3) — see vmc_fadeout's comment for the loop-tail quirk
  { vmc_iheartu,   3, 0,  VMF_CLEAR },               // VMP_IHEARTU  (mode 7)  — was i_heart_u(500, 3, 0)
  { vmc_redheart,  3, 0,  VMF_CLEAR },               // VMP_REDHEART (mode 9 front) — was red_heart(500, 3, 0)
  { vmc_march,    42, 47, VMF_CLEAR },               // VMP_MARCH    (mode 11) — was march(0xffffff, 552, 42, 47)
  { vmc_saber,     0, 0,  VMF_CLEAR | VMF_ONESHOT }, // VMP_SABER    (mode 19) — was lightsaberBattle(250)
  { vmc_swintro,   4, 10, VMF_CLEAR },               // VMP_SWINTRO  (mode 20) — was StarWarsIntro(500, 4, 0xC8AA00, 10)
  { vmc_process,   3, 0,  VMF_CLEAR },               // VMP_PROCESS  (mode 22) — fork addition, no native equivalent; see vmc_process's comment
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

// OP_FRAME's bitmap pointer table — indexed by BM_REBEL/BM_PULSE/BM_I/
// BM_HEART/BM_U/BM_LS0..BM_LS21 (declared with the rest of the bitmap/
// palette tables, above). Lives inside this fence, unlike vmFramePals/
// BM_*/FP_*, because it is a POINTER table: each entry is the real address
// of `rebel`/`pulse`/`LetterI`/`Heart`/`LetterU`/`lightsaber0..21`, the
// PROGMEM byte arrays matrices.h declares — real data, not just a
// declaration, so linking it requires those symbols to actually be DEFINED
// in this translation unit. The firmware build satisfies that (matrices.h
// precedes this header's include point, main.cpp ~line 289 vs ~line 359);
// the decode-walk test (PSI_VM_TABLES_ONLY) does not include matrices.h at
// all, which is exactly why this table — like vmColor() above, for the same
// "needs firmware-only symbols" reason — sits behind the fence instead of
// with the other tables. Referencing lightsaber0..21 here is what actually
// revives their 48 B-each PROGMEM bitmaps (+1,056 B total) — the enum ids
// above cost nothing by themselves; this is the I2C squeeze moment Task 13
// split into its own commit for.
const byte* const vmBitmaps[] PROGMEM = {
  rebel, pulse, LetterI, Heart, LetterU,
  lightsaber0, lightsaber1, lightsaber2, lightsaber3, lightsaber4,
  lightsaber5, lightsaber6, lightsaber7, lightsaber8, lightsaber9,
  lightsaber10, lightsaber11, lightsaber12, lightsaber13, lightsaber14,
  lightsaber15, lightsaber16, lightsaber17, lightsaber18, lightsaber19,
  lightsaber20, lightsaber21,
};
static_assert(sizeof(vmBitmaps) / sizeof(vmBitmaps[0]) == BM__COUNT,
              "vmBitmaps must have exactly one entry per BM_* id (BM__COUNT)");

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

// DiscoBall's per-sparkle draw (Task 11 — the FIRST random()-consuming
// conversion). Native (src/main.cpp DiscoBall(), ledPatternState case 0,
// currently lines 1261-1262 — the brief's cited 1820-1821 is stale, from
// before Tasks 9/10 deleted radar()/Pulse() above this point and shifted
// everything down; re-read from the live file, not trusted from the brief):
//   randRow = random(0, LEDS_PER_COLUMN);
//   randCol = random(0, COLUMNS);
//   ledIndex = ledMatrix[randCol][randRow];
//   if (ledIndex != -1) leds[ledIndex] = color;
// — ROW drawn before COLUMN, one full row+col pair PER sparkle, interleaved
// across the numSparkles loop (draw1-row, draw1-col, draw2-row, draw2-col,
// ...), never batched (all rows then all cols). The golden replays the exact
// avr-libc Park-Miller random() sequence DiscoBall's calls produced, so this
// order is load-bearing, not stylistic. A -1 "hole" position still CONSUMES
// its row+col draw pair — native's `if (ledIndex != -1)` guards only the
// leds[] write, not the two random() calls feeding it — so the `if` below
// wraps only the write too, exactly mirroring that. Verified against the
// golden's first sparkle frame before this was ever compiled: mode12_disco4
// .psig frame 1 (t=1) lights exactly 1 LED (idx 40) despite numSparkles=3 —
// two of the three draws landed on holes and were silently skipped, matching
// bit-for-bit once vmStep()'s OP_SPARKLE case (below) runs this 3 times.
static void vmSparkleDraw(uint8_t n, CRGB col) {
  while (n--) {
    int randRow = random(0, LEDS_PER_COLUMN);
    int randCol = random(0, COLUMNS);
    int8_t ledIndex = ledMatrix[randCol][randRow];
    if (ledIndex != -1) leds[ledIndex] = col;
  }
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
      case OP_SHOWLIVE:
        // show(brightness()), NOT a bare show() — mirrors displayMatrixColor's
        // own displayMe=true tail (`if (displayMe) FastLED.show(brightness());`),
        // the FIRST of red_heart/lightsaberBattle's two same-tick shows. See
        // OP_SHOWLIVE's enum comment, above, for the golden proof this needs
        // LIVE brightness, not OP_SHOWNOW's frozen boot scale.
        FastLED.show(brightness());
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
      case OP_SPARKLE: {
        uint8_t n = pgm_read_byte(pc++);
        vmSparkleDraw(n, vmColor(pgm_read_byte(pc++)));
        break;
      }
      // OP_SCALE_RAND/OP_MUL_RAND (Task 12): FadeOut's two per-LED random
      // passes. ONE random() call per LED, draw-then-write, index order
      // 0..NUM_LEDS-1 — matches native's `for (x=0; x<NUM_LEDS; x++)` body
      // exactly (see vmc_fadeout's comment for the golden-verified pass
      // sequence this feeds). Neither op advances g_vmPC on its own — the
      // caller's OP_SHOWR rep count is what makes a staging op like this one
      // re-run (with fresh random() draws) every tick of a multi-tick pass.
      case OP_SCALE_RAND: {
        uint8_t lo = pgm_read_byte(pc++);
        uint8_t hi = pgm_read_byte(pc++);
        for (int x = 0; x < NUM_LEDS; x++) leds[x].nscale8_video(random(lo, hi));
        break;
      }
      case OP_MUL_RAND: {
        uint8_t mx = pgm_read_byte(pc++);
        for (int x = 0; x < NUM_LEDS; x++) leds[x] *= random(0, mx);
        break;
      }
      // (OP_FILL_ROW/OP_FILL_COLR landed Task 8 above — vmc_leia/vmc_swscan/
      // vmc_knight exercise both. OP_HALFCOLR landed Task 9 — vmc_radar.
      // OP_PIX/OP_FRAME landed Task 10 above — vmc_rebel/vmc_pulse.
      // OP_SPARKLE landed Task 11 above — vmc_disco. OP_SCALE_RAND/
      // OP_MUL_RAND landed Task 12 above — vmc_fadeout.)
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

// ---------------------------------------------------------------------------
// Contract-layer shims (Task 14): ContractPSI.h's CE_SCAN / CE_SPARKLE cases
// used to call the native scanCol()/DiscoBall() directly; both natives are now
// deleted (no other caller — the mode 6/10/15 sweep group and modes 12/13 were
// already ported to bytecode in earlier tasks, per the comments above). These
// mirror the exact per-tick semantics of the ONE call shape the contract layer
// ever used — scanCol(d, 0, col, true) and DiscoBall(d, 0, 3, col, 0) — traced
// line-by-line against src/main.cpp before this was written (see
// task-14-report.md for the side-by-side). Not general re-implementations of
// either native (e.g. DiscoBall's loops/runtime params are hardcoded to the
// values the contract always passed, exactly like vmContractScan already
// hardcodes start_col=0/scanDirection=1 below), under the contract's own
// firstTime re-init protocol (runContractAnim() sets firstTime on effect
// switch, same as every native mode body's own lifecycle).
static void vmContractScan(unsigned long d, const CRGB& col) {
  if (firstTime) {
    ledPatternState = COLUMNS - 1;   // scanCol(...,start_col=0,scanDirection=1) entry state
    firstTime = false;
    patternRunning = true;
  }
  if (checkDelay()) {
    if (ledPatternState >= 0 && ledPatternState <= COLUMNS - 1) {
      // Native's per-case allOFF(true) in FULL — not just its clear+show.
      // allOFF's body (src/main.cpp) ends with `if ((runtime != 0) ||
      // (timingReceived)) globalTimerDonedoRestoreDefault();`, and scanCol
      // called it with runtime defaulted to 0, so the tail reduces to the
      // timingReceived branch below — inherited by scanCol on EVERY gated
      // tick even though scanCol's own body never names timingReceived.
      // (allOFF's firstTime block is dead here: the shim's own firstTime
      // block above has already cleared the flag, exactly as native
      // scanCol's had by the time its cases ran.)
      FastLED.clear();
      FastLED.show();                // native allOFF(true): clear + bare show
      if (timingReceived) globalTimerDonedoRestoreDefault();
      fill_column((uint8_t)ledPatternState, col, 0);
      vmShowDelay(d);
    }
    ledPatternState--;
    if (ledPatternState < 0) {
      ledPatternState = COLUMNS - 1;
      globalPatternLoops--;
    }
  }
  // native scanCol()'s OWN body has no runtime/loops epilogue (no
  // loopsDonedoRestoreDefault/globalTimerDonedoRestoreDefault call of its
  // own, and no reference to timingReceived) — globalPatternLoops decrements
  // for bookkeeping only. Its ONE timing conditional is the inherited
  // allOFF(true) tail mirrored above, inside the gated tick.
}

static void vmContractSparkle(unsigned long d, const CRGB& col) {
  if (firstTime) {
    firstTime = false;
    patternRunning = true;
    globalPatternLoops = 2;          // native loops==0 seed (loops*2 branch not taken)
    // native's firstTime block: `if ((runtime!=0)&&(!timingReceived)) set_global_timeout(runtime);`
    // is dead at this call shape (runtime==0 always); `if (timingReceived) set_global_timeout(...)`
    // is NOT dead — timingReceived is native-JawaLite global state (main.cpp doTcommand(), set by
    // a T-command with a nonzero timing value) that the contract layer never reads or clears, so a
    // stale native timing command left armed before the board entered contract mode is still live
    // here. CE_METER's still-native VUMeter(d,0,0) call two cases below carries the identical
    // pattern unmodified, and every ported mode's vmPlay() (above) mirrors it too — mirrored here
    // for the same reason: an unmirrored gap would be a new asymmetry, not a simplification.
    if (timingReceived) set_global_timeout(commandTiming);
    ledPatternState = 0;
    FastLED.clear();
    FastLED.show();                  // native's firstTime allOFF(true): clear + bare show
  }
  if (checkDelay()) {
    if (ledPatternState == 0) vmSparkleDraw(3, col);
    else FastLED.clear();            // native's ledPatternState==1: allOFF(false), clear only, no show
    ledPatternState ^= 1;
    globalPatternLoops--;
    vmShowDelay(d);
  }
  // native's tail runs UNCONDITIONALLY every call (not gated by checkDelay):
  //   if ((runtime==0) && (!timingReceived)) { if (loops) loopsDonedoRestoreDefault(); }
  //   else { globalTimerDonedoRestoreDefault(); }
  // runtime==0 and loops==0 always at this call shape, so the first branch is dead (loops=0 =>
  // no-op) and the whole thing collapses to: fire globalTimerDonedoRestoreDefault() iff
  // timingReceived is (still) true.
  if (timingReceived) globalTimerDonedoRestoreDefault();
}

#endif  // PSI_VM_TABLES_ONLY

#endif  // PSI_VM_H
