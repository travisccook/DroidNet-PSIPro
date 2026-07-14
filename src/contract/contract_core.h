// Part of the DroidNet Driveable-Animation Contract. Copyright (c) 2026 Travis Cook.
// Shared byte-identically across the DroidNet RSeries/PSI/Flthy forks.
// SPDX-License-Identifier: LGPL-2.1-only OR MIT
// src/contract/contract_core.h — pure, dependency-free contract parsing/logic.
// Host-compilable (no Arduino/FastLED/Reeltwo). The firmware layer includes this too.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// NOT named `RGB`. FastLED (which the PSI and the RSeries Logics both pull in ahead of
// this header) declares `RGB` as an ENUMERATOR of fl::EOrder — `enum EOrder { RGB=0012,
// GRB=0102, ... }`, hoisted into the global namespace by its compatibility shim. A class
// name is hidden by a variable/enumerator of the same name in the same scope (C++
// [basic.scope.hiding]), so `struct RGB {...}` would still COMPILE here and then every
// later use of the plain type name would fail with "'RGB' does not name a type" — which
// is exactly what the real avr-gcc/xtensa builds reported. The host mocks never modelled
// EOrder, so this was invisible to the mock type-check by construction. Keep the name
// qualified; do not "simplify" it back to RGB.
struct ContractRGB { uint8_t r, g, b; };

enum ContractVerb : uint8_t {
  CV_NONE = 0, CV_ANIMATE, CV_PULSE, CV_CLOCK, CV_BRIGHT, CV_LEVEL, CV_STOP, CV_MODE, CV_QUERY
};
enum ContractEffect : uint8_t {
  CE_NONE = 0, CE_OFF, CE_SOLID, CE_FLASH, CE_PULSE, CE_RAINBOW, CE_SCAN, CE_SPARKLE, CE_METER, CE_NATIVE,
  CE_COMET, CE_CHASE, CE_WIPE, CE_GRADIENT, CE_COLORCYCLE, CE_TWINKLE
};

struct ContractParams {
  bool hasEffect = false;  ContractEffect effect = CE_NONE;  int nativeCode = -1;
  bool hasColor = false;   ContractRGB color{0, 0, 0};
  bool hasSpeed = false;   uint8_t speed = 0;
  bool hasDur = false;     uint32_t durMs = 0;
  bool hasBright = false;  uint8_t bright = 0;
  bool hasBeatMod = false; uint8_t beatMod = 0;
  bool hasAt = false;      int32_t atBeat = -1;
  bool hasAm = false;      uint8_t accentMode = 0;
  // clock params (Plan 2): parsed here but unused in Phase-1
  bool hasBpm = false;     uint16_t bpm = 0;
  bool hasPh = false;      uint32_t phMs = 0;
  bool hasBpb = false;     uint8_t bpb = 4;
  bool hasBeat = false;    int32_t beatAnchor = 0;
  bool hasLevel = false;   uint8_t level = 0;   // verb L value
  char mode = 0;           // verb M: 's'(how) | 'i'(dle)
  // ===== v1.2: accent-effect overlay =====
  // The ONE overlay primitive, reachable from two triggers:
  //   * live  (Phase 1): verb P, which carries the overlay in i=/c=/d=;
  //   * score (Phase 2): verb A + at=, which carries it in ae=/ac=/ad= so the board
  //     can fire the SAME effect-swap accent autonomously, without the Pi.
  // Absent on the wire => hasAccentFx stays false => the entry behaves EXACTLY as v1.1.
  bool hasAccentFx = false;    ContractEffect accentFx = CE_NONE;   // ae=<effect name>
  bool hasAccentColor = false; ContractRGB accentColor{0, 0, 0};            // ac=<rrggbb>
  bool hasAccentDur = false;   uint16_t accentDurMs = 0;            // ad=<ms>, clamped <= 2550
};

struct ParsedContract {
  bool valid = false;
  char cls = 0;   // 'L' 'P' 'H' '*'
  char unit = 0;  // 'F' 'R' 'T' '*'
  ContractVerb verb = CV_NONE;
  ContractParams params;
};

