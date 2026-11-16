// Part of the DroidNet Driveable-Animation Contract. Copyright (c) 2026 Travis Cook.
// Shared byte-identically across the DroidNet RSeries/PSI/Flthy forks.
// SPDX-License-Identifier: LGPL-2.1-only OR MIT
// src/contract/contract_core.h — pure, dependency-free contract parsing/logic.
// Host-compilable (no Arduino/FastLED/Reeltwo). The firmware layer includes this too.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

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

// ===== v1.3 constants. ONE definition for all three forks (spec S1, S3, S5, C1). =====
#define CONTRACT_VERSION "1.3"
#define CONTRACT_RELEASE 1                           // firmware release: 1 = R1 (bench), 2 = R2 (looks)
// PHOTOSENSITIVITY. >=170 ms per state => an accent or a pump swing may not RESTART more often
// than every 2 * 170 = 340 ms, i.e. <= 2.94 per second, inside the <= 3 flashes/sec guidance.
// A safety limit, not a tuning knob. It used to be defined once per fork.
static const uint32_t STROBE_MIN_STATE_MS = 170;
static const uint16_t CONTRACT_BPM_MIN = 30;        // S1: a C outside 30..300 is ignored whole...
static const uint16_t CONTRACT_BPM_MAX = 300;       //     ...except bpm=0, which holds the show (C6)
static const uint8_t  CONTRACT_BPB_MAX = 12;        // S1: a bpb outside 1..12 is 4
static const uint16_t ACCENT_EDGE_LATE_MS = 60;     // C1
static const uint16_t ACCENT_DUR_DEFAULT_MS = 180;  // ad= absent
static const uint16_t ACCENT_DUR_MAX_MS = 2550;     // S5: P d= and ad= ceiling (255 * 10 ms)
static const uint8_t  ACCENT_EVERY_MAX = 16;        // an= 1..16
static const uint8_t  CONTRACT_ROUTE_MAX = 99;      // Q:r= 0..99
// at= and beat= are clamped to +/- this at parse. Every sum and difference of beat indices the
// core and the forks compute then stays inside an int32, whatever a line carries (a clock running
// 24 days at 300 BPM adds under 11 million beats to its anchor).
static const int32_t  CONTRACT_BEAT_MAX = 1000000000L;
static const uint8_t  CONTRACT_QUERY_MAX = 48;      // section 10: "!<cls><unit>q:...<CR>" at most
static const uint8_t  CONTRACT_REPLY_MAX = 57;      // ...and with its route prefix (";W99,;S0")