// ===== PARSE IMPL (Tasks 2/3) =====
inline ContractVerb _verbFromChar(char v) {
  switch (v) {
    case 'A': return CV_ANIMATE; case 'P': return CV_PULSE; case 'C': return CV_CLOCK;
    case 'B': return CV_BRIGHT;  case 'L': return CV_LEVEL;  case 'X': return CV_STOP;
    case 'M': return CV_MODE;    case 'Q': return CV_QUERY;  default: return CV_NONE;
  }
}
inline int _hexNib(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
inline bool _parseHex6(const char* s, size_t len, ContractRGB& out) {
  if (len < 6) return false;
  int v[6];
  for (int i = 0; i < 6; i++) { v[i] = _hexNib(s[i]); if (v[i] < 0) return false; }
  out.r = (uint8_t)(v[0] * 16 + v[1]);
  out.g = (uint8_t)(v[2] * 16 + v[3]);
  out.b = (uint8_t)(v[4] * 16 + v[5]);
  return true;
}
inline ContractEffect _effectFromName(const char* s, size_t len, int& nativeCode) {
  nativeCode = -1;
  auto eq = [&](const char* k) { size_t kl = strlen(k); return kl == len && strncmp(s, k, kl) == 0; };
  if (eq("off")) return CE_OFF;         if (eq("solid")) return CE_SOLID;
  if (eq("flash")) return CE_FLASH;     if (eq("pulse")) return CE_PULSE;
  if (eq("rainbow")) return CE_RAINBOW; if (eq("scan")) return CE_SCAN;
  if (eq("sparkle")) return CE_SPARKLE; if (eq("meter")) return CE_METER;
  if (eq("comet")) return CE_COMET;       if (eq("chase")) return CE_CHASE;
  if (eq("wipe")) return CE_WIPE;         if (eq("gradient")) return CE_GRADIENT;
  if (eq("colorcycle")) return CE_COLORCYCLE; if (eq("twinkle")) return CE_TWINKLE;
  // native:<n>. The digits are parsed BOUNDED BY len, not by a NUL.
  // This used to be `nativeCode = atoi(s + 7)`, and atoi() stops at the first NON-DIGIT — not
  // at s+len. So a caller handing us a slice that ends in a digit (a view into a ring buffer,
  // say) would have atoi() read straight off the end of the buffer. AddressSanitizer proves it:
  // memcpy "native:123" into a 10-byte malloc with no NUL, call this, and you get a
  // heap-buffer-overflow READ. It is NOT reachable from any caller in this firmware today —
  // _parseParams always ends a value slice on ',' or '\0', both non-digits, inside the same
  // NUL-terminated line buffer — so this closes a latent trap for a future caller, on a parser
  // that eats bytes off a shared serial bus. Behaviour is unchanged for every reachable input.
  if (len > 7 && strncmp(s, "native:", 7) == 0) {
    size_t i = 7;
    int32_t sign = 1;
    if (i < len && (s[i] == '-' || s[i] == '+')) { sign = (s[i] == '-') ? -1 : 1; i++; }
    int32_t n = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') {
      // Saturate INSIDE int16, because nativeCode is a plain `int` and that is 16 BITS on the
      // ATmega. The exact last-safe-multiply test, not an approximate one: a loose guard here
      // still lets the final digit push the accumulator past 32767 (a `n < 3277` bound accepts
      // "32768" and lands on 32768, one over — which is precisely the overflow this exists to
      // stop, and which the host fuzzer's int16 invariant caught).
      int32_t d = s[i] - '0';
      if (n <= (32767 - d) / 10) n = n * 10 + d;
      i++;
    }
    nativeCode = (int)(sign * n);
    return CE_NATIVE;
  }
  return CE_NONE;
}
// v1.2: which effects are legal as a one-shot ACCENT overlay (ae=)?
// ONLY the stateless renders — a pure function of (elapsed-ms, colour), safe to swap in
// for ~180ms and swap back out.
//   * CE_SCAN / CE_SPARKLE / CE_METER are STATEFUL: they keep per-unit frame counters
//     (Flthy u.frame/u.frameMs, PSI firstTime + scanCol/DiscoBall/VUMeter internals) that
//     they SHARE with the base look. A swap-and-restore corrupts the base look's state
//     machine mid-song.
//   * CE_NATIVE hands the frame to a renderer we do not own (RSeries _select(), Flthy
//     LED_command[].LEDFunction), so the contract's per-frame render — and therefore the
//     overlay's EXPIRY CHECK — never runs. A native accent would latch the board into the
//     native look forever.
// A rejected ae= leaves accentFx == CE_NONE, i.e. NO accent. Fail-safe, never a latch.
inline bool accentEffectAllowed(ContractEffect e) {
  switch (e) {
    case CE_OFF: case CE_SOLID: case CE_FLASH: case CE_PULSE: case CE_RAINBOW:
    case CE_COMET: case CE_CHASE: case CE_WIPE: case CE_GRADIENT:
    case CE_COLORCYCLE: case CE_TWINKLE: return true;
    default: return false;   // CE_NONE, CE_SCAN, CE_SPARKLE, CE_METER, CE_NATIVE
  }
}
// ---- decimal parsers -------------------------------------------------------------
// These replace strtol()/strtoul(), which cost 548 B + 460 B = 1008 B of avr-libc on the
// ATmega32U4 — ~3.5% of the PSI's 28 KB flash — to parse the handful of integers the wire
// carries. atoi() is NOT replaced: on AVR it is 58 B of hand-written assembly that does
// not pull in strtol at all (verified with avr-nm/avr-objdump), so the uint8_t-clamped
// params (s/b/m/am/v/bpb/bpm) keep using it. Only the 32-bit fields moved here.
//
// _decDigits reads the grammar the wire can actually carry — optional leading whitespace,
// an optional '+'/'-', then ASCII decimal digits, stopping at the first non-digit (which
// _parseParams guarantees is ',' or '\0'). The whitespace skip is not decoration: the PSI's
// buildCommand() copies every byte up to '\r' verbatim, so a hand-typed "ad= 180" really can
// reach a value slice, and strtol() would have skipped that space. Same isspace() set atoi
// uses (0x09..0x0D, 0x20), so every numeric param on the line now behaves alike.
//
// WIDTH: fixed at uint32_t on ALL targets. `long` is 64-bit on the host but 32-bit on
// AVR/ESP32, so the old strtol/strtoul silently parsed to a different width under the host
// tests than on the boards. Pinning 32 bits means the 263 host checks now gate the exact
// values the ATmega32U4 computes.
//
// OVERFLOW: saturating, like strtol/strtoul (which return LONG_MAX/ULONG_MAX + ERANGE).
// The magnitude latches at 0xFFFFFFFF and stays there for any further digits, so a stray
// `ad=99999999999` can never wrap small and slip under the 2550 clamp below.
inline uint32_t _decDigits(const char*& s, bool& neg) {
  while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;   // isspace(), as strtol/atoi do
  neg = (*s == '-');
  if (neg || *s == '+') s++;
  uint32_t acc = 0;
  while (*s >= '0' && *s <= '9') {
    uint8_t d = (uint8_t)(*s++ - '0');
    // 4294967295 == 429496729*10 + 5, so this is the exact last-safe-multiply test.
    if (acc > 429496729UL || (acc == 429496729UL && d > 5)) acc = 0xFFFFFFFFUL;  // latch
    else acc = acc * 10UL + d;
  }
  return acc;
}
// d= / ph= / ad=: unsigned. Matches strtoul, including its wrap-the-sign behaviour
// (strtoul("-5") == ULONG_MAX-4) for every in-range input the wire can carry.
inline uint32_t _parseU32(const char* s) {
  bool neg; uint32_t m = _decDigits(s, neg);
  return neg ? (uint32_t)(0UL - m) : m;
}
// at= / beat=: signed int32 BEAT INDICES that must survive past 32767 — `int` is 16 BITS on
// the ATmega32U4 (PSI) and ATmega2560 (Flthy), so parsing these with atoi() would wrap
// `at=40000` NEGATIVE, and a negative atBeat sorts to the FRONT of the score and fires its
// section immediately, at show start. Saturates at INT32_MIN/INT32_MAX exactly as a 32-bit
// strtol does, so an out-of-range beat still sorts to the END of the score, never the front.
inline int32_t _parseI32(const char* s) {
  bool neg; uint32_t m = _decDigits(s, neg);
  // "magnitude does not fit in int32" is exactly "bit 31 set" (m >= 2^31), which on AVR is a
  // one-byte bit test instead of a four-byte compare against 2147483647/2147483648. Note the
  // negative ceiling is 2^31 EXACTLY == INT32_MIN, so -2147483648 lands on INT32_MIN either
  // way, whether by saturating or by parsing exactly. Both roads meet.
  if (m & 0x80000000UL) return neg ? (int32_t)(-2147483647L - 1L) : (int32_t)2147483647L;
  return neg ? -(int32_t)m : (int32_t)m;
}

// One key=value token. k/kl and v/vl are slices into the (null-terminated) source
// buffer; numeric parses (atoi/_parseU32/_parseI32) read v up to the next ',' or '\0'.
inline void _applyParam(ContractParams& pr, const char* k, size_t kl, const char* v, size_t vl) {
  auto key = [&](const char* s) { return kl == strlen(s) && strncmp(k, s, kl) == 0; };
  if (key("i")) {
    int nc; ContractEffect e = _effectFromName(v, vl, nc);
    if (e != CE_NONE) { pr.hasEffect = true; pr.effect = e; pr.nativeCode = nc; }
  } else if (key("c")) {
    ContractRGB rgb; if (_parseHex6(v, vl, rgb)) { pr.hasColor = true; pr.color = rgb; }
  } else if (key("s"))  { pr.hasSpeed = true;   pr.speed = (uint8_t)atoi(v); }
  else if (key("d"))    { pr.hasDur = true;     pr.durMs = _parseU32(v); }
  else if (key("b"))    { pr.hasBright = true;  pr.bright = (uint8_t)atoi(v); }
  else if (key("m"))    { pr.hasBeatMod = true; pr.beatMod = (uint8_t)atoi(v); }
  else if (key("at"))   { pr.hasAt = true;      pr.atBeat = _parseI32(v); }
  else if (key("am"))   { pr.hasAm = true;      pr.accentMode = (uint8_t)atoi(v); }
  else if (key("v"))    { pr.hasLevel = true;   pr.level = (uint8_t)atoi(v);
                          if (vl > 0 && (v[0] == 's' || v[0] == 'i')) pr.mode = v[0]; }
  else if (key("bpm"))  { pr.hasBpm = true;     pr.bpm = (uint16_t)atoi(v); }
  else if (key("ph"))   { pr.hasPh = true;      pr.phMs = _parseU32(v); }
  else if (key("bpb"))  { pr.hasBpb = true;     pr.bpb = (uint8_t)atoi(v); }
  else if (key("beat")) { pr.hasBeat = true;    pr.beatAnchor = _parseI32(v); }
  // ---- v1.2 accent overlay (ae/ac/ad). key() is an EXACT-LENGTH compare, so these three
  // cannot alias a/at/am/b/c/d/i/s/m/v/bpm/ph/bpb/beat/sw.
  else if (key("ae"))   { int nc; ContractEffect e = _effectFromName(v, vl, nc);
                          // native:<n> and the stateful effects are rejected here (see
                          // accentEffectAllowed) => hasAccentFx stays false => no accent.
                          if (e != CE_NONE && accentEffectAllowed(e)) { pr.hasAccentFx = true; pr.accentFx = e; }
                          (void)nc; }
  else if (key("ac"))   { ContractRGB rgb; if (_parseHex6(v, vl, rgb)) { pr.hasAccentColor = true; pr.accentColor = rgb; } }
  else if (key("ad"))   { // 32-bit, not atoi: `int` is 16-bit on AVR, so a stray ad=99999
                          // would wrap NEGATIVE before it could ever be clamped.
                          uint32_t d = _parseU32(v);
                          if (d > 2550UL) d = 2550UL;                 // 255 * 10ms — the score stores /10
                          pr.hasAccentDur = true; pr.accentDurMs = (uint16_t)d; }
  // "sw" (swing) and any unknown key: parsed-and-ignored (forward-compatible)
}
inline void _parseParams(const char* s, ContractParams& pr) {
  while (*s) {
    const char* kstart = s; while (*s && *s != '=' && *s != ',') s++;
    size_t kl = (size_t)(s - kstart);
    const char* vstart = s; size_t vl = 0;
    if (*s == '=') { s++; vstart = s; while (*s && *s != ',') s++; vl = (size_t)(s - vstart); }
    if (kl) _applyParam(pr, kstart, kl, vstart, vl);
    if (*s == ',') s++;
  }
}
// afterBang = the bytes AFTER the leading '!'. Returns true on a well-formed
// scope+verb. Unknown params are ignored (forward-compatible).
inline bool contractParse(const char* afterBang, ParsedContract& out) {
  out = ParsedContract{};
  if (!afterBang) return false;
  size_t n = strlen(afterBang);
  if (n < 3) return false;                         // need class + unit + verb
  char cls = afterBang[0], unit = afterBang[1];
  if (cls != 'L' && cls != 'P' && cls != 'H' && cls != '*') return false;
  if (unit != 'F' && unit != 'R' && unit != 'T' && unit != '*') return false;
  ContractVerb verb = _verbFromChar(afterBang[2]);
  if (verb == CV_NONE) return false;
  out.cls = cls; out.unit = unit; out.verb = verb;
  if (n > 3 && afterBang[3] == ':') _parseParams(afterBang + 4, out.params);
  out.valid = true;
  return true;
}
// ===== END PARSE IMPL =====