enum ContractVerb : uint8_t {
  CV_NONE = 0, CV_ANIMATE, CV_PULSE, CV_CLOCK, CV_BRIGHT, CV_LEVEL, CV_STOP, CV_MODE, CV_QUERY, CV_VERIFY
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
  bool bpmBad = false;     // bpm= was there but empty, signed or not all digits: the C is ignored whole (S1)
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
  // ===== v1.3 =====
  bool hasWd = false;    uint8_t wd = 0;       // M:v=show,wd=<seconds>   host-silence watchdog (L2)
  bool hasN = false;     uint8_t n = 0;        // V:n=<entries held>
  bool hasK = false;     uint16_t k = 0;       // V:k=<hhhh>              XOR of the held entries' CRCs
  bool hasAn = false;    uint8_t an = 1;       // an=<n> 1..16            every-N accents (with am=2)
  bool hasAp = false;    uint8_t ap = 0;       // ap=<phase> 0..n-1
  bool hasRoute = false; uint8_t route = 0;    // Q:r=<n> 0..99           where the reply is routed
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
    case 'M': return CV_MODE;    case 'Q': return CV_QUERY;  case 'V': return CV_VERIFY;
    default: return CV_NONE;
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
// ---- name tables, and where they live -------------------------------------------------
// The parser's vocabulary (effect names, parameter keys, the mode words) and the query reply's
// fixed text are each ONE comma-separated string, matched by _nameIndex(). That replaced an
// if-chain of strlen()/strncmp() calls, one per name, which was the largest single piece of this
// file on an ATmega.
//
// On a Harvard MCU (AVR) a string literal lives in RAM unless it is placed in flash and read back
// with a special instruction. This file stays pure C++, so it does that through two macros a fork
// MAY define before including it (both, or neither):
//     #define CONTRACT_PSTR(s)       PSTR(s)             // put this literal in flash
//     #define CONTRACT_READ_BYTE(p)  pgm_read_byte(p)    // read one byte of it
// Undefined, they are the plain-C identity, which is right for the host and the ESP32. Every read
// of a table goes through CONTRACT_READ_BYTE: the host suite builds the tests a second time with
// stand-ins that make a direct dereference of a "flash" pointer fault (test_contract_v13.cpp).
#ifndef CONTRACT_PSTR
#define CONTRACT_PSTR(s) (s)
#define CONTRACT_READ_BYTE(p) (*(const char*)(p))
#endif
// Which entry of the comma-separated `list` is exactly the slice s[0..len)? -1: none.
// Never reads s past len. The list must not be empty and must not end in a comma.
inline int8_t _nameIndex(const char* list, const char* s, size_t len) {
  int8_t idx = 0;
  size_t i = 0;
  bool same = true;
  for (;;) {
    const char c = (char)CONTRACT_READ_BYTE(list);
    list++;
    if (c == ',' || c == '\0') {
      if (same && i == len) return idx;
      if (c == '\0') return -1;
      idx++; i = 0; same = true;
    } else {
      if (i >= len || s[i] != c) same = false;
      i++;
    }
  }
}
// The effect table is in ContractEffect order, CE_NATIVE (which takes a number) left out.
static_assert(CE_OFF == 1 && CE_METER == 8 && CE_NATIVE == 9 && CE_COMET == 10 && CE_TWINKLE == 15,
              "ContractEffect changed: update the name table in _effectFromName()");
inline ContractEffect _effectFromName(const char* s, size_t len, int& nativeCode) {
  nativeCode = -1;
  const int8_t idx = _nameIndex(CONTRACT_PSTR("off,solid,flash,pulse,rainbow,scan,sparkle,meter,"
                                              "comet,chase,wipe,gradient,colorcycle,twinkle"), s, len);
  if (idx >= 0) return (ContractEffect)(idx + (idx >= 8 ? 2 : 1));     // skip CE_NATIVE's slot
  // native:<n>. The digits are parsed BOUNDED BY len, not by a NUL.
  // This used to be `nativeCode = atoi(s + 7)`, and atoi() stops at the first NON-DIGIT — not
  // at s+len. So a caller handing us a slice that ends in a digit (a view into a ring buffer,
  // say) would have atoi() read straight off the end of the buffer. AddressSanitizer proves it:
  // memcpy "native:123" into a 10-byte malloc with no NUL, call this, and you get a
  // heap-buffer-overflow READ. It is NOT reachable from any caller in this firmware today —
  // _parseParams always ends a value slice on ',' or '\0', both non-digits, inside the same
  // NUL-terminated line buffer — so this closes a latent trap for a future caller, on a parser
  // that eats bytes off a shared serial bus. Behaviour is unchanged for every reachable input.
  if (len > 7 && _nameIndex(CONTRACT_PSTR("native:"), s, 7) == 0) {
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
// carries. Since v1.3 atoi() is not used either: the small parameters (s/b/m/am/v/bpb/bpm)
// go through _parseSatU8/_parseSatU16 below, which sit on this same digit reader and
// SATURATE where atoi() wrapped.
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
// tests than on the boards. Pinning 32 bits means the host checks gate the exact values the
// ATmega32U4 computes.
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

// S5: saturating small integers. A value past the range is the range's end, never a wrap:
// b=300 is 255 and b=-1 is 0, on the host and on a 16-bit-int AVR alike. These replace atoi()
// for every numeric parameter, so no parameter can wrap into range any more (bpm=65656 used to
// be 120 on every board).
inline uint16_t _parseSatU16(const char* s, uint16_t maxV) {
  bool neg; uint32_t m = _decDigits(s, neg);
  if (neg) return 0;
  return m > (uint32_t)maxV ? maxV : (uint16_t)m;
}
inline uint8_t _parseSatU8(const char* s, uint8_t maxV) { return (uint8_t)_parseSatU16(s, maxV); }
// A value that is one or more decimal digits and nothing else. bpm= and r= are taken ONLY in this
// form: for them a damaged value must not turn into a meaning (bpm= with no number used to parse
// as 0, which is Hold; r=zz as route 0, which is "reply to me").
inline bool _allDigits(const char* s, size_t len) {
  if (len == 0) return false;
  for (size_t i = 0; i < len; i++) if (s[i] < '0' || s[i] > '9') return false;
  return true;
}
// at= / beat=: inside +/- CONTRACT_BEAT_MAX.
inline int32_t _parseBeat(const char* s) {
  const int32_t b = _parseI32(s);
  return b > CONTRACT_BEAT_MAX ? CONTRACT_BEAT_MAX : (b < -CONTRACT_BEAT_MAX ? -CONTRACT_BEAT_MAX : b);
}
// k=<hhhh>: exactly four hex digits.
inline bool _parseHex4(const char* s, size_t len, uint16_t& out) {
  if (len != 4) return false;
  uint16_t v = 0;
  for (uint8_t i = 0; i < 4; i++) {
    int nb = _hexNib(s[i]);
    if (nb < 0) return false;
    v = (uint16_t)((uint16_t)(v << 4) | (uint16_t)nb);
  }
  out = v;
  return true;
}
// S5: an accent duration in range (verb P's d= and the scored ad=).
inline uint16_t accentDurClamp(uint32_t d) {
  if (d < STROBE_MIN_STATE_MS) d = STROBE_MIN_STATE_MS;
  if (d > ACCENT_DUR_MAX_MS) d = ACCENT_DUR_MAX_MS;
  return (uint16_t)d;
}

// One key=value token. k/kl and v/vl are slices into the (null-terminated) source
// buffer; numeric parses (_parseSatU8/_parseU32/_parseI32) read v up to the next ',' or '\0'.
inline void _applyParam(ContractParams& pr, const char* k, size_t kl, const char* v, size_t vl) {
  // The key table. _nameIndex() is an EXACT-LENGTH match, so no key can alias another
  // (a/at/am/an/ap, b/bpm/bpb/beat, ...). The enum is in the table's order.
  enum { K_I, K_C, K_S, K_D, K_B, K_M, K_AT, K_AM, K_AN, K_AP, K_V, K_BPM, K_PH, K_BPB, K_BEAT,
         K_AE, K_AC, K_AD, K_WD, K_N, K_K, K_R };
  switch (_nameIndex(CONTRACT_PSTR("i,c,s,d,b,m,at,am,an,ap,v,bpm,ph,bpb,beat,ae,ac,ad,wd,n,k,r"), k, kl)) {
    case K_I: {
      int nc; ContractEffect e = _effectFromName(v, vl, nc);
      if (e != CE_NONE) { pr.hasEffect = true; pr.effect = e; pr.nativeCode = nc; }
      break;
    }
    case K_C: { ContractRGB rgb; if (_parseHex6(v, vl, rgb)) { pr.hasColor = true; pr.color = rgb; } break; }
    case K_S:    pr.hasSpeed = true;   pr.speed = _parseSatU8(v, 255); break;
    case K_D:    pr.hasDur = true;     pr.durMs = _parseU32(v); break;
    case K_B:    pr.hasBright = true;  pr.bright = _parseSatU8(v, 255); break;
    case K_M:    pr.hasBeatMod = true; pr.beatMod = _parseSatU8(v, 255); break;
    case K_AT:   pr.hasAt = true;      pr.atBeat = _parseBeat(v); break;
    case K_AM:   pr.hasAm = true;      pr.accentMode = _parseSatU8(v, 255);
                 if (pr.accentMode > 4) pr.accentMode = 0;              // an unknown mode is no accent
                 break;
    case K_AN:   pr.hasAn = true;      pr.an = _parseSatU8(v, 255); break;
    case K_AP:   pr.hasAp = true;      pr.ap = _parseSatU8(v, 255); break;
    case K_V: {
      pr.hasLevel = true; pr.level = _parseSatU8(v, 255);
      const int8_t word = _nameIndex(CONTRACT_PSTR("show,idle"), v, vl);   // S5: exactly these two
      if (word == 0) pr.mode = 's'; else if (word == 1) pr.mode = 'i';
      break;
    }
    case K_BPM:  // S1: digits only. Anything else marks the line; beatClockSeed() then ignores it whole.
                 if (_allDigits(v, vl)) { pr.hasBpm = true; pr.bpm = _parseSatU16(v, 65535u); }
                 else pr.bpmBad = true;
                 break;
    case K_PH:   pr.hasPh = true;      pr.phMs = _parseU32(v); break;
    case K_BPB:  pr.hasBpb = true;     pr.bpb = _parseSatU8(v, 255); break;
    case K_BEAT: pr.hasBeat = true;    pr.beatAnchor = _parseBeat(v); break;
    // ---- v1.2 accent overlay (ae/ac/ad)
    case K_AE: {
      // native:<n> and the stateful effects are rejected here (see accentEffectAllowed)
      // => hasAccentFx stays false => no accent.
      int nc; ContractEffect e = _effectFromName(v, vl, nc);
      if (e != CE_NONE && accentEffectAllowed(e)) { pr.hasAccentFx = true; pr.accentFx = e; }
      (void)nc;
      break;
    }
    case K_AC: { ContractRGB rgb; if (_parseHex6(v, vl, rgb)) { pr.hasAccentColor = true; pr.accentColor = rgb; } break; }
    case K_AD: {
      // 32-bit: `int` is 16-bit on AVR, so a stray ad=99999 would wrap NEGATIVE before it could
      // ever be clamped.
      uint32_t d = _parseU32(v);
      if (d > 2550UL) d = 2550UL;                 // 255 * 10ms — the score stores /10
      pr.hasAccentDur = true; pr.accentDurMs = (uint16_t)d;
      break;
    }
    // ---- v1.3: watchdog, load verification, query route
    case K_WD:   pr.hasWd = true;      pr.wd = _parseSatU8(v, 255); break;
    case K_N:    pr.hasN = true;       pr.n = _parseSatU8(v, 255); break;
    case K_K: { uint16_t k16; if (_parseHex4(v, vl, k16)) { pr.hasK = true; pr.k = k16; } break; }
    case K_R: {  // section 10: a number 0..99 and nothing else. Any other r= is no route: the Q gets no reply.
      const uint16_t route = _allDigits(v, vl) ? _parseSatU16(v, 65535u) : 65535u;
      pr.hasRoute = route <= CONTRACT_ROUTE_MAX;
      pr.route = pr.hasRoute ? (uint8_t)route : 0;
      break;
    }
    default: break;   // "sw" (swing) and any unknown key: parsed-and-ignored (forward-compatible)
  }
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
// Seeded by verb C; drives a per-beat accent envelope and the Phase-2 section schedule. All
// firmware forks share this exact logic so on-board playback is identical across boards.
//
// INTEGERS ONLY (core revision CA1.3). There is no float in this file. Until CA1.2 the beat
// position and the accent envelope were float; on the 8-bit forks that linked the compiler's
// soft-float library (about 1.3 KB) for a handful of lines. Everything is now 32-bit integer
// arithmetic with stated rounding, so the three boards agree to the bit, a mirror of it can be
// tested with no tolerance, and a beat falls on the exact millisecond for as long as the clock runs.
//
// THE UNIT. A beat at b BPM lasts 60000 / b ms, which is not a whole number. But ms * bpm is:
// one beat is exactly 60000 of those "units" at every tempo. All beat arithmetic is done in units.
static const uint16_t BEAT_UNITS = 60000;

struct BeatClock {
  bool     running = false;
  uint16_t bpm = 0;
  uint32_t anchorMs = 0;    // millis of the reference downbeat (= receiptMs + phMs)
  uint8_t  bpb = 4;
  int32_t  beatOffset = 0;  // absolute beat index at anchorMs (a downbeat -> multiple of bpb)
};

struct BeatPos {
  int32_t  beatIndex = 0;   // absolute beat ordinal (may be < 0 before the anchor)
  uint16_t phase16 = 0;     // how far into the current beat, Q16: 0 at the beat, 65535 just before the next
  uint8_t  barPos = 0;      // 0 == downbeat
};

// Floor division and modulus for a positive divisor (beat indices go negative before the anchor).
inline int32_t _floorDiv(int32_t a, int32_t b) { const int32_t q = a / b; return ((a % b) < 0) ? q - 1 : q; }
inline int32_t _floorMod(int32_t a, int32_t b) { const int32_t r = a % b; return r < 0 ? r + b : r; }

// Whole beats since the anchor (negative before it), and in `unitsInto` the units into the
// current beat, 0..59999. EXACT: no rounding anywhere, for any nowMs within 2^31 ms (24.8 days)
// of the anchor either way, across the millis() wrap. In integers:
//     delta   = (int32)(nowMs - anchorMs)                 the uint32 difference, read as signed
//     minutes = floor(delta / 60000)
//     ms      = delta - minutes * 60000                   0..59999
//     t       = ms * bpm                                  at most 17,999,700 at 300 BPM
//     beats   = minutes * bpm + floor(t / 60000)
//     units   = t mod 60000
// (delta * bpm would be the direct form; it leaves 32 bits after two hours at 300 BPM. Splitting
// off whole minutes first keeps every product small, and a whole minute is exactly bpm beats.)
inline int32_t _beatSplit(const BeatClock& bc, uint32_t nowMs, uint16_t& unitsInto) {
  const int32_t delta = (int32_t)(nowMs - bc.anchorMs);
  int32_t minutes = delta / 60000L;
  int32_t ms = delta % 60000L;                            // (C++ rounds towards zero: put the remainder back in 0..59999)
  if (ms < 0) { ms += 60000L; minutes--; }
  const uint32_t t = (uint32_t)ms * bc.bpm;
  unitsInto = (uint16_t)(t % BEAT_UNITS);
  return (int32_t)((uint32_t)minutes * bc.bpm + t / BEAT_UNITS);      // (unsigned: no overflow to be undefined about)
}

// Beat position at time nowMs. Drift-free: computed from millis and the anchor every time.
//     beatIndex = beatOffset + beats
//     phase16   = floor(units * 65536 / 60000)            0..65535
//     barPos    = beatIndex mod bpb, floored              0..bpb-1
inline BeatPos beatPosAt(const BeatClock& bc, uint32_t nowMs) {
  BeatPos out;
  if (!bc.running || bc.bpm == 0) return out;
  uint16_t units;
  out.beatIndex = (int32_t)((uint32_t)bc.beatOffset + (uint32_t)_beatSplit(bc, nowMs, units));
  out.phase16 = (uint16_t)(((uint32_t)units << 16) / BEAT_UNITS);
  out.barPos = (uint8_t)_floorMod(out.beatIndex, bc.bpb ? bc.bpb : 4);
  return out;
}

// What a C line did.
//   CS_IGNORED  nothing changed (S1: a bpm outside 30..300 or not a number, or a bare re-anchor with
//               no clock running).
//   CS_SEEK     a new timeline: the line carried bpm=, or it moved the position by a beat or more.
//   CS_NUDGE    a bare re-anchor (no bpm=) that moved the position by less than one beat: the
//               host's periodic drift correction. C1 and C2 treat it differently from a seek.
//   CS_HOLD     C6: bpm=0. The clock is stopped; the score and the look stay. The fork freezes
//               its section cursors simply by not ticking a stopped clock. Only a C with a tempo
//               resumes it.
enum ContractSeed : uint8_t { CS_IGNORED = 0, CS_SEEK = 1, CS_NUDGE = 2, CS_HOLD = 3 };

// Seed/adjust the clock from a parsed C command received at nowMs.
inline ContractSeed beatClockSeed(BeatClock& bc, const ContractParams& pr, uint32_t nowMs) {
  if (pr.bpmBad) return CS_IGNORED;                       // S1: a bpm= that is not a number: the WHOLE line, its ph=/beat= too
  if (pr.hasBpm && pr.bpm == 0) { bc.running = false; return CS_HOLD; }                // C6
  if (pr.hasBpm && (pr.bpm < CONTRACT_BPM_MIN || pr.bpm > CONTRACT_BPM_MAX)) return CS_IGNORED;   // S1: the WHOLE line
  if (!pr.hasBpm && !bc.running) return CS_IGNORED;      // a bare re-anchor neither starts nor resumes a clock
  const BeatClock old = bc;
  if (pr.hasBpm) bc.bpm = pr.bpm;
  if (pr.hasBpb) bc.bpb = (pr.bpb >= 1 && pr.bpb <= CONTRACT_BPB_MAX) ? pr.bpb : 4;   // S1
  bc.anchorMs = nowMs + (pr.hasPh ? pr.phMs : 0);
  if (pr.hasBeat) bc.beatOffset = pr.beatAnchor;         // re-anchor (drift correction)
  bc.running = true;
  if (pr.hasBpm || !old.running) return CS_SEEK;
  // How far did the position move at this instant? By less than one beat either way: a nudge.
  // Exactly: the same beat; or the next one but not as far into it; or the one before but further in.
  uint16_t ua, ub;
  const uint32_t ia = (uint32_t)old.beatOffset + (uint32_t)_beatSplit(old, nowMs, ua);
  const uint32_t ib = (uint32_t)bc.beatOffset + (uint32_t)_beatSplit(bc, nowMs, ub);
  const int32_t d = (int32_t)(ib - ia);
  return (d == 0 || (d == 1 && ub < ua) || (d == -1 && ub > ua)) ? CS_NUDGE : CS_SEEK;
}

// Sentinel for "this unit has not accented any beat yet". A show must never inherit a
// stale beat edge from the previous one, so every show boundary (stop / mode idle / mode
// show / clock re-seed / score load) resets the per-unit guard to this.
// Why not -1 or 0: beatPosAt() legitimately returns a NEGATIVE beatIndex before the anchor,
// and a re-anchored show restarts at beat 0 — either would swallow a real first edge. This
// sentinel is outside any reachable beat index, so the first beat of a new show always fires.
static const int32_t BEAT_NONE = (int32_t)0x80000000;

// C1: the accent-edge guard a unit takes after ANY C, and after a scored A that arrives while the
// clock runs. More than ACCENT_EDGE_LATE_MS into the current beat: that beat counts as already
// accented, so a drift correction mid-beat cannot re-fire it. Otherwise the guard is cleared, so
// a seek that lands on a beat still accents it. (The strobe cool-down still applies on top.)
inline int32_t accentEdgeAfterSeed(const BeatClock& bc, uint32_t nowMs) {
  if (!bc.running || bc.bpm == 0) return BEAT_NONE;
  uint16_t units;
  const int32_t beat = (int32_t)((uint32_t)bc.beatOffset + (uint32_t)_beatSplit(bc, nowMs, units));
  // units / bpm is the time into the beat in ms, so "more than 60 whole ms into it" is
  // units >= 61 * bpm. No division, and no rounding to argue about.
  return units >= (uint32_t)(ACCENT_EDGE_LATE_MS + 1u) * bc.bpm ? beat : BEAT_NONE;
}

// C2: the section cursor after a tick. After a nudge (pinForward) it never moves backwards;
// after a seek it follows the beat wherever it went. idx is scoreActiveIndex()'s answer.
inline int8_t scoreCursor(int8_t cur, int idx, bool pinForward) {
  return (pinForward && idx < cur) ? cur : (int8_t)idx;
}

// S2: THE accent and pump rate floor. No beat-driven change, pump or overlay, may recur faster
// than every 2 * STROBE_MIN_STATE_MS (340 ms). beatRateDivisor() is the smallest k with
// k * beatPeriod >= 340 ms: 1 up to 176 BPM, 2 up to 300. barRateDivisor() is the same for a
// group of `bpb` beats (a bar, or the n of an every-N accent).
inline uint8_t barRateDivisor(uint16_t bpm, uint8_t bpb) {
  const uint32_t group = 60000UL * (bpb ? bpb : 4);                    // ms * bpm in one group
  const uint32_t k = ((uint32_t)(2u * STROBE_MIN_STATE_MS) * bpm + group - 1UL) / group;
  return k < 1 ? 1 : (uint8_t)k;
}
inline uint8_t beatRateDivisor(uint16_t bpm) { return barRateDivisor(bpm, 1); }

// Every-N accents (am=2 with an=<n>, ap=<phase>): accent the beats where beatIndex % n == ap.
// Packed in ONE byte: high nibble n-1 (n 1..16), low nibble the phase. 0 == every beat, which is
// what a line with no an=/ap= means. An n outside 1..16 is clamped; a phase >= n is 0.
inline uint8_t accentEveryPack(uint8_t n, uint8_t ap) {
  if (n < 1) n = 1;
  if (n > ACCENT_EVERY_MAX) n = ACCENT_EVERY_MAX;
  if (ap >= n) ap = 0;
  return (uint8_t)((uint8_t)((n - 1u) << 4) | ap);
}
inline uint8_t accentEveryN(uint8_t every) { return (uint8_t)((every >> 4) + 1u); }
inline uint8_t accentEveryPhase(uint8_t every) {
  const uint8_t ap = (uint8_t)(every & 15u);
  return ap < accentEveryN(every) ? ap : 0;
}
inline uint8_t accentEveryFrom(const ContractParams& pr) {
  return accentEveryPack(pr.hasAn ? pr.an : 1, pr.hasAp ? pr.ap : 0);
}

// Does accent mode `am` fire an accent on the beat at `bp`, with the clock `bc`?
// am: 0 none, 1 downbeat, 2 every beat (or every n-th, see `every`), 3 build, 4 drop (-> downbeat).
// THE single definition of the fire predicate. beatAccentAmount() gates its brightness
// envelope on this, and the firmware's once-per-beat effect-swap overlay fires on this —
// so the brightness pump and the effect accent can never disagree about WHICH beats carry
// an accent, and BOTH are under the S2 rate floor: at 240 BPM am=2 fires on every second beat.
// Up to 176 BPM (and for any downbeat mode in 2 beats a bar or more) it is the v1.2 predicate.
inline bool beatAccentFires(uint8_t am, const BeatPos& bp, const BeatClock& bc, uint8_t every = 0) {
  // Each mode picks a GROUP of n beats and asks which group this beat begins; the rate floor then
  // keeps every k-th group, k = barRateDivisor(bpm, n). (One tail for the three modes: on an 8-bit
  // MCU three copies of the divisor arithmetic were the largest function in this file.)
  int32_t group;                                          // which group of n beats this one begins
  uint8_t n;
  if (am == 2) {                                          // every beat, or every n-th from a phase
    n = accentEveryN(every);
    const int32_t rel = bp.beatIndex - (int32_t)accentEveryPhase(every);
    if (_floorMod(rel, n) != 0) return false;
    group = _floorDiv(rel, n);
  } else if (am == 3) {                                   // build: every beat
    n = 1;
    group = bp.beatIndex;
  } else if (am == 1 || am == 4) {                        // downbeat (and drop): every bar
    if (bp.barPos != 0) return false;
    n = bc.bpb ? bc.bpb : 4;
    group = _floorDiv(bp.beatIndex, n);
  } else {
    return false;
  }
  return (group % (int32_t)barRateDivisor(bc.bpm, n)) == 0;
}

// How far through its section a beat is (drives am=3, build), Q16 and already clamped: 0 at the
// section's first beat or before it, 65536 at its end or past it, 0 when the section has no length.
//     n = beatIndex - sectionStart,  d = sectionEnd - sectionStart        (0 < n < d here)
//     while d > 65535:  n = floor(n / 2), d = floor(d / 2)                (only a section over 65535 beats long)
//     progress16 = floor(n * 65536 / d)
// (Named ...16, and beatAccentAmount() takes it as an integer: a fork that still holds the old
// float in a variable does not compile, instead of quietly keeping soft-float linked.)
inline uint32_t beatSectionProgress16(int32_t beatIndex, int32_t sectionStart, int32_t sectionEnd) {
  if (sectionEnd <= sectionStart || beatIndex <= sectionStart) return 0;
  if (beatIndex >= sectionEnd) return 65536UL;
  uint32_t n = (uint32_t)beatIndex - (uint32_t)sectionStart;
  uint32_t d = (uint32_t)sectionEnd - (uint32_t)sectionStart;
  while (d > 65535UL) { n >>= 1; d >>= 1; }
  return (n << 16) / d;
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
// am: 0 none, 1 downbeat, 2 every-beat, 3 build (uses sectionProgress16), 4 drop (->downbeat).
// The envelope is (1 - phase)^2: full at the beat, a fast fall, a slow tail. In Q16, with p the
// beat's phase16 (every product fits 32 bits; ">> 16" is floor(x / 65536)):
//     env = 65536 + ((p * p) >> 16) - 2 * p               exactly floor((65536 - p)^2 / 65536): 65536 .. 0
//     on a downbeat:  env = min(65536, env + ((env * 19661) >> 16))        x 1.3 (19661 / 65536 = 0.30000)
//     am == 3 and sectionProgress16 < 65536:  env = (env * sectionProgress16) >> 16
//     amount = (env * beatMod) >> 16
// Q16 for the phase, not Q8: the amount has 8 bits, and a squared 8-bit phase would lose the
// bottom two of them at the top of the envelope, where the fall is steepest (a step of 2 or 3 in
// the amount between neighbouring phases). With 16 bits the amount is the float version's to
// within 1, everywhere.
inline uint8_t beatAccentAmount(uint8_t am, const BeatPos& bp, uint8_t beatMod, uint32_t sectionProgress16,
                                const BeatClock& bc, uint8_t every = 0) {
  if (am == 0 || beatMod == 0) return 0;   // (the am==0 half is redundant with the predicate;
                                           // the beatMod==0 half is NOT)
  if (!beatAccentFires(am, bp, bc, every)) return 0;
  const uint32_t p = bp.phase16;
  uint32_t env = 65536UL + ((p * p) >> 16) - 2UL * p;  // fast attack, slow decay
  if (bp.barPos == 0) { env += (env * 19661UL) >> 16; if (env > 65536UL) env = 65536UL; }   // downbeat emphasis
  if (am == 3 && sectionProgress16 < 65536UL) env = (env * sectionProgress16) >> 16;        // build: scale by section progress
  return (uint8_t)((env * beatMod) >> 16);
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
  int32_t        atBeat;
  ContractEffect effect;
  int            nativeCode;           // for effect==CE_NATIVE (an iconic native look)
  ContractRGB    color;
  uint8_t        speed;
  uint8_t        beatMod;
  uint8_t        accentMode;
  // ---- v1.2 accent overlay. accentFx == CE_NONE => no overlay => EXACT v1.1 behaviour.
  // The firmware's beat-edge trigger tests `accentFx == CE_NONE` and bails, so an entry
  // built from a v1.1 line (no ae=) can never arm the overlay.
  ContractEffect accentFx;             // 1B; never CE_NATIVE/CE_SCAN/... (rejected at parse)
  ContractRGB    accentColor;          // 3B; RESOLVED at insert: ac= if given, else the entry's color
  uint8_t        accentDur10;          // 1B; duration / 10ms (18 => 180ms). Keeps the entry small
                                       // on AVR: a 2550ms ceiling is far beyond any musical accent.
  // ---- v1.3 (4 more bytes: 22 an entry on AVR)
  uint8_t        bright;               // b= as a fraction of the ceiling (contractLevel). Absent == 255.
  uint8_t        accentEvery;          // accentEveryPack(an, ap): every-N accents for am=2
  uint16_t       crc;                  // contractCrc16() of the line that made this entry (verb V)
  // The defaults are set by this constructor, NOT by default member initialisers, on purpose. With
  // initialisers the type is constant-initialised, so a fork's score table (a static array of
  // these) becomes a pre-built image in flash that start-up copies to RAM: 22 bytes of flash an
  // entry. A constructor that is not constexpr leaves the table in .bss and fills it with one
  // small loop instead (measured with avr-gcc 7.3 on a 12-entry table: 134 bytes less flash).
  // Every ScoreEntry, wherever it is declared, still starts with exactly these values.
  ScoreEntry() {
    atBeat = 0; effect = CE_SOLID; nativeCode = -1;
    color.r = color.g = color.b = 0;
    speed = 128; beatMod = 0; accentMode = 0;
    accentFx = CE_NONE;
    accentColor.r = accentColor.g = accentColor.b = 0;
    accentDur10 = 18; bright = 255; accentEvery = 0; crc = 0;
  }
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
// The shortest cycle of a look that lights its whole field once a cycle (pulse, wipe). The S9
// gate lets a field light again 340 ms after it last got BRIGHTER, which is the cycle's peak, so
// the falling half and the first tenth of the next rise have to take 340 ms between them: a
// triangle needs 618 ms, a wipe on 8 positions 605. Shorter, and the gate holds the rise back.
// Measured (test/host/gate_held_table.cpp): nothing is held from 640 ms up with frames 10 to
// 15 ms apart, and from 680 ms up with frames as much as 40 ms apart. 680 is four states of the
// strobe floor.
static const uint32_t FX_CYCLE_MIN_MS = 4u * STROBE_MIN_STATE_MS;
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
// comet: a lit head with a fading tail (two fifths of the strand) that laps the strand. Every part
// of the strand goes dark and lit once a lap, and the whole-field gate cannot see that: the
// field's level never moves. So a lap is a cycle like a pulse's, and is never shorter than
// FX_CYCLE_MIN_MS (CA1.6: on 8 columns at s=255 a lap took 240 ms, and each half of the display
// lit four times a second. At 340 ms a lap the comet alone keeps to three, and one hand-over
// in that second is a fourth; at 680 ms a host that alternates the comet with a full look, as
// fast as S4 lets it, lights no quarter of the field more than three times a second).
inline uint32_t fxCometStepMs(uint8_t speed, int N) {
  uint32_t s = fxStepMs(speed);
  if (N <= 0) return s;
  const uint32_t least = (FX_CYCLE_MIN_MS + (uint32_t)N - 1u) / (uint32_t)N;
  return s < least ? least : s;
}
inline int fxHead(uint32_t elapsed, uint8_t speed, int N) {
  if (N <= 0) return 0;
  return (int)((elapsed / fxCometStepMs(speed, N)) % (uint32_t)N);
}
inline uint8_t fxCometBright(int p, int head, int N) {
  if (N <= 0) return 0;
  int trail = (int)(((uint32_t)N * 2u) / 5u); if (trail < 2) trail = 2;   // 2*N wraps 16-bit int for N > 16383
  int dist = (head - p) % N; if (dist < 0) dist += N;
  if (dist >= trail) return 0;
  // dist*255 wraps a 16-bit int for dist > 128, i.e. any strand with N > ~322.
  return (uint8_t)(255u - ((uint32_t)dist * 255u) / (uint32_t)trail);
}
// chase: every third position of a RING of N lit, the ring turning one position a step. The
// lit count is the same at every step, ceil(N / 3), whatever N is (CA1.5). It used to be every
// third position of the line, which lights 3, 3, then 2 of 8: the field mean moved by an eighth
// of the ceiling at every step, more than the S9 gate's tenth. Where N is not a multiple of 3
// one gap of the ring is shorter than the others, and it travels with the rest.
inline bool fxChaseLit(int p, uint32_t elapsed, uint8_t speed, int N) {
  if (N <= 0) return false;
  uint32_t s = fxStepMs(speed); if (!s) s = 1;
  return ((((uint32_t)p + elapsed / s) % (uint32_t)N) % 3u) == 0u;
}
// A ping-pong wipe: fill 0 -> N-1, then DRAIN BACK from the far end. The drain half used to
// read `p > (ph - N)`, which empties from position 0 upward — i.e. the erase head travelled
// the SAME direction as the fill head, so the effect was really two forward sweeps and never
// ping-ponged at all. That contradicted the spec, this function's own comment, and the JS
// visualizer's preview. The mirrored form below empties from N-1 downward, which is what a
// ping-pong is, and keeps the identical cadence (one position per step, N steps per half).
// One wipe, fill and drain, takes 2 * N steps, and never less than FX_CYCLE_MIN_MS (CA1.5): a
// short strand at a high speed lit its whole field again less than 340 ms after it was last
// full, and the S9 gate held the fill back. The step is stretched where 2 * N of them are shorter.
inline uint32_t fxWipeStepMs(uint8_t speed, int N) {
  uint32_t s = fxStepMs(speed);
  const uint32_t least = (FX_CYCLE_MIN_MS + (uint32_t)N * 2u - 1u) / ((uint32_t)N * 2u);
  return s < least ? least : s;
}
inline bool fxWipeLit(int p, uint32_t elapsed, uint8_t speed, int N) {
  if (N <= 0) return false; uint32_t s = fxWipeStepMs(speed, N);
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

// ===== v1.3: PER-UNIT STATE AND LIFECYCLE (spec sections 3 S4/S6, 5 L1-L5) =====
// One struct holds everything a show owns on one unit, and one function resets it, so a fork
// cannot forget a field at a show boundary. A fork embeds ContractUnitState and adds only what
// is specific to its board.

// What the display falls back to when no overlay is up.
//   CB_NONE    the contract holds nothing: the board's own native look runs. With inShow set this
//              is "armed, nothing shown yet" (L1): M:v=show keeps the native idle look until the
//              show's first C, live A or P, so the dome is not dark between Send and Play.
//   CB_DARK    the show's dark baseline (after contractShowBegin(), or after X).
//   CB_LOOK    a contract look.
//   CB_NATIVE  a native:<code> look the contract selected.
enum ContractBase : uint8_t { CB_NONE = 0, CB_DARK, CB_LOOK, CB_NATIVE };

// One verb-A line as a patch: which keys it carried (has), and their values.
enum : uint8_t { LP_EFFECT = 1, LP_COLOR = 2, LP_SPEED = 4, LP_BRIGHT = 8, LP_BEATMOD = 16, LP_AM = 32,
                 LP_EVERY = 64 };
struct ContractLookPatch {
  uint8_t        has = 0;
  ContractEffect effect = CE_NONE;
  int            nativeCode = -1;
  ContractRGB    color{0, 0, 0};
  uint8_t        speed = 0, bright = 0, beatMod = 0, accentMode = 0;
  uint8_t        every = 0;             // accentEveryPack(an, ap)
};
inline ContractLookPatch lookPatchFrom(const ContractParams& pr) {
  ContractLookPatch lp;
  if (pr.hasEffect)  { lp.has |= LP_EFFECT;  lp.effect = pr.effect; lp.nativeCode = pr.nativeCode; }
  if (pr.hasColor)   { lp.has |= LP_COLOR;   lp.color = pr.color; }
  if (pr.hasSpeed)   { lp.has |= LP_SPEED;   lp.speed = pr.speed; }
  if (pr.hasBright)  { lp.has |= LP_BRIGHT;  lp.bright = pr.bright; }
  if (pr.hasBeatMod) { lp.has |= LP_BEATMOD; lp.beatMod = pr.beatMod; }
  if (pr.hasAm)      { lp.has |= LP_AM;      lp.accentMode = pr.accentMode; }
  if (pr.hasAn || pr.hasAp) { lp.has |= LP_EVERY; lp.every = accentEveryFrom(pr); }
  return lp;
}
// Merge a newer patch over an older one, key by key: the newest value of each key wins (S4).
inline void lookPatchMerge(ContractLookPatch& into, const ContractLookPatch& newer) {
  if (newer.has & LP_EFFECT)  { into.effect = newer.effect; into.nativeCode = newer.nativeCode; }
  if (newer.has & LP_COLOR)   into.color = newer.color;
  if (newer.has & LP_SPEED)   into.speed = newer.speed;
  if (newer.has & LP_BRIGHT)  into.bright = newer.bright;
  if (newer.has & LP_BEATMOD) into.beatMod = newer.beatMod;
  if (newer.has & LP_AM)      into.accentMode = newer.accentMode;
  if (newer.has & LP_EVERY)   into.every = newer.every;
  into.has |= newer.has;
}
// L5: an A with no i= on a unit that has no look yet is i=solid.
inline void lookPatchDefault(ContractLookPatch& lp, uint8_t base) {
  if (!(lp.has & LP_EFFECT) && (base == CB_NONE || base == CB_DARK)) {
    lp.has |= LP_EFFECT; lp.effect = CE_SOLID; lp.nativeCode = -1;
  }
}

// THE SAFETY ANCHORS of a unit: what no reset clears (contractShowReset). They are a struct of
// their own, and the reset assigns the other one (ContractShowState): a field added here is
// kept without anyone having to remember it.
struct ContractAnchors {
  uint32_t       lastApplyMs = 0;       // S4: when the unit's look last changed (a live A, a section)
  uint32_t       pulseLastMs = 0;       // the strobe cool-down
  // ---- S9: the field gate (contractFieldGate)
  uint32_t       fieldOnsetMs = 0;      // when the field last lit, or last got brighter while lit
  uint8_t        fieldRef = 0;          // lit: the brightest level since it lit. Not lit: the darkest since it fell.
  bool           fieldLit = false;
  uint8_t        fieldRefY = 0;         // the same two for the field's LUMINANCE (a colour change moves it
  bool           fieldLitY = false;     // while the brightest channel stays where it was)
  bool           fieldRise = false;     // a measure lit and none has fallen since: the other may light with it (CA1.6)
  // ---- S4 for every path: the look gate (contractLookGate)
  uint32_t       lookSig = 0;           // the look the field last showed lit (contractLookSig); 0: none yet
  uint32_t       lookMs = 0;            // when it changed to that, or the field last lit or got brighter in it
  // ---- S9 for a region: verb L (contractLevelOffer)
  uint32_t       levelMs = 0;           // when the unit's level last went UP
};
// What a show reset clears: everything about a unit but its anchors (CA1.6: a struct of its own,
// so that the reset assigns this part and never has to save the other).
struct ContractShowState {
  bool           inShow = false;        // M:v=show armed (it stays set through X)
  uint8_t        base = CB_NONE;        // ContractBase
  // ---- the look
  ContractEffect effect = CE_OFF;
  int            nativeCode = -1;
  ContractRGB    color{0, 0, 0};
  uint8_t        speed = 128;
  uint8_t        ceil = 255;            // S6: the board's OWN brightness when the unit was armed
  uint8_t        bright = 255;          // b= / B: a FRACTION of ceil (255 = the ceiling). See contractLevel().
  uint8_t        beatMod = 0, accentMode = 0;
  uint8_t        accentEvery = 0;       // accentEveryPack(an, ap)
  uint32_t       startMs = 0;
  // ---- S4: the held live look
  bool           holdPending = false;
  ContractLookPatch held;
  // ---- the accent overlay
  bool           pulseActive = false;
  ContractEffect pulseFx = CE_NONE;
  ContractRGB    pulseColor{255, 255, 255};
  uint16_t       pulseDurMs = 0;
  uint32_t       pulseStartMs = 0;
  uint8_t        pulseBright = 255;     // the overlay's own brightness fraction (P's b=): never the base look's
  // ---- the score cursor, and the load check
  uint8_t        scoreLen = 0;          // entries held (the table itself is the fork's, sized by its cap)
  int8_t         curSection = -1;
  bool           pinForward = false;    // C2: the last C was a nudge
  int32_t        sectionStart = 0, sectionEnd = 0;
  bool           overflow = false;      // an insert met a full table: the load is bad
  uint8_t        loadOk = 0;            // 0 no V yet, 1 good, 2 bad. 2 survives until the next M:v=show.
  int32_t        lastAccentBeat = BEAT_NONE;
  // ---- L2
  uint8_t        wdSec = 0;             // 0 = no watchdog
  // ---- verb L: the level a meter look shows (contractLevelOffer)
  uint8_t        level = 0;             // what the unit shows
  uint8_t        levelWant = 0;         // the last L. Above `level`: a raise that waits for the window (contractLevelDue)
};
// A unit: its anchors, and its show state. Every field is reached as before (s.base, s.fieldLit).
struct ContractUnitState : ContractAnchors, ContractShowState {};

// S6: the level a brightness fraction gives under a ceiling. 255 is the ceiling itself, 128 half
// of it, so a contract line can dim a board and can never make it brighter than its owner set it.
inline uint8_t contractLevel(uint8_t ceil, uint8_t fraction) {
  return (uint8_t)(((uint16_t)ceil * (uint16_t)fraction) / 255u);
}

// L1: THE show reset. `ceiling` is the board's own brightness right now (S6).
// The safety anchors are kept on purpose: the strobe cool-down, the live-look hold and the field
// gate (S9). A reset that cleared them would be a way straight through the photosensitivity cap
// (X, then P, repeated). A failed load check (loadOk == 2) is kept too; contractOnShow() is what
// clears it.
inline void contractShowReset(ContractUnitState& s, uint8_t ceiling) {
  const uint8_t keepOk = (s.loadOk == 2) ? 2 : 0;
  static_cast<ContractShowState&>(s) = ContractShowState();   // the anchors are not touched
  s.loadOk = keepOk;
  s.ceil = ceiling;
}
// M:v=show[,wd=<s>]: reset and arm. base stays CB_NONE: the native idle look keeps running until
// contractShowBegin().
inline void contractOnShow(ContractUnitState& s, uint8_t ceiling, uint8_t wdSec) {
  contractShowReset(s, ceiling);
  s.loadOk = 0; s.inShow = true; s.wdSec = wdSec;
}
// X: reset and black out. The unit stays in whichever mode it was in, with its watchdog armed.
inline void contractOnStop(ContractUnitState& s, uint8_t ceiling) {
  const bool was = s.inShow; const uint8_t wd = s.wdSec;
  contractShowReset(s, ceiling);
  s.inShow = was; s.wdSec = wd; s.base = CB_DARK;
}
// M:v=idle (and the watchdog, and a failed V): give the board back completely.
inline void contractOnIdle(ContractUnitState& s, uint8_t ceiling) { contractShowReset(s, ceiling); }
// The show's first C, live A or P on an armed unit: the baseline becomes dark. true: it changed
// just now (the fork blacks the unit out before it applies the line). A scored A is NOT a start:
// the load burst arrives between Send and Play.
inline bool contractShowBegin(ContractUnitState& s) {
  if (!s.inShow || s.base != CB_NONE) return false;
  s.base = CB_DARK;
  return true;
}
// S6: verb B is honoured only in show mode, so a host sends it after M:v=show. (Every reset puts
// `bright` back to 255, so a B never outlives its show.)
inline bool contractBrightHonoured(const ContractUnitState& s) { return s.inShow; }
// L1: a board's beat clock stops at a reset only when NO unit on it needs it any more.
inline bool contractUnitNeedsClock(const ContractUnitState& s) {
  return s.scoreLen != 0 || s.base == CB_LOOK || s.base == CB_NATIVE;
}

// L2: has the host been silent for wd seconds? lastLineMs is the millis of the last well-formed
// contract line addressed to the BOARD (any unit, any verb, an unanswered wildcard Q included).
inline bool contractWatchdogExpired(const ContractUnitState& s, uint32_t lastLineMs, uint32_t nowMs) {
  return s.inShow && s.wdSec != 0 && (uint32_t)(nowMs - lastLineMs) >= (uint32_t)s.wdSec * 1000UL;
}

// S4: live look changes. A live A that arrives less than STROBE_MIN_STATE_MS after the last
// APPLIED one is held; lines held in the same window merge key by key.
//   liveLookOffer()  true: apply `lp` now.  false: it was stored.
//   liveLookDue()    poll every pass. true: `out` is the held look and its window has ended; apply it.
inline bool liveLookOffer(ContractUnitState& s, const ContractLookPatch& lp, uint32_t nowMs) {
  if (!s.holdPending && strobeCoolDownExpired(s.lastApplyMs, nowMs, STROBE_MIN_STATE_MS)) {
    s.lastApplyMs = nowMs;
    return true;
  }
  if (s.holdPending) lookPatchMerge(s.held, lp); else s.held = lp;
  s.holdPending = true;
  return false;
}
inline bool liveLookDue(ContractUnitState& s, uint32_t nowMs, ContractLookPatch& out) {
  if (!s.holdPending || !strobeCoolDownExpired(s.lastApplyMs, nowMs, STROBE_MIN_STATE_MS)) return false;
  out = s.held;
  s.holdPending = false; s.lastApplyMs = nowMs;
  return true;
}

// ===== S9 FOR A REGION: VERB L (core revision CA1.6) =====
// The field gate measures a unit's whole field. A meter that follows verb L moves PART of it: on
// a PSI, L:v=255 and L:v=128 by turns light and darken the top half of the panel, and the gate,
// which dims whole frames and sees a mean that stays legal, let that through ten times a second.
// The limit belongs on the level itself, and it is the same for every fork:
//   * a level that is no higher than the one the unit shows is taken at once (a fall is never
//     delayed, like a blackout);
//   * a HIGHER level is taken at most once in 340 ms per unit. One that comes sooner is kept, the
//     newest of them, and becomes the level when the window ends, unless a level that is no
//     higher than the unit's has arrived by then.
// So no part of a meter goes from dark to lit more than three times a second through L.
// A fork wires two calls and reads one field:
//     verb L:        contractLevelOffer(state, v);         (it only notes what the host wants)
//     every pass:    contractLevelDue(state, now);         (its tick, or just before it draws)
//     the meter:     state.level, in place of the fork's own copy of v.
// contractLevelDue() returns true when state.level changed, for a fork that derives something
// from it then. A fork that wants a fall on the LEDs before its next pass calls it right after
// the offer as well. The level and what the host wants are cleared by the show reset; the time
// is an anchor.
inline void contractLevelOffer(ContractUnitState& s, uint8_t v) { s.levelWant = v; }
inline bool contractLevelDue(ContractUnitState& s, uint32_t nowMs) {
  if (s.levelWant == s.level) return false;
  if (s.levelWant > s.level) {
    if (!strobeCoolDownExpired(s.levelMs, nowMs, 2 * STROBE_MIN_STATE_MS)) return false;   // a raise inside the window: it waits
    s.levelMs = nowMs;
  }
  s.level = s.levelWant;
  return true;
}

// ===== v1.3 S9: ONE LIMIT FOR EVERY PATH — THE FIELD GATE =====
// S2 bounds the beat-driven changes and S4 the live looks, each on its own path. A host can still
// make a display flash by combining paths, or through one that has no limit of its own: a relight
// after X, B, a meter following L, a C that seeks between a lit and a dark section, a tempo re-seed
// that restarts the pump, two scored sections whose pumps meet at the seam. S9 closes all of them
// in ONE place, and at the place that counts: on the light a unit is about to put out.
//
// A fork renders its frame, measures the FIELD LEVEL (0..255: the mean over the unit's LEDs of
// each LED's brightest channel, so a red field counts like a white one), and passes it through
//     allowed = contractFieldGate(state, level, now);
// before the frame leaves. When allowed < level it scales the frame down to `allowed`.
//
// The rule. A change of a STEP (more than a tenth of the unit's ceiling, and at least 2 levels)
// is a swing; smaller changes are a look's own texture and pass. The unit is either LIT or not:
//   * LIT. The gate follows the level up, and each new peak is the end of the onset so far. A
//     fall of a step or more below the peak makes the unit not lit. A fall is never delayed: a
//     blackout is always immediate.
//   * NOT LIT. The gate follows the level down (the trough). A rise of a step or more above the
//     trough is an ONSET, and an onset is allowed only 2 * STROBE_MIN_STATE_MS (340 ms) after
//     the field last lit or last got brighter. Until then the unit may show no more than one
//     step less one above its trough.
// So, for ANY observer who calls the field dark below one level and lit above another at least a
// step higher: two dark-to-lit onsets are never less than 340 ms apart, which is at most three
// in any second. (Why "last got brighter" and not "last lit": an onset is over when the light
// stops rising. Timing from its start would let a slow rise be followed too closely.)
//
// What it costs a look: nothing, unless it swings the whole field by more than a tenth of the
// ceiling more often than every 340 ms. The looks that could are held to the floor already (S2,
// S4, S8); the gate squares off the fastest of them a little (a pulse faster than about 1.8
// cycles a second rises in a step instead of a ramp) and never makes anything brighter. A look
// with texture (a sparkle, a chase) moves its field mean by less than a step from frame to frame;
// docs/CHANGING_THE_CORE.md has the measured share of frames the gate holds back, look by look.
//
// Seventeen bytes of state a unit, with the look gate's (and four for verb L's time). It survives every reset (contractShowReset).
inline uint8_t contractFieldStep(uint8_t ceil) { return ceil < 10 ? (uint8_t)2 : (uint8_t)(ceil / 10u + 1u); }

// TWO MEASURES, ONE GATE (core revision CA1.4). The brightest channel cannot see colour: a field
// that goes from white to blue and back keeps its level and loses nine tenths of its light. So a
// fork that draws colour measures each frame twice,
//     level = the mean over the LEDs of each LED's brightest channel      (above)
//     lum   = the mean over the LEDs of each LED's luminance, Rec.709:
//             (54 * r + 183 * g + 19 * b) >> 8                            (contractLuminance)
// and the rule above runs on each, with ONE shared time: an onset in either measure needs 340 ms
// from the last onset, or the last brightening, in either. The step is the same for both.
// A fork with single-colour LEDs has one measure and uses contractFieldGate(): the same rule
// with lum = level.
//
// ONE RISE IS ONE ONSET (core revision CA1.6). A coloured field that fades in crosses the step
// in its brightest channel before it does in luminance (green at a tenth of the ceiling has a
// luminance of seven hundredths). With the shared time alone the second crossing came inside the
// first one's 340 ms, was held, and the fade stalled there and then jumped. It is the same rise.
// So: a frame in which a measure lights (an onset the rule allowed) and no measure falls begins
// a RISE (fieldRise), and the rise lasts until a measure falls: goes from lit to not lit. While
// it lasts, the measure that is not lit yet may light without waiting. A second onset in any one
// measure still needs a fall in that measure first; the fall ends the rise, and the onset that
// follows waits its 340 ms from the shared time. So for an observer of either measure nothing
// changed: two of its onsets are never less than 340 ms apart. (Why, for luminance: between two
// of its onsets it fell, and that ended the rise. To light again inside the window it needs a
// new rise, begun after the fall by an onset of the level that waited for the window itself:
// 340 ms after the shared time, which is no earlier than luminance's last peak.)
// A frame in which one measure lights and another falls begins no rise: two looks that trade
// places could otherwise hand the rise to each other for ever.
inline uint8_t contractLuminance(uint8_t r, uint8_t g, uint8_t b) {
  return (uint8_t)(((uint16_t)54u * r + (uint16_t)183u * g + (uint16_t)19u * b) >> 8);
}
// One measure's part of the rule, in two halves. `open`: the 340 ms had passed when this frame
// began, or the unit is in a rise. ASK: what the measure allows for x (nothing changes).
inline uint8_t _fieldAsk(bool lit, uint8_t ref, uint8_t x, uint8_t step, bool open) {
  if (lit || open || x < ref || (uint8_t)(x - ref) < step) return x;      // lit already; an onset in time; texture
  return (uint8_t)(ref + step - 1u);                                      // held
}
// TELL: x, which the rule allowed, is what the unit shows. Sets `brighter` when the field lit,
// or got brighter while lit, in this measure. `rise` goes through both measures of a frame: it
// starts as fieldRise (0 or 1), an onset sets bit 0, and a fall makes it 2, where it stays.
inline void _fieldTell(bool& lit, uint8_t& ref, uint8_t x, uint8_t step, bool& brighter, uint8_t& rise) {
  if (lit) {
    if (x > ref) { ref = x; brighter = true; }                            // still rising
    else if ((uint8_t)(ref - x) >= step) { lit = false; ref = x; rise = 2; }      // a fall: never delayed
  } else if (x < ref) ref = x;                                            // the trough
  else if ((uint8_t)(x - ref) >= step) { lit = true; ref = x; brighter = true; rise |= 1; }   // an onset
}
// The field lit, or got brighter while lit: both of the gate's times move. The second is the
// look gate's (below). A field that lights changes its look, whatever look it lights in, and a
// look that fades in (a pulse, a wipe) is not on the field at its first, dim frame: the next
// change of look keeps its 170 ms from when this one stopped arriving.
inline void _fieldBrighter(ContractUnitState& s, uint32_t nowMs) { s.fieldOnsetMs = nowMs; s.lookMs = nowMs; }
// The gate for a frame with both measures, in its two halves.
// ASK: the SCALE for a frame drawn at (level, lum). 255: as drawn. Otherwise every channel of
// every LED is multiplied by scale / 255, rounding down. Nothing changes.
//     open  = fieldRise, or 340 ms have passed since fieldOnsetMs   (decided once, before either measure)
//     aL    = what the rule allows of level,  aY = what it allows of lum
//     scale = min(255, floor(aL * 255 / level), floor(aY * 255 / lum))
inline uint8_t contractFieldAsk(const ContractUnitState& s, uint8_t level, uint8_t lum, uint32_t nowMs) {
  const uint8_t step = contractFieldStep(s.ceil);
  const bool open = s.fieldRise || strobeCoolDownExpired(s.fieldOnsetMs, nowMs, 2 * STROBE_MIN_STATE_MS);
  const uint8_t aL = _fieldAsk(s.fieldLit, s.fieldRef, level, step, open);
  const uint8_t aY = _fieldAsk(s.fieldLitY, s.fieldRefY, lum, step, open);
  uint8_t scale = 255;
  if (aL < level) scale = (uint8_t)(((uint16_t)aL * 255u) / level);
  if (aY < lum) { const uint8_t sy = (uint8_t)(((uint16_t)aY * 255u) / lum); if (sy < scale) scale = sy; }
  return scale;
}
// TELL: the frame the unit SHOWS has this level and this luminance (a frame the ask allowed:
// as drawn, or scaled as it said). Both measures record it. Not what was drawn: a frame one
// measure held down is dimmer in the other one too, and a peak that never reached the LEDs must
// not be remembered. fieldOnsetMs = lookMs = now when either measure lit or got brighter.
// fieldRise after the frame: false when a measure fell in it; else true when one lit; else as it was.
inline void contractFieldTell(ContractUnitState& s, uint8_t level, uint8_t lum, uint32_t nowMs) {
  const uint8_t step = contractFieldStep(s.ceil);
  bool brighter = false; uint8_t rise = s.fieldRise;
  _fieldTell(s.fieldLit, s.fieldRef, level, step, brighter, rise);
  _fieldTell(s.fieldLitY, s.fieldRefY, lum, step, brighter, rise);
  if (brighter) _fieldBrighter(s, nowMs);
  s.fieldRise = rise == 1;
}
// Both in one call, for a fork that does not measure its frame again after scaling it: the
// scaled frame is recorded as floor(level * scale / 255) and floor(lum * scale / 255). (A fork
// that does measure the scaled frame asks, scales, measures and tells: per-LED rounding can
// leave the frame a level under that.)
inline uint8_t contractFieldScale(ContractUnitState& s, uint8_t level, uint8_t lum, uint32_t nowMs) {
  const uint8_t scale = contractFieldAsk(s, level, lum, nowMs);
  if (scale != 255) {
    level = (uint8_t)(((uint16_t)level * scale) / 255u);
    lum   = (uint8_t)(((uint16_t)lum * scale) / 255u);
  }
  contractFieldTell(s, level, lum, nowMs);
  return scale;
}
// ASK WITHOUT TELLING: what contractFieldGate() would return for `level` now, with no change to
// the state. For light a fork cannot scale after the fact, see below. (Such light always waits
// for the window: a rise is for frames a fork draws and measures, contractFieldAsk().)
inline uint8_t contractFieldAllowed(const ContractUnitState& s, uint8_t level, uint32_t nowMs) {
  const uint8_t step = contractFieldStep(s.ceil);
  const bool open = strobeCoolDownExpired(s.fieldOnsetMs, nowMs, 2 * STROBE_MIN_STATE_MS);
  const uint8_t aL = _fieldAsk(s.fieldLit, s.fieldRef, level, step, open);
  const uint8_t aY = _fieldAsk(s.fieldLitY, s.fieldRefY, level, step, open);
  return aL < aY ? aL : aY;
}
// The gate for one measure (white or single-colour light, and every native look): the level the
// unit may show, which is what both measures record. The same rule as contractFieldScale() with
// lum = level, for a fork that then shows that level itself; a fork that scales a drawn frame
// uses contractFieldScale(), which records the frame as the scaling rounds it.
inline uint8_t contractFieldGate(ContractUnitState& s, uint8_t level, uint32_t nowMs) {
  const uint8_t a = contractFieldAllowed(s, level, nowMs);
  contractFieldTell(s, a, a, nowMs);
  return a;
}
// A NATIVE LOOK is light the fork does not draw, cannot measure, and often cannot scale. It goes
// through the same gate, by the level the fork knows it by: 0 for a native blackout, the
// brightness the fork runs it at where it sets one (the logics), the ceiling otherwise. In two
// steps, because the gate must only ever be TOLD about light that reached the LEDs:
//   1. ASK, before handing the LEDs over: contractFieldAllowed(s, level, now) >= level. If not,
//      this would be an onset inside the window: the fork keeps what it is showing and asks again
//      on a later pass. (contractFieldMayLight() asks for a native look at the ceiling.)
//   2. TELL, at the moment the native look is actually on the LEDs: contractFieldGate(s, level, then).
// A fork that told the gate at step 1 and then did not show the look after all (a later line
// changed its mind before the frame) would leave the gate believing in a peak that never was,
// and a later, smaller rise could then slip through unmarked.
inline bool contractFieldMayLight(const ContractUnitState& s, uint32_t nowMs) {
  return contractFieldAllowed(s, s.ceil, nowMs) >= s.ceil;
}
// Light the fork did not choose and cannot hold back is on the LEDs: someone's button started a
// native look. There is nothing to allow. The gate records it, so that what the CONTRACT does
// next keeps its distance from it.
inline void contractFieldForced(ContractUnitState& s, uint8_t level, uint32_t nowMs) {
  if (contractFieldGate(s, level, nowMs) < level) {
    s.fieldLit = s.fieldLitY = true; s.fieldRef = s.fieldRefY = level;
    _fieldBrighter(s, nowMs);
  }
}

// ===== S4 FOR EVERY PATH: THE LOOK GATE (core revision CA1.4) =====
// The field gate limits how often a unit LIGHTS. A unit can also flicker without ever going dark:
// red, blue, red, at one level. S4 held a live A to one change in 170 ms; nothing held a section
// reached by a C seek, an accent thrown between two looks, or a display handed to a native look
// and taken back. The rule is now the same for all of them, in two places:
//   * a SECTION is applied only 170 ms after the unit's last look change, like a live A
//     (contractSectionMayApply; a section that has to wait is applied by a later tick, if the
//     beat is still in it). That keeps the old look moving while the new one waits;
//   * at the OUTPUT, whatever asked: a frame that would show a different look than the field last
//     showed lit is drawn only 170 ms after the field changed to that one (contractLookGate).
//     Until then the fork leaves the frame that is up. A BLACK frame is never a look and is never
//     held: a blackout is always immediate. "Changed to that one" is when it ARRIVED: the frame
//     of the change, and after it every frame in which the field lit or got brighter (the field
//     gate's own time, fieldOnsetMs: _fieldBrighter). So a look that fades in keeps the next
//     change away until 170 ms after it stopped rising, and so does a field that lights.
// A look is its effect and its colour (contractLookSig), or a native sequence
// (contractLookSigNative). Brightness is not part of it: that is the field gate's business.
inline uint32_t contractLookSig(ContractEffect e, const ContractRGB& c) {
  return ((uint32_t)e << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}
inline uint32_t contractLookSigNative(int code) { return ((uint32_t)CE_NATIVE << 24) | (uint32_t)(uint16_t)code; }
// ASK: may the field change to this look now? (For a native look: before handing the LEDs over.)
inline bool contractLookAllowed(const ContractUnitState& s, uint32_t sig, uint32_t nowMs) {
  return sig == s.lookSig || strobeCoolDownExpired(s.lookMs, nowMs, STROBE_MIN_STATE_MS);
}
// The gate, at the frame: true, draw it (and it is the field's look from now); false, leave what is up.
inline bool contractLookGate(ContractUnitState& s, uint32_t sig, uint32_t nowMs) {
  if (sig == s.lookSig) return true;
  if (!strobeCoolDownExpired(s.lookMs, nowMs, STROBE_MIN_STATE_MS)) return false;
  s.lookSig = sig; s.lookMs = nowMs;
  return true;
}
// THE FRAME AT WHICH A LOOK CAN BE SEEN (CA1.6). The look gate's time is that frame's. A look
// that comes up through a few levels (a pulse, a dim fade) has not changed the field's colour at
// its first frame: a frame whose BRIGHTEST LED is under LOOK_MIN_LEVEL levels shows no colour,
// and is no more a look than a black one. It is never held, and it does not take the gate; the
// first frame with an LED at 4 levels or more does, and the 170 ms run from there. (Taken at
// the first frame, the gate let the next look follow 171 ms after a frame nobody could see:
// 77 ms after the colour appeared, in the Flthy fork's streams.) A fork passes the brightest
// channel of the frame's brightest LED. Not the field level: one full column of a wide panel
// is a field level of 3 and a colour anyone can see. A fork that does not track the peak passes
// 255 for any frame that is not black, which is the rule as it was.
//     if (!contractLookAllowedAt(s, sig, peak, now)) keep what is up;         (ask)
//     ... the field gate ...
//     contractLookGateAt(s, sig, peak, now);                                  (the frame is shown)
// or contractLookGateAt() alone where it does both at once.
static const uint8_t LOOK_MIN_LEVEL = 4;
inline bool contractLookAllowedAt(const ContractUnitState& s, uint32_t sig, uint8_t peak, uint32_t nowMs) {
  return peak < LOOK_MIN_LEVEL || contractLookAllowed(s, sig, nowMs);
}
inline bool contractLookGateAt(ContractUnitState& s, uint32_t sig, uint8_t peak, uint32_t nowMs) {
  return peak < LOOK_MIN_LEVEL || contractLookGate(s, sig, nowMs);
}
// S11: "Re-applying the same look does not restart it." A fork asks this BEFORE it writes a live
// A or a section into the unit. true: the unit's base look is this very look already (the same
// effect, native code, colour and speed): its timeline carries on, and nothing changes for the
// gate. false: the look starts again. The fork then sets its start time and calls
// contractLookRestart(): a look whose timeline starts again (a gradient back at its first hue, a
// scan at its first column) is a change of look even when its effect and colour are the old
// ones, and the next lit frame goes through the gate as a new look.
inline bool contractLookSame(const ContractUnitState& s, ContractEffect effect, int nativeCode, const ContractRGB& c, uint8_t speed) {
  return (s.base == CB_LOOK || s.base == CB_NATIVE) && s.base == ((effect == CE_NATIVE && nativeCode >= 0) ? CB_NATIVE : CB_LOOK) &&
         s.effect == effect && s.nativeCode == nativeCode && s.speed == speed &&
         s.color.r == c.r && s.color.g == c.g && s.color.b == c.b;
}
inline void contractLookRestart(ContractUnitState& s) { s.lookSig = 0; }
// A look nobody asked the gate about is on the LEDs (a native look that started; a button).
inline void contractLookForced(ContractUnitState& s, uint32_t sig, uint32_t nowMs) {
  if (sig != s.lookSig) { s.lookSig = sig; s.lookMs = nowMs; }
}
// ===== S10: NATIVE LOOKS (core revision CA1.4) =====
// A native look on a fork's allow-list is one of two kinds (the spec's S10), and the fork's list
// says which (nativeGated<Board>() below):
//   * RUNS AS STOCK. Its whole field does not swing by more than a tenth more often than every
//     340 ms, or what moves is a small lit area. Its frames are NOT passed through the gate. The
//     gate governs its edges, with these four calls and nothing else:
//         contractNativeMayStart(s, code, level, now)   ASK, before the LEDs are handed over.
//                                                       false: keep what is up, ask again next pass.
//                                                       Yes only when an ONSET is allowed (340 ms
//                                                       after the field last lit or got brighter),
//                                                       whether the unit is lit or dark just now: a
//                                                       stock look may clear the unit and light it at
//                                                       once, or be in a dark stretch of its own and
//                                                       relight in its own time, and to anyone
//                                                       watching that light follows the contract's
//                                                       last. The same call hands a unit BACK to the
//                                                       board's own idle look.
//         contractNativeStarted(s, code, level, now)    TELL, at the frame it is on the LEDs.
//         contractNativeLevel(s, level, now)            TELL: the fork moved its brightness.
//         contractNativeEnded(s, known, now)            TELL: the fork draws again, from this frame.
//     `level` is the field level the look runs at, as the fork knows it: 0 for a native blackout,
//     the brightness the fork gives the look, the ceiling where it gives none. It is told ONCE,
//     when the look starts (and again only when the fork itself changes it): the look's own
//     frames are not the gate's business, and a fork does not report them.
//   * GATED. Its whole field does swing faster, and the fork can hold its frames: it measures
//     each one and passes it through the field gate like a contract look. A fork that asks the
//     gates where its frames are latched passes the look's signature, contractLookSigNative(code),
//     to contractLookAllowedAt() / contractLookGateAt() with the look's first lit frame, so the
//     170 ms run from the frame that is on the LEDs. contractLookForced() when the look is
//     SELECTED is for a fork that cannot hold that first frame: called before the frame, it
//     starts S4's window before the look can be seen, and the next look may follow it sooner
//     than 170 ms after it appeared (CA1.6, from the Flthy fork).
// TWO FIGURES (CA1.6). contractNativeStarted() and contractNativeLevel() take the look's level,
// and record it for both measures, which is right for white light and an upper bound for any
// other. A fork with colour LEDs whose stock look has a luminance well under its level (a
// rainbow: three quarters and a third) passes both, from a frame it measured: with one figure
// the gate believes in luminance that is not there, and a contract frame that is dimmer by
// level and brighter by luminance is no onset to it.
// A fork that cannot hold a hand-over back (the look is on the LEDs already: a button) skips the
// ask and tells; the gate then records light it would have refused, and what the CONTRACT does
// next keeps its distance from it.
inline bool contractNativeMayStart(const ContractUnitState& s, int code, uint8_t level, uint32_t nowMs) {
  if (level == 0) return true;                                                           // a blackout is never held
  return strobeCoolDownExpired(s.fieldOnsetMs, nowMs, 2 * STROBE_MIN_STATE_MS) &&        // S9: its light is an onset
         contractLookAllowed(s, contractLookSigNative(code), nowMs);                     // S4
}
inline void contractNativeLevel(ContractUnitState& s, uint8_t level, uint32_t nowMs) { contractFieldForced(s, level, nowMs); }
inline void contractNativeLevel(ContractUnitState& s, uint8_t level, uint8_t lum, uint32_t nowMs) { contractFieldTell(s, level, lum, nowMs); }
inline void contractNativeStarted(ContractUnitState& s, int code, uint8_t level, uint32_t nowMs) {
  contractFieldForced(s, level, nowMs);
  if (level == 0) return;
  contractLookForced(s, contractLookSigNative(code), nowMs);
  // A stock look starting is light the gate did not draw, and its first frames may be an onset
  // nobody can see from here. Whatever the contract lights next keeps 340 ms from this frame.
  s.fieldOnsetMs = nowMs;
}
inline void contractNativeStarted(ContractUnitState& s, int code, uint8_t level, uint8_t lum, uint32_t nowMs) {
  contractFieldTell(s, level, lum, nowMs);          // what is on the LEDs, whatever the rule would have said of it
  if (level == 0) return;
  contractLookForced(s, contractLookSigNative(code), nowMs);
  s.fieldOnsetMs = nowMs;
}
// `known`: the look that ends is on the fork's allow-list AND ran at the level the gate was told.
// One that is not on the list (someone's button started it) may flash as it likes, and one that
// draws at a level of its own may stand anywhere: nothing is known of the field, so it is taken
// as dark and just lit, and the first lit frame the fork draws after it waits the window out.
// A fork does not call this for its idle look, if that is steady light: a show starts from it.
inline void contractNativeEnded(ContractUnitState& s, bool known, uint32_t nowMs) {
  s.fieldOnsetMs = nowMs;
  if (!known) { s.fieldLit = s.fieldLitY = s.fieldRise = false; s.fieldRef = s.fieldRefY = 0; }
}

// A scored section is due: may it be applied now? true: it may, and this is the unit's last look
// change from now (the same anchor a live A uses, so the two keep their distance from each other).
inline bool contractSectionMayApply(ContractUnitState& s, uint32_t nowMs) {
  if (!strobeCoolDownExpired(s.lastApplyMs, nowMs, STROBE_MIN_STATE_MS)) return false;
  s.lastApplyMs = nowMs;
  return true;
}

// The accent overlay: is it still up at nowMs? Takes it down when its time is over. ONE definition
// of the expiry, for the fork's render and for its tick (the tick is what hands the LEDs back).
inline bool contractOverlayUp(ContractUnitState& s, uint32_t nowMs) {
  if (s.pulseActive && (uint32_t)(nowMs - s.pulseStartMs) >= s.pulseDurMs) s.pulseActive = false;
  return s.pulseActive;
}

// ===== v1.3: SCORE CAPACITY, LOAD VERIFICATION, QUERY (spec sections 6, 7, 10) =====

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final XOR; "123456789" is
// 0x29B1) of a NUL-terminated line: the bytes from the class character to the last parameter
// character — no '!', no WCB route, no terminator. Bitwise: no table, about 40 B of AVR flash.
inline uint16_t contractCrc16(const char* s) {
  uint16_t crc = 0xFFFFu;
  for (; *s; s++) {
    crc ^= (uint16_t)((uint16_t)(uint8_t)*s << 8);
    for (uint8_t i = 0; i < 8; i++)
      crc = (crc & 0x8000u) ? (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
  }
  return crc;
}

// scoreInsert() that says what happened. Entries stay sorted by atBeat; an equal atBeat replaces.
enum ScoreInsert : uint8_t { SI_INSERTED = 0, SI_REPLACED = 1, SI_FULL = 2 };
inline ScoreInsert scoreInsertEx(ScoreEntry* entries, uint8_t& n, uint8_t cap, const ScoreEntry& e) {
  uint8_t i = 0;
  while (i < n && entries[i].atBeat < e.atBeat) i++;
  if (i < n && entries[i].atBeat == e.atBeat) { entries[i] = e; return SI_REPLACED; }
  if (n >= cap) return SI_FULL;
  for (uint8_t j = n; j > i; j--) entries[j] = entries[j - 1];
  entries[i] = e;
  n++;
  return SI_INSERTED;
}
// A scored A line for this unit: store it with the CRC of its line. false: it was NOT stored.
//   * The table was full: the load is marked bad, which verb V then reports (overflow is an
//     error, not a silent drop).
//   * The unit failed its load check (loadOk == 2): it stays on its native look until the next
//     M:v=show, so every scored line until then is dropped. (A line that arrives late, after the
//     V, must not start half a show.)
// A line delivered twice replaces itself and changes NOTHING, the section cursor included: a
// re-sent load does not restart the section that is playing.
inline bool scoreAccept(ContractUnitState& s, ScoreEntry* entries, uint8_t cap, ScoreEntry e, uint16_t lineCrc) {
  if (s.loadOk == 2) return false;
  e.crc = lineCrc;
  uint8_t i = 0;
  while (i < s.scoreLen && entries[i].atBeat < e.atBeat) i++;
  const bool again = i < s.scoreLen && entries[i].atBeat == e.atBeat && entries[i].crc == lineCrc;
  if (scoreInsertEx(entries, s.scoreLen, cap, e) == SI_FULL) { s.overflow = true; return false; }
  if (!again) { s.curSection = -1; s.pinForward = false; }   // the table changed: re-evaluate the cursor
  return true;
}
// k: the XOR of the held entries' line CRCs. Independent of arrival order and of duplicates.
inline uint16_t scoreK(const ScoreEntry* entries, uint8_t n) {
  uint16_t k = 0;
  for (uint8_t i = 0; i < n; i++) k ^= entries[i].crc;
  return k;
}
// Verb V: n = entries the host expects this unit to hold, k = the XOR of their line CRCs.
// Returns the verdict and stores it in s.loadOk: 1 good, 2 bad (a count or checksum mismatch, or
// the table overflowed). On 2 the fork runs its M:v=idle path on the unit; contractShowReset()
// keeps the 2, so Q reports it until the next M:v=show.
inline uint8_t scoreVerify(ContractUnitState& s, const ScoreEntry* entries, uint8_t n, uint16_t k) {
  s.loadOk = (!s.overflow && s.scoreLen == n && scoreK(entries, s.scoreLen) == k) ? 1 : 2;
  return s.loadOk;
}

// Is this Q one a board answers? Only a query addressed to it by class AND unit, carrying r=.
// A wildcard Q is never answered (it still feeds the watchdog: it is Studio's keepalive).
inline bool contractQueryAnswerable(const ParsedContract& p) {
  return p.verb == CV_QUERY && p.cls != '*' && p.unit != '*' && p.params.hasRoute;
}
// The reply to Q:r=<route>, routed back the way SHADOW routes its replies so the WCB never
// broadcasts it:   r=0  ";S0!<cls><unit>q:ver=1.3,rel=1,cap=<n>,n=<held>,ok=<0|1|2>,bpm=<n>\r"
//                  r=n  ";W<n>,;S0!<cls><unit>q:...\r"
// No NUL is written. Returns the length (at most CONTRACT_REPLY_MAX), or 0 when `out` is smaller
// than CONTRACT_REPLY_MAX. No printf: that is over 1 KB of flash on an ATmega.
inline uint8_t _qStr(char* o, uint8_t i, const char* flashStr) {       // flashStr: a CONTRACT_PSTR() literal
  for (;;) {
    const char c = (char)CONTRACT_READ_BYTE(flashStr);
    if (c == '\0') return i;
    flashStr++;
    o[i++] = c;
  }
}
inline uint8_t _qNum(char* o, uint8_t i, uint16_t v) {
  char t[5]; uint8_t n = 0;
  do { t[n++] = (char)('0' + (v % 10u)); v = (uint16_t)(v / 10u); } while (v);
  while (n) o[i++] = t[--n];
  return i;
}
inline uint8_t contractFormatQuery(char* out, uint8_t outLen, uint8_t route, char cls, char unit,
                                   uint8_t cap, uint8_t held, uint8_t ok, uint16_t bpm) {
  if (outLen < CONTRACT_REPLY_MAX) return 0;
  if (route > CONTRACT_ROUTE_MAX) route = CONTRACT_ROUTE_MAX;
  uint8_t i = 0;
  if (route != 0) { i = _qStr(out, i, CONTRACT_PSTR(";W")); i = _qNum(out, i, route); out[i++] = ','; }
  i = _qStr(out, i, CONTRACT_PSTR(";S0!"));
  out[i++] = cls; out[i++] = unit;
  i = _qStr(out, i, CONTRACT_PSTR("q:ver=" CONTRACT_VERSION ",rel=")); i = _qNum(out, i, CONTRACT_RELEASE);
  i = _qStr(out, i, CONTRACT_PSTR(",cap="));  i = _qNum(out, i, cap);
  i = _qStr(out, i, CONTRACT_PSTR(",n="));    i = _qNum(out, i, held);
  i = _qStr(out, i, CONTRACT_PSTR(",ok="));   i = _qNum(out, i, ok > 2 ? 2 : ok);
  i = _qStr(out, i, CONTRACT_PSTR(",bpm="));  i = _qNum(out, i, bpm > 999u ? 999u : bpm);
  out[i++] = '\r';
  return i;
}

// ===== v1.3: ONE TIMING DEFINITION FOR flash / pulse / scan (spec S8 and section 12) =====
// Up to v1.2 each fork had its own formula for these three, so the same s= flashed, breathed
// and swept at different rates on the three boards. These are the shared definitions.
// R1 adopts ONLY the pulse floor (S8: a pulse cycle is never shorter than 340 ms); the forks
// switch flash and scan to these in R2, because that changes how those looks appear.
// (CA1.5: the fastest state is 190 ms, not STROBE_MIN_STATE_MS. A flash that lights exactly
// every 340 ms meets the S9 gate's 340 ms window with no room at all: a frame drawn a few
// milliseconds late makes the next ON edge early by as much, the gate holds it for a frame, and
// every held edge moves the next one later still, until a whole flash is lost. With 20 ms a
// state to spare nothing is held with frames up to 40 ms apart: test/host/gate_held_table.cpp.)
static const uint32_t FX_FLASH_MIN_STATE_MS = STROBE_MIN_STATE_MS + 20u;
inline uint32_t fxFlashHalfMs(uint8_t speed) { return FX_FLASH_MIN_STATE_MS + (uint32_t)(255 - speed); }   // 190..445 ms a state
inline bool fxFlashOn(uint32_t elapsed, uint8_t speed) {
  const uint32_t h = fxFlashHalfMs(speed);
  return (elapsed % (2u * h)) < h;
}
// pulse: a triangle breathe 0..255..0. The full cycle is 2000 ms at s=0 down to FX_CYCLE_MIN_MS
// at s=255 (it was 340 ms until CA1.5: S8's floor, which the S9 gate then squared off).
inline uint32_t fxPulsePeriodMs(uint8_t speed) {
  return 2000u - ((uint32_t)speed * (2000u - FX_CYCLE_MIN_MS)) / 255u;
}
inline uint8_t fxPulseBright(uint32_t elapsed, uint8_t speed) {
  const uint32_t period = fxPulsePeriodMs(speed);
  const uint32_t ph = elapsed % period;
  const uint32_t tri = (ph < period / 2u) ? (ph * 510u / period) : ((period - ph) * 510u / period);
  return (uint8_t)(tri > 255u ? 255u : tri);         // an odd period's midpoint computes 256
}
// scan: ONE lit position on a strand of N, one step per fxStepMs(speed). wrap=false ping-pongs
// 0..N-1..0 (a panel); wrap=true goes round (a ring).
inline int fxScanPos(uint32_t elapsed, uint8_t speed, int N, bool wrap) {
  if (N <= 1) return 0;
  const uint32_t step = elapsed / fxStepMs(speed);
  if (wrap) return (int)(step % (uint32_t)N);
  const uint32_t span = ((uint32_t)N - 1u) * 2u;     // widened: 2*N wraps a 16-bit int past 16383
  const uint32_t k = step % span;
  return (k < (uint32_t)N) ? (int)k : (int)(span - k);
}

// ===== v1.3 S7: NATIVE ALLOW-LISTS, AS DATA =====
// Each fork lists the native:<n> codes it can run safely with complete state; a line naming any
// other code is dropped whole. The lists live here, not in the forks' render layers, so the
// generated vector file can carry them and Studio's vocabulary is tested against the firmware's
// own list.
// RSeries logics (LogicEngine sequence ids, and the fork's custom effects from 101). The engine
// draws these itself, so there is no gated kind here (S10): a sequence is on the list when it
// passes S10's test as the contract starts it (default colour, the speed below, no microphone
// source): by brightness and by luminance, no two onsets closer than 340 ms, over the whole
// field and over any region of a quarter of it or more. test/host/test_native_s10.cpp is that
// test, on a model of the stock code, and DROIDNET_FORK.md has its table. On the list:
//    0 normal, 1 alarm, 2 failure, 4 march, 5 solid colour, 6 flash colour, 7 and 8 flip flop,
//    9 colour swap, 10 rainbow, 11 red alert, 12 and 13 the mic effects, 14 lights out,
//    19 roaming pixel, 20 and 21 the scan lines, 22 fire, 23 PSI colour wipe, 24 pulse,
//    101 plasma, 105 colour wheel.
// Not on it:
//    3 Leia           (sets its own brightness, 50..255, every frame: past the ceiling, deaf to B)
//    15..18 text      (they need a message)
//    102 metaballs, 103 fractal (whole-field colour and level changes, many a second)
//    104 fade and scroll (what it does is drawn at random when it starts)
//    25 (one past the engine's effect table, an out-of-bounds call), 99 (RANDOM can pick
//    anything), 100 (the bitmap needs an SD card), 106 (the contract's own renderer).
inline bool nativeAllowedLogics(int code) {
  return (code >= 0 && code <= 14 && code != 3) || (code >= 19 && code <= 24) || code == 101 || code == 105;
}
// S10: "where a stock look's rate follows a speed setting, the contract starts it at the fastest
// speed that passes". The engine's speed digit (0..9) a fork selects the sequence with:
//    4 march            speed 1: its thirds flip every 300 ms, not every 150 (a region lights every 600 ms)
//    19 roaming pixel   speed 1: a pixel every 50 ms, not every 10 (a rear row is a quarter of the field)
//    20 horizontal scan speed 2: a row every 150 ms, not every 50 (the same rear row)
//    22 fire            speed 1: the field is rolled again every 200 ms, not every 100
// and 0, the engine's own, for every other code.
inline uint8_t nativeSpeedLogics(int code) {
  return code == 20 ? 2 : (code == 4 || code == 19 || code == 22) ? 1 : 0;
}
// S6: which of them draw at the board's brightness, so that b= and B reach them. The rest (19..24
// and the custom effects) set their pixels at a level of their own: a native look selected
// through the contract looks as it does when its button is pressed, and a host has no use for b=
// on it. For the field gate such a look is lit at full scale, whatever the ceiling.
inline bool nativeDimsLogics(int code) { return code >= 0 && code <= 14 && nativeAllowedLogics(code); }
// Which of them hold the WHOLE field within a tenth of the level the gate is told, for as long
// as they run (march and the flip flops do: their halves take turns). What a fork may rely on
// when such a look ends; after any other the field may stand anywhere (contractNativeEnded).
inline bool nativeSteadyLogics(int code) {
  return code == 0 || code == 1 || code == 4 || code == 5 || code == 7 || code == 8 || (code >= 11 && code <= 14);
}
inline bool nativeGatedLogics(int) { return false; }
// PSI Pro (its mode numbers): 0..22 and 92, WITHOUT Flash (2), Alarm (3) and Scream (5), which are
// full-field white at 8.3 and 4 Hz — past the strobe floor this contract holds everywhere else.
inline bool nativeAllowedPsi(int code) {
  return (code >= 0 && code <= 22 && code != 2 && code != 3 && code != 5) || code == 92;
}
// Flthy holoprojectors (its LED function numbers): Leia (1) and rainbow (6), until the contract's
// own looks replace the rest of its native hatch.
inline bool nativeAllowedFlthy(int code) { return code == 1 || code == 6; }
// S10: which allow-listed looks are GATED (the fork passes their frames through the field gate)
// rather than run as stock. The holoprojector's Leia re-rolls all seven pixels together, about
// four whole-field swings a second as stock.
inline bool nativeGatedPsi(int) { return false; }
inline bool nativeGatedFlthy(int code) { return code == 1; }