// ===== BEAT-CLOCK + SCORE (Plan 2, board-agnostic, host-tested) =====
// Seeded by verb C; drives a per-beat accent envelope and the Phase-2 section
// schedule. Pure math (float is fine at frame rate, even on AVR). All firmware
// forks share this exact logic so on-board playback is identical across boards.

struct BeatClock {
  bool     running = false;
  uint16_t bpm = 0;
  uint32_t anchorMs = 0;    // millis of the reference downbeat (= receiptMs + phMs)
  uint8_t  bpb = 4;
  int32_t  beatOffset = 0;  // absolute beat index at anchorMs (a downbeat -> multiple of bpb)
};

struct BeatPos {
  int32_t beatIndex = 0;    // absolute beat ordinal (may be < 0 before the anchor)
  float   phase = 0.0f;     // 0..1 within the current beat
  uint8_t barPos = 0;       // 0 == downbeat
};

// Seed/adjust the clock from a parsed C command received at nowMs.
inline void beatClockSeed(BeatClock& bc, const ContractParams& pr, uint32_t nowMs) {
  if (pr.hasBpm) bc.bpm = pr.bpm;
  if (pr.hasBpb) bc.bpb = pr.bpb ? pr.bpb : 4;
  bc.anchorMs = nowMs + (pr.hasPh ? pr.phMs : 0);
  if (pr.hasBeat) bc.beatOffset = pr.beatAnchor;      // re-anchor (drift correction)
  bc.running = (bc.bpm > 0);
}

// Beat position at time nowMs (drift-free between re-seeds; computed from millis).
inline BeatPos beatPosAt(const BeatClock& bc, uint32_t nowMs) {
  BeatPos out;
  if (!bc.running || bc.bpm == 0) return out;
  int32_t delta = (int32_t)(nowMs - bc.anchorMs);     // signed ms since the anchor downbeat
  float beats = (float)delta * (float)bc.bpm / 60000.0f;
  float fl = floorf(beats);
  out.beatIndex = bc.beatOffset + (int32_t)fl;
  out.phase = beats - fl;                             // 0..1
  int bpb = bc.bpb ? bc.bpb : 4;
  out.barPos = (uint8_t)(((out.beatIndex % bpb) + bpb) % bpb);
  return out;
}

// Sentinel for "this unit has not accented any beat yet". A show must never inherit a
// stale beat edge from the previous one, so every show boundary (stop / mode idle / mode
// show / clock re-seed / score load) resets the per-unit guard to this.
// Why not -1 or 0: beatPosAt() legitimately returns a NEGATIVE beatIndex before the anchor,
// and a re-anchored show restarts at beat 0 — either would swallow a real first edge. This
// sentinel is outside any reachable beat index, so the first beat of a new show always fires.
static const int32_t BEAT_NONE = (int32_t)0x80000000;

// Does accent mode `am` fire an accent on a beat at bar position `barPos`?
// am: 0 none, 1 downbeat, 2 every-beat, 3 build, 4 drop (-> downbeat).
// THE single definition of the fire predicate. beatAccentAmount() gates its brightness
// envelope on this, and the firmware's once-per-beat effect-swap overlay (v1.2) fires on
// this — so the brightness pump and the effect accent can never disagree about WHICH beats
// carry an accent. Extracted verbatim from beatAccentAmount(); it is the same expression.
inline bool beatAccentFires(uint8_t am, uint8_t barPos) {
  return (am == 2) || ((am == 1 || am == 4) && barPos == 0) || (am == 3);
}

// Consume a beat edge. Returns true EXACTLY ONCE per new beat index.
// Advances the guard UNCONDITIONALLY — a beat that does not fire must still consume its
// edge, or the very next tick (still inside the same beat) re-tests and re-fires every frame.
// So callers must call this FIRST and test the fire predicate afterwards.
inline bool beatEdge(int32_t& lastBeat, int32_t beatIndex) {
  if (beatIndex == lastBeat) return false;
  lastBeat = beatIndex;
  return true;
}

// Photosensitivity cool-down: may an accent START now, given that the last one started at
// lastFireMs? THE single definition of the gate; all three boards call this, so they cannot
// drift apart on a SAFETY limit.
//
// Note what is deliberately NOT here: a `lastFireMs != 0` "has it ever fired?" escape. All
// three boards used to carry one, and it was a hole straight through the cap. millis()
// genuinely does return 0 — once at boot, and again every ~49.7 days when it wraps — so an
// accent that fires on that exact tick stamps lastFireMs = 0, which the `!= 0` idiom then
// reads back as "never fired" and waves the NEXT accent through with no gap at all,
// back-to-back. Rare, but this is a photosensitivity limit: the failure mode is a seizure
// risk, not a cosmetic glitch, and it should not be reachable at all.
//
// Dropping the clause costs nothing, because unsigned arithmetic already does the right
// thing: (now - 0) == now, so before the board has ever fired, the gate simply opens once
// millis() >= minGapMs. The entire price is that an accent arriving in the first ~340 ms of
// power-on is coalesced away — and nothing has delivered a show by then. A suppressed accent
// is the fail-safe direction for a strobe cap; a free one is not.
inline bool strobeCoolDownExpired(uint32_t lastFireMs, uint32_t nowMs, uint32_t minGapMs) {
  return (uint32_t)(nowMs - lastFireMs) >= minGapMs;
}

// Accent envelope amount 0..255 for the current beat, per accent mode + depth.
// am: 0 none, 1 downbeat, 2 every-beat, 3 build (uses sectionProgress 0..1), 4 drop (->downbeat).
inline uint8_t beatAccentAmount(uint8_t am, const BeatPos& bp, uint8_t beatMod, float sectionProgress) {
  if (am == 0 || beatMod == 0) return 0;   // (the am==0 half is now redundant with
                                           // beatAccentFires(0,x)==false; the beatMod==0 half is NOT)
  if (!beatAccentFires(am, bp.barPos)) return 0;
  float env = (1.0f - bp.phase) * (1.0f - bp.phase);  // fast attack, slow decay
  if (bp.barPos == 0) { env *= 1.3f; if (env > 1.0f) env = 1.0f; }   // downbeat emphasis
  if (am == 3) {                                       // build: scale by section progress
    float s = sectionProgress < 0 ? 0.0f : (sectionProgress > 1 ? 1.0f : sectionProgress);
    env *= s;
  }
  float amt = env * (float)beatMod;
  if (amt < 0) amt = 0; if (amt > 255) amt = 255;
  return (uint8_t)amt;
}

// Beat-pumped brightness — THE shared envelope. All three forks (Logics/PSI/HPs)
// call this so an identical cue renders at an identical level on every board.
//
// Semantics (spec §3 "accent depth is scaled by the look's m"):
//   * b= (bright) is the CEILING. The render never exceeds it, and hits it exactly
//     at full accent (amt=255).
//   * m= (beatMod) is the DEPTH knob: between accents the level dips to the floor
//     bright*(255-m)/255, then the accent rides it back up toward bright.
//     m=0 => no pump at all (returns bright verbatim, for any amt).
//   * amt = this frame's accent envelope from beatAccentAmount() (already m-scaled,
//     so a shallow m both raises the floor and softens the ride — one knob, one feel).
// AVR-safe: no float, every 8x8 product is widened to uint16_t first (int is 16-bit
// on AVR), and fl <= bright always, so (bright - fl) can never go negative.
inline uint8_t envBright(uint8_t bright, uint8_t beatMod, uint8_t amt) {
  uint8_t fl = (uint8_t)(((uint16_t)bright * (uint16_t)(255u - (uint16_t)beatMod)) / 255u);
  return (uint8_t)((uint16_t)fl + ((uint16_t)amt * (uint16_t)(bright - fl)) / 255u);
}

// A per-unit section-schedule entry (Phase-2 score, built from verb A + at=).
struct ScoreEntry {
  int32_t        atBeat = 0;
  ContractEffect effect = CE_SOLID;
  int            nativeCode = -1;      // for effect==CE_NATIVE (an iconic native look)
  ContractRGB            color{0, 0, 0};
  uint8_t        speed = 128;
  uint8_t        beatMod = 0;
  uint8_t        accentMode = 0;
  // ---- v1.2 accent overlay. accentFx == CE_NONE => no overlay => EXACT v1.1 behaviour.
  // The firmware's beat-edge trigger tests `accentFx == CE_NONE` and bails, so an entry
  // built from a v1.1 line (no ae=) can never arm the overlay.
  ContractEffect accentFx = CE_NONE;   // 1B; never CE_NATIVE/CE_SCAN/... (rejected at parse)
  ContractRGB            accentColor{0, 0, 0}; // 3B; RESOLVED at insert: ac= if given, else the entry's color
  uint8_t        accentDur10 = 18;     // 1B; duration / 10ms (18 => 180ms). Keeps the entry small
                                       // on AVR: a 2550ms ceiling is far beyond any musical accent.
};

// Index of the last entry with atBeat <= beatIndex (-1 if none yet). Entries sorted asc.
inline int scoreActiveIndex(const ScoreEntry* entries, int n, int32_t beatIndex) {
  int found = -1;
  for (int i = 0; i < n; i++) {
    if (entries[i].atBeat <= beatIndex) found = i; else break;
  }
  return found;
}

// Forget every scheduled section and the active-section cursor. Firmware MUST call this at
// EVERY show boundary — verb X (stop), verb M:v=idle, AND verb M:v=show — so the next show
// starts from an empty table. v=show is the boundary that is easy to miss and the one the
// product actually hits: Studio's Deliver "Resend" pushes `!**M:v=show` and then the new
// score, with NO intervening stop, so a board that clears only on X/idle ACCUMULATES show B
// into show A. scoreInsert() drops silently at cap, so the merged table replays A's sections
// in between B's and, once it is full, discards B's sections outright.
// (activeIndex goes to -1, not 0: -1 is "no section yet", which is what scoreActiveIndex
// returns before the first entry and what the render loop compares against to detect a
// section change.)
inline void scoreClear(int& count, int& activeIndex) {
  count = 0;
  activeIndex = -1;
}

// Insert keeping the array sorted by atBeat (replace on exact match). Returns new count
// (drops silently if at cap). Firmware calls this from verb A when at= is present.
inline int scoreInsert(ScoreEntry* entries, int n, int cap, const ScoreEntry& e) {
  int i = 0;
  while (i < n && entries[i].atBeat < e.atBeat) i++;
  if (i < n && entries[i].atBeat == e.atBeat) { entries[i] = e; return n; }
  if (n >= cap) return n;
  for (int j = n; j > i; j--) entries[j] = entries[j - 1];
  entries[i] = e;
  return n + 1;
}
// ===== FX SCALAR HELPERS (Tasks 3-4 build effect renderers atop these) =====
// Pure integer math, board-agnostic. fxStepMs: per-frame cadence from a 0..255
// speed knob. fxHash16: cheap xorshift PRNG hash for sparkle/twinkle seeding.
// fxHsv2rgb: standard 6-sextant HSV->RGB (integer division, matches the JS parity port).
inline uint32_t fxStepMs(uint8_t speed) { return 30u + (uint32_t)(255 - speed) / 2u; }
// The `x +=` is load-bearing: a bare xorshift has 0 as a FIXED POINT (0^0 == 0 at every
// step), so fxHash16(0) returned 0. Every consumer seeds from the strand index, so index 0
// hashed to zero — in twinkle that gave pixel 0 the minimum period AND zero phase offset,
// i.e. a pixel visibly out of step with the rest of the strand, on every board, forever.
// Adding an odd constant (the golden-ratio word, the usual choice) before the mixing means
// no input maps to zero, and the avalanche behaviour is otherwise unchanged.
inline uint16_t fxHash16(uint32_t x) {
  x += 0x9E3779B9u;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5; return (uint16_t)(x & 0xFFFFu);
}
inline ContractRGB fxHsv2rgb(uint8_t h, uint8_t s, uint8_t v) {
  if (s == 0) return ContractRGB{ v, v, v };
  // The hue wheel must be CYCLIC: hue 255 has to sit exactly one step before hue 0, or the
  // colour jumps once per rotation. The old form (region = h/43, rem = (h - region*43) * 6)
  // was not: 6 sextants x 43 = 258 > 256, so the last sextant was truncated and hue 255
  // landed at rem 240 instead of ~255. At full saturation that put hue 255 at (255,0,15)
  // while hue 0 is (255,0,0) — a 15-unit snap where every other step moves 6. colorcycle
  // visibly hitched once per rotation.
  // Scaling by 6 into 16 bits and splitting on the byte boundary is exactly cyclic: 6 * 256
  // == 1536 == 6 whole sextants of 256, so every adjacent hue pair — including 255 -> 0 —
  // is the same 6 units apart.
  uint16_t hh = (uint16_t)h * 6u;              // 0..1530
  uint8_t region = (uint8_t)(hh >> 8);         // 0..5, never 6 (max 1530 >> 8 == 5)
  uint8_t rem = (uint8_t)(hh & 0xFFu);         // 0..255 within the sextant
  uint8_t p = (uint8_t)(((uint16_t)v * (255 - s)) / 255);
  uint8_t q = (uint8_t)(((uint16_t)v * (255 - (((uint16_t)s * rem) / 255))) / 255);
  uint8_t t = (uint8_t)(((uint16_t)v * (255 - (((uint16_t)s * (255 - rem)) / 255))) / 255);
  switch (region) {
    case 0:  return ContractRGB{ v, t, p };
    case 1:  return ContractRGB{ q, v, p };
    case 2:  return ContractRGB{ p, v, t };
    case 3:  return ContractRGB{ p, q, v };
    case 4:  return ContractRGB{ t, p, v };
    default: return ContractRGB{ v, p, q };
  }
}
// ===== END FX SCALAR HELPERS =====
// ===== FX SPATIAL HELPERS (Task 3; comet/chase/wipe renderers build atop these) =====
// Pure integer math over a strip of N positions (0..N-1). fxHead: which position
// leads at time `elapsed` for a given speed knob. fxCometBright: per-position
// brightness for a comet trailing behind the head (linear falloff, wraps around
// N). fxChaseLit: classic marquee-chase lit/unlit test. fxWipeLit: ping-pong
// fill wipe (fills 0..N-1 then drains back), lit test per position.
//
// AVR NOTE (widen-before-multiply): `int` is 16 BITS on the ATmega32U4/2560, so every
// product built from the strand length N or a position must be widened to uint32_t
// BEFORE the multiply, not after. `(uint32_t)(dist * 255)` evaluates dist*255 in a
// 16-bit int and casts the ALREADY-WRAPPED result. The host build (32-bit int) computes
// the right answer either way, so these are invisible to the test suite by construction —
// they can only be caught by inspection. All widenings below are value-preserving on the
// host, so the pinned vectors and the JS visualizer's parity mirror are unchanged.
inline int fxHead(uint32_t elapsed, uint8_t speed, int N) {
  if (N <= 0) return 0; uint32_t s = fxStepMs(speed); if (!s) s = 1;
  return (int)((elapsed / s) % (uint32_t)N);
}
inline uint8_t fxCometBright(int p, int head, int N) {
  if (N <= 0) return 0;
  int trail = (int)(((uint32_t)N * 2u) / 5u); if (trail < 2) trail = 2;   // 2*N wraps 16-bit int for N > 16383
  int dist = (head - p) % N; if (dist < 0) dist += N;
  if (dist >= trail) return 0;
  // dist*255 wraps a 16-bit int for dist > 128, i.e. any strand with N > ~322.
  return (uint8_t)(255u - ((uint32_t)dist * 255u) / (uint32_t)trail);
}
inline bool fxChaseLit(int p, uint32_t elapsed, uint8_t speed) {
  uint32_t s = fxStepMs(speed); if (!s) s = 1;
  return (((uint32_t)p + elapsed / s) % 3u) == 0u;
}
// A ping-pong wipe: fill 0 -> N-1, then DRAIN BACK from the far end. The drain half used to
// read `p > (ph - N)`, which empties from position 0 upward — i.e. the erase head travelled
// the SAME direction as the fill head, so the effect was really two forward sweeps and never
// ping-ponged at all. That contradicted the spec, this function's own comment, and the JS
// visualizer's preview. The mirrored form below empties from N-1 downward, which is what a
// ping-pong is, and keeps the identical cadence (one position per step, N steps per half).
inline bool fxWipeLit(int p, uint32_t elapsed, uint8_t speed, int N) {
  if (N <= 0) return false; uint32_t s = fxStepMs(speed); if (!s) s = 1;
  uint32_t ph = (elapsed / s) % ((uint32_t)N * 2u);   // 2*N wraps a 16-bit int for N > 16383
  if (ph < (uint32_t)N) return p <= (int)ph;          // fill: light 0..ph
  return p < (int)((uint32_t)N * 2u - 1u - ph);       // drain: dark from N-1 back down
}
// ===== END FX SPATIAL HELPERS =====
// ===== FX HUE/TWINKLE HELPERS (Task 4; gradient/colorcycle/twinkle renderers build atop these) =====
// Pure integer math. fxGradientHue: hue at position p across a strip's span
// (0..128 spread), drifting with elapsed. fxCycleHue: whole-strip hue rotation
// over time (half the drift rate of the gradient's per-position spread).
// fxTwinkleBright: per-LED (fxHash16-seeded) triangle-wave brightness so each
// index twinkles at its own hashed period/phase.
//
// The gradient's time drift and the cycle's rotation are deliberately coupled: fxCycleHue
// divides elapsed by fxStepMs(speed) * 2, EXACTLY twice fxGradientHue's divisor, so at the
// same speed knob a whole-strip colorcycle rotates at HALF the rate the gradient's own hue
// drifts. That 2u is the contract between the two effects (and with the JS visualizer's
// mirror of them) — it is pinned as a relationship, not just as vectors, in the host tests.
inline uint8_t fxGradientHue(int p, int N, uint8_t baseHue, uint32_t elapsed, uint8_t speed) {
  int span = (N > 1) ? (N - 1) : 1; uint32_t s = fxStepMs(speed); if (!s) s = 1;
  // AVR: p*128 wraps a 16-bit int for any strand position >= 256 — widen p BEFORE the
  // multiply. `(uint32_t)(p * 128)` would cast the already-wrapped 16-bit product.
  return (uint8_t)((uint32_t)baseHue + ((uint32_t)p * 128u) / (uint32_t)span + elapsed / s);
}
inline uint8_t fxCycleHue(uint8_t baseHue, uint32_t elapsed, uint8_t speed) {
  uint32_t s = fxStepMs(speed) * 2u; if (!s) s = 1;   // *2u: half the gradient's drift rate
  return (uint8_t)((uint32_t)baseHue + elapsed / s);
}
inline uint8_t fxTwinkleBright(int idx, uint32_t now, uint8_t speed) {
  uint16_t h = fxHash16((uint32_t)idx * 2654435761u);
  uint32_t period = 400u + (uint32_t)(255 - speed) * 6u + (uint32_t)(h & 0x1FFu);
  uint32_t phase = (now + ((uint32_t)h << 3)) % period;
  uint32_t half = period / 2u; if (!half) half = 1;
  uint32_t tri = (phase < half) ? (phase * 255u / half) : ((period - phase) * 255u / half);
  if (tri > 255u) tri = 255u;      // odd-period midpoint can exceed 255; clamp to the intended peak
  return (uint8_t)tri;
}
// ===== END FX HUE/TWINKLE HELPERS =====
// ===== END BEAT-CLOCK + SCORE =====
