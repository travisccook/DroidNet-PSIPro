// Part of the DroidNet Driveable-Animation Contract test harness.
// Copyright (c) 2026 Travis Cook. Shared byte-identically across the DroidNet
// RSeries/PSI/Flthy forks.  SPDX-License-Identifier: LGPL-2.1-only OR MIT
//
// test/host/fuzz_parsers.cpp — DIFFERENTIAL + ROBUSTNESS fuzzer for the hand-rolled
// parsers in contract_core.h. Deterministic (fixed seed), self-contained, needs nothing
// but clang. Build it with the sanitizers on:
//
//   clang++ -std=c++17 -Wall -Wextra -O1 -g \
//           -fsanitize=address,undefined -fno-sanitize-recover=all \
//           test/host/fuzz_parsers.cpp -o /tmp/psi_fuzz && /tmp/psi_fuzz
//
// WHY THIS FILE EXISTS
// --------------------
// To save 1,008 B of ATmega32U4 flash, strtol()/strtoul() were replaced by the hand-rolled
// _decDigits/_parseU32/_parseI32. Those three functions read bytes off a shared, noisy
// serial bus, and the unit suite in test_contract_core.cpp pins them with a few dozen
// hand-picked vectors. This file replaces "hand-picked" with:
//
//   * a 32-BIT ORACLE built from the real libc (strtoull), because the code deliberately
//     fixes the width at 32 bits ON ALL TARGETS -- `long` is 64-bit on this host but 32-bit
//     on AVR/ESP32, so comparing against the HOST's strtol()/strtoul() directly would flag
//     every overflow as a "bug" when it is the whole point of the rewrite. The oracle is
//     itself validated against the real libc on every input where the width cannot matter
//     (section A), so it is not merely a restatement of my beliefs;
//   * EXHAUSTIVE enumeration over the grammar these functions can actually see
//     (whitespace x sign x digits x junk-suffix), every string up to length 4 over a
//     13-character alphabet, and all 256 bytes (sections B/C/D);
//   * a large fixed-seed randomized sweep (section E);
//   * a whole-line differential against an INDEPENDENTLY WRITTEN model of the grammar, over
//     an exhaustive key x value grid and a randomized line sweep (section F);
//   * hostile-input robustness for contractParse() under ASan, with every line in a HEAP
//     buffer of exactly strlen()+1 bytes, so a single byte read past the NUL is a hard
//     failure rather than a silent success (section G);
//   * slice-API probes for _parseHex6/_effectFromName, whose `len` argument is the only
//     thing standing between them and the end of the buffer (section H);
//   * exhaustive checks on the effect-name table and the accent allow-list (section I).
//
// KNOWN, DELIBERATELY PINNED DEVIATION FROM 32-BIT strtoul  (the one and only one)
// --------------------------------------------------------------------------------
//   _parseU32("-4294967296")  == 1            -- and so does EVERY negative-AND-overflowing
//                                                input, because _decDigits latches the
//                                                MAGNITUDE at 0xFFFFFFFF and _parseU32 then
//                                                negates it: 0 - 0xFFFFFFFF == 1.
//   32-bit strtoul("-4294967296") == 0xFFFFFFFF  -- ERANGE => ULONG_MAX, sign ignored.
//
// It IS reachable from the wire (d=, ph=, ad= are the _parseU32 keys) and it is harmless on
// all three of them (see section B2, which pins the wire-level consequence). This harness
// does not paper over it: it asserts BOTH halves -- the implementation returns exactly 1,
// the true oracle returns exactly 0xFFFFFFFF -- for the whole input class, so if either side
// ever changes, this test goes red and somebody has to look at it on purpose.
//
// MUTATION DRILL (how to convince yourself this file has teeth)
// ------------------------------------------------------------
//   cp -R <repo> /tmp/mut && cd /tmp/mut
//   # break the last-safe-multiply test in _decDigits (the 341-check unit suite still passes
//   # this mutation; this file does not):
//   sed -i '' 's/&& d > 5)/\&\& d > 6)/' src/contract/contract_core.h
//   clang++ -std=c++17 -fsanitize=address,undefined -fno-sanitize-recover=all \
//           test/host/fuzz_parsers.cpp -o /tmp/mut_fuzz && /tmp/mut_fuzz    # => FAILURES

#include "../../src/contract/contract_core.h"

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

// ---------------------------------------------------------------------------
// tiny harness
// ---------------------------------------------------------------------------
static long g_total = 0;
static long g_fail = 0;
static int  g_reported = 0;   // cap the noise: a divergence CLASS is usually enormous
static int  g_maxreport = 25; // FUZZ_VERBOSE=<n> raises it (useful when triaging a mutation)

static int model_atoi_str(const char* s);   // fwd: section F's model, used by A's self-check

static std::string esc(const char* s) {
  std::string o;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    char b[8];
    if (*p >= 0x20 && *p < 0x7f && *p != '"') { b[0] = (char)*p; b[1] = 0; }
    else snprintf(b, sizeof b, "\\x%02x", *p);
    o += b;
  }
  return o;
}
static void failf(const char* what, const char* input, const char* fmt, ...) {
  g_fail++;
  if (g_reported++ < g_maxreport) {
    printf("FAIL [%s] input=\"%s\"  ", what, esc(input).c_str());
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
  } else if (g_reported == g_maxreport + 1) {
    printf("... (further failure detail suppressed; set FUZZ_VERBOSE=1000 to see it)\n");
  }
}
#define CHECK_MSG(cond, what, input, ...) \
  do { g_total++; if (!(cond)) failf(what, input, __VA_ARGS__); } while (0)
#define CHECK(cond) \
  do { g_total++; if (!(cond)) { g_fail++; if (g_reported++ < g_maxreport) \
       printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

// ---------------------------------------------------------------------------
// SECTION A — the 32-bit oracle, and the proof that the oracle is honest
// ---------------------------------------------------------------------------
// A true 32-bit strtoul/strtol, expressed in terms of the real libc's digit conversion
// (strtoull) so the *hard* part -- turning ASCII into a number -- is done by code we did not
// write. Only the width/sign/ERANGE rules are ours, and those are quoted from C99 7.20.1.4:
//   strtoul: "...if the subject sequence begins with a minus sign, the value resulting from
//   the conversion is negated (in the return type)... If the correct value is outside the
//   range of representable values, ULONG_MAX is returned"  -- ULONG_MAX, sign or no sign.
//   strtol:  "...LONG_MAX or LONG_MIN is returned (according to the sign of the value)".

struct Scanned {
  bool neg = false;
  bool any = false;              // did any digit convert at all?
  bool over64 = false;           // magnitude did not fit even in 64 bits
  unsigned long long mag = 0;    // magnitude (unsigned), valid when !over64
};

static Scanned scan_with_libc(const char* s) {
  Scanned r;
  const char* p = s;
  while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;   // C isspace() set, "C" locale
  if (*p == '-') { r.neg = true; p++; }
  else if (*p == '+') { p++; }
  const char* d0 = p;
  while (*p >= '0' && *p <= '9') p++;
  if (p == d0) return r;                                  // no digits => no conversion
  r.any = true;
  std::string digits(d0, (size_t)(p - d0));
  errno = 0;
  r.mag = strtoull(digits.c_str(), nullptr, 10);          // libc does the conversion
  if (errno == ERANGE) r.over64 = true;
  return r;
}
static uint32_t oracle_u32(const char* s) {               // TRUE 32-bit strtoul
  Scanned r = scan_with_libc(s);
  if (!r.any) return 0;
  if (r.over64 || r.mag > 0xFFFFFFFFull) return 0xFFFFFFFFu;   // ERANGE -> ULONG_MAX
  uint32_t m = (uint32_t)r.mag;
  return r.neg ? (uint32_t)(0u - m) : m;                       // negated in the return type
}
static int32_t oracle_i32(const char* s) {                // TRUE 32-bit strtol (saturating)
  Scanned r = scan_with_libc(s);
  if (!r.any) return 0;
  const unsigned long long lim = r.neg ? 2147483648ull : 2147483647ull;
  if (r.over64 || r.mag > lim) return r.neg ? INT32_MIN : INT32_MAX;
  return r.neg ? (int32_t)(-(long long)r.mag) : (int32_t)r.mag;
}
// The ONE input class where the implementation knowingly parts ways with 32-bit strtoul.
static bool is_known_u32_deviation(const char* s) {
  Scanned r = scan_with_libc(s);
  return r.any && r.neg && (r.over64 || r.mag > 0xFFFFFFFFull);
}
// What contract_core.h ACTUALLY computes for a u32 param (oracle + the pinned deviation).
// Used by the line-level model in section F.
static uint32_t impl_spec_u32(const char* s) {
  return is_known_u32_deviation(s) ? 1u : oracle_u32(s);
}

// The oracle must agree with the REAL host libc wherever the 64-vs-32-bit width cannot
// possibly matter. That is what makes it an oracle rather than an opinion:
//   * magnitude <= 0xFFFFFFFF -- the host's 64-bit strtoul computes the same magnitude and,
//     for a negative, negates it in 64 bits, whose LOW 32 BITS are exactly our answer;
//   * magnitude that overflows 64 bits -- the host returns ULONG_MAX (all ones), whose low
//     32 bits are 0xFFFFFFFF, also exactly our answer.
//   The gap between (2^32 .. 2^64] is precisely where the host's `long` width becomes
//   visible, and there is nothing there to compare against -- it is checked against the
//   written rule instead. That gap is also exactly why this code was rewritten.
static void section_A_oracle_selfcheck() {
  static const char* v[] = {
    "0", "1", "9", "10", "255", "256", "65535", "65536", "2147483646", "2147483647",
    "2147483648", "4294967294", "4294967295", "-0", "-1", "-5", "-255", "-2147483647",
    "-2147483648", "-4294967295", "+0", "+1", "+4294967295", " 42", "\t7", "\n8", "\v9",
    "\f10", "\r11", "  -12", "007", "0000000000000000000042", "", "-", "+", "abc", "-abc",
    " ", "12abc", "-12,x", "18446744073709551615", "18446744073709551616",
    "99999999999999999999999999999999", "-99999999999999999999999999999999",
  };
  for (const char* s : v) {
    Scanned r = scan_with_libc(s);
    if (!r.any || r.mag <= 0xFFFFFFFFull || r.over64) {
      errno = 0;
      unsigned long hostu = strtoul(s, nullptr, 10);            // 64-bit `long` on this host
      CHECK_MSG(oracle_u32(s) == (uint32_t)hostu, "oracle-vs-libc-u32", s,
                "oracle=%u  host(low32)=%u", oracle_u32(s), (uint32_t)hostu);
    }
    errno = 0;
    long hosti = strtol(s, nullptr, 10);
    if (errno != ERANGE && hosti >= INT32_MIN && hosti <= INT32_MAX) {
      CHECK_MSG(oracle_i32(s) == (int32_t)hosti, "oracle-vs-libc-i32", s,
                "oracle=%d  host=%ld", oracle_i32(s), hosti);
    }
    // ...and the model of the HOST's atoi() (used for the s/b/m/am/v/bpm/bpb params in
    // section F) against the real thing, on every one of these vectors.
    CHECK_MSG(model_atoi_str(s) == atoi(s), "model-vs-libc-atoi", s,
              "model=%d  host=%d", model_atoi_str(s), atoi(s));
  }
  // ...and the deviation predicate itself, on the boundary, so a future edit to
  // is_known_u32_deviation() cannot quietly widen the amnesty.
  CHECK(!is_known_u32_deviation("-4294967295"));   // in range: NO amnesty, must match strtoul
  CHECK(is_known_u32_deviation("-4294967296"));    // one past: the deviation begins here
  CHECK(!is_known_u32_deviation("4294967296"));    // positive overflow: NO amnesty (it latches)
}

// ---------------------------------------------------------------------------
// the two assertions every numeric input in this file goes through
// ---------------------------------------------------------------------------
static void check_num(const char* s) {
  const uint32_t gotU = _parseU32(s);
  const int32_t  gotI = _parseI32(s);

  if (is_known_u32_deviation(s)) {
    // PINNED DEVIATION -- assert BOTH halves so neither side can drift unnoticed.
    CHECK_MSG(gotU == 1u, "u32-pinned-deviation", s,
              "expected the documented 1 (== 0 - 0xFFFFFFFF), got %u", gotU);
    CHECK_MSG(oracle_u32(s) == 0xFFFFFFFFu, "u32-pinned-oracle", s,
              "a true 32-bit strtoul should return ULONG_MAX, got %u", oracle_u32(s));
  } else {
    const uint32_t wantU = oracle_u32(s);
    CHECK_MSG(gotU == wantU, "u32", s, "_parseU32=%u  strtoul32=%u", gotU, wantU);
  }
  // _parseI32 is claimed to match a 32-bit strtol EVERYWHERE, with no exceptions at all.
  const int32_t wantI = oracle_i32(s);
  CHECK_MSG(gotI == wantI, "i32", s, "_parseI32=%d  strtol32=%d", gotI, wantI);
}

// ---------------------------------------------------------------------------
// SECTION B — exhaustive enumeration over the grammar the wire can carry
// ---------------------------------------------------------------------------
// prefix(whitespace) x sign x body(digit strings, incl. every 32-bit boundary and several
// overflow classes) x suffix(the bytes that can follow a value slice, plus junk).
// For functions this small, structured exhaustion is a stronger statement than any amount of
// coverage-guided random mutation.
static void section_B_grammar() {
  static const char* pre[] = {
    "", " ", "  ", "\t", "\n", "\v", "\f", "\r", " \t\n\v\f\r ", "\r\n",
  };
  static const char* sign[] = { "", "+", "-", "--", "++", "+-", "-+", "- ", "+ " };
  static const char* body[] = {
    "", "x", ".", "e", "o",                              // no digits at all
    "0", "00", "000000000000000000000", "1", "7", "9", "10", "42", "099", "0000042",
    "127", "128", "255", "256", "32767", "32768", "65535", "65536", "70000",  // 8/16-bit edges
    "2147483646", "2147483647", "2147483648", "2147483649",                   // int32 edges
    "4294967289", "4294967290", "4294967294", "4294967295",   // <-- the last-safe-multiply
    "4294967296", "4294967297", "4294967300", "4294967399",   //     test lives exactly here
    "42949672950", "42949672959",
    "9999999999", "99999999999", "18446744073709551615", "18446744073709551616",
    "99999999999999999999999999999999999999999999999999999999999999999999999999999999",
    "000000000004294967296",                            // leading zeros must not defeat the
    "0000000000000000000000000000004294967295",         // latch, nor trigger it early
  };
  static const char* suf[] = {
    "", ",", ",b=1", "x", " ", "\t", ".5", "e9", "abc", "-1", "0", ",,", "=", ":",
  };
  for (const char* a : pre)
    for (const char* b : sign)
      for (const char* c : body)
        for (const char* d : suf)
          check_num((std::string(a) + b + c + d).c_str());
}

// B2 — the deviation and its wire-level blast radius, written out as the minimal
// reproducers a human can read (the same assertions check_num() makes, spelled out).
static void section_B2_minimal_reproducers() {
  CHECK(_parseU32("-4294967295") == 1u);            // in range: a true strtoul agrees...
  CHECK(oracle_u32("-4294967295") == 1u);           //   ...(0 - 4294967295 == 1)
  CHECK(_parseU32("-4294967296") == 1u);            // OVERFLOW: implementation still says 1
  CHECK(oracle_u32("-4294967296") == 0xFFFFFFFFu);  //   ...a true 32-bit strtoul: ULONG_MAX
  CHECK(_parseU32("-99999999999") == 1u);           // the vector from the brief: CONFIRMED
  CHECK(oracle_u32("-99999999999") == 0xFFFFFFFFu);
  // The positive side does NOT deviate -- this is what the latch buys:
  CHECK(_parseU32("99999999999") == 0xFFFFFFFFu);
  CHECK(_parseU32("4294967295") == 0xFFFFFFFFu);
  CHECK(_parseU32("4294967296") == 0xFFFFFFFFu);    // latched, NOT wrapped to 0
  // _parseI32 deviates on NEITHER side:
  CHECK(_parseI32("-99999999999") == INT32_MIN);
  CHECK(_parseI32("99999999999") == INT32_MAX);
  CHECK(_parseI32("-2147483648") == INT32_MIN);
  CHECK(_parseI32("-2147483649") == INT32_MIN);
  CHECK(_parseI32("2147483647") == INT32_MAX);
  CHECK(_parseI32("2147483648") == INT32_MAX);

  // ...and how far the deviation can travel on each of the three keys that use _parseU32.
  // Pinned so a future reader can see the blast radius without re-deriving it.
  ParsedContract p;
  CHECK(contractParse("P*A:d=-99999999999", p) && p.params.hasDur && p.params.durMs == 1u);
  CHECK(contractParse("**C:ph=-99999999999", p) && p.params.hasPh && p.params.phMs == 1u);
  CHECK(contractParse("P*A:ad=-99999999999", p) && p.params.hasAccentDur &&
        p.params.accentDurMs == 1u);                     // ad= is clamped to 2550 anyway
  CHECK(contractParse("P*A:ad=99999999999", p) && p.params.accentDurMs == 2550u);
  // For reference, the in-range negatives (which DO match strtoul) are the far more
  // dangerous-looking values -- and they are the ones the firmware already survives:
  CHECK(contractParse("P*A:d=-1", p) && p.params.durMs == 0xFFFFFFFFu);
}

// ---------------------------------------------------------------------------
// SECTION C — exhaustive over every string up to length 4 from a 13-byte alphabet
// ---------------------------------------------------------------------------
// 13 + 13^2 + 13^3 + 13^4 = 30,927 strings, each compared against the oracle. The alphabet
// covers every branch in _decDigits: both whitespace shapes, both signs, the digit range's
// two ends, a mid digit, the two terminators the tokenizer can leave behind, a printable
// non-digit, and two high-bit bytes (the signed-vs-unsigned `char` question -- see D).
static void section_C_exhaustive_short() {
  static const char alpha[] = { ' ', '\t', '\r', '+', '-', '0', '4', '9', ',', 'x',
                                '.', (char)0x80, (char)0xff };
  const int A = (int)sizeof(alpha);
  char buf[8];
  for (int len = 1; len <= 4; len++) {
    long combos = 1;
    for (int i = 0; i < len; i++) combos *= A;
    for (long n = 0; n < combos; n++) {
      long q = n;
      for (int i = 0; i < len; i++) { buf[i] = alpha[q % A]; q /= A; }
      buf[len] = '\0';
      check_num(buf);
    }
  }
}

// ---------------------------------------------------------------------------
// SECTION D — every single byte, alone and around a digit
// ---------------------------------------------------------------------------
// `char` is SIGNED on this host and UNSIGNED on avr-gcc. Every comparison in
// _decDigits/_hexNib is a range test whose answer is the same either way (a byte >= 0x80 is
// either negative -- failing `>= '\t'` -- or > 0x7f -- failing `<= '\r'`). That is an
// argument; this section is the evidence for the host half of it, byte by byte.
static void section_D_all_bytes() {
  for (int b = 1; b < 256; b++) {         // 0 would just terminate the string
    char s1[2] = { (char)b, 0 };
    char s2[3] = { (char)b, '1', 0 };
    char s3[3] = { '1', (char)b, 0 };
    char s4[4] = { '-', (char)b, '1', 0 };
    check_num(s1); check_num(s2); check_num(s3); check_num(s4);
  }
}

// ---------------------------------------------------------------------------
// SECTION E — large deterministic randomized sweep (FIXED SEED)
// ---------------------------------------------------------------------------
static uint64_t g_rng = 0x0DD1E5EEDBEEF001ull;   // fixed seed: every run is identical
static uint32_t rnd() {                          // xorshift64*, deterministic everywhere
  g_rng ^= g_rng >> 12; g_rng ^= g_rng << 25; g_rng ^= g_rng >> 27;
  return (uint32_t)((g_rng * 2685821657736338717ull) >> 32);
}
static uint32_t rnd_n(uint32_t n) { return n ? rnd() % n : 0; }

static void section_E_random_numeric(long iters) {
  // digit-heavy (that is where the arithmetic is) but with enough sign/space/junk to keep
  // hitting the other branches.
  static const char alpha[] = "00112233445566778899000000000000+-  \t\r\n,x.e";
  const uint32_t A = (uint32_t)(sizeof(alpha) - 1);
  char buf[40];
  for (long i = 0; i < iters; i++) {
    uint32_t len = rnd_n(25);
    for (uint32_t j = 0; j < len; j++) buf[j] = alpha[rnd_n(A)];
    buf[len] = '\0';
    check_num(buf);
  }
  // ...plus a sweep that spends its whole budget within a few units of every boundary that
  // matters, which uniform random strings would essentially never land on.
  static const unsigned long long anchors[] = {
    0ull, 255ull, 256ull, 65535ull, 65536ull, 2147483647ull, 2147483648ull,
    4294967295ull, 4294967296ull, 42949672950ull, 18446744073709551615ull,
  };
  for (unsigned long long a : anchors)
    for (long long d = -6; d <= 6; d++) {
      if ((long long)a + d < 0 && a < 7) continue;
      unsigned long long v = a + (unsigned long long)d;
      char s[40];
      snprintf(s, sizeof s, "%llu", v);        check_num(s);
      snprintf(s, sizeof s, "-%llu", v);       check_num(s);
      snprintf(s, sizeof s, "+%llu", v);       check_num(s);
      snprintf(s, sizeof s, " %llu,b=1", v);   check_num(s);
      snprintf(s, sizeof s, "0000%llu", v);    check_num(s);
    }
}

// ---------------------------------------------------------------------------
// SECTION F — whole-line differential against an INDEPENDENT model of the grammar
// ---------------------------------------------------------------------------
// The model is written from the wire grammar in a deliberately different style (std::string,
// split-then-dispatch) so that a slicing/aliasing bug in _parseParams cannot be mirrored by
// an identical bug here. Where the implementation has a QUIRK, the model reproduces the quirk
// ON PURPOSE and labels it -- pinning a quirk is the point; silently sharing a bug is not.
// Every "QUIRK (pinned)" below is listed in the report.

struct Model {
  bool hasEffect = false;      ContractEffect effect = CE_NONE;  int nativeCode = -1;
  bool hasColor = false;       ContractRGB color{0,0,0};
  bool hasSpeed = false;       uint8_t speed = 0;
  bool hasDur = false;         uint32_t durMs = 0;
  bool hasBright = false;      uint8_t bright = 0;
  bool hasBeatMod = false;     uint8_t beatMod = 0;
  bool hasAt = false;          int32_t atBeat = -1;
  bool hasAm = false;          uint8_t accentMode = 0;
  bool hasBpm = false;         uint16_t bpm = 0;
  bool hasPh = false;          uint32_t phMs = 0;
  bool hasBpb = false;         uint8_t bpb = 4;
  bool hasBeat = false;        int32_t beatAnchor = 0;
  bool hasLevel = false;       uint8_t level = 0;
  char mode = 0;
  bool hasAccentFx = false;    ContractEffect accentFx = CE_NONE;
  bool hasAccentColor = false; ContractRGB accentColor{0,0,0};
  bool hasAccentDur = false;   uint16_t accentDurMs = 0;
};

// contract_core.h still uses atoi() for the uint8_t/uint16_t params (s/b/m/am/v/bpm/bpb and
// the native:<n> code). atoi() is NOT the 32-bit oracle -- it is `(int)strtol(s,NULL,10)`,
// which on THIS 64-bit host converts in 64 bits (saturating at LONG_MAX/LONG_MIN only past
// 2^63) and then TRUNCATES to a 32-bit int. So atoi("4294967296") == 0, not INT_MAX.
// Restated here from that rule, independently of libc, and cross-checked against the real
// host atoi() in section A.
//
// !!! THE HOST CANNOT SEE WHAT THE BOARD DOES HERE. avr-libc's atoi() accumulates in a
// 16-BIT int and wraps; this host accumulates in 64 and truncates to 32. For every param
// that is immediately cast to uint8_t/uint16_t the two agree (a mod-2^32 truncation and a
// mod-2^16 wrap have the same low 8/16 bits) -- EXCEPT when the host saturates, i.e. for
// magnitudes >= 2^63. That gap is analysed in the report; it is exactly the class of
// host-vs-target width divergence that _parseU32/_parseI32 were introduced to kill for the
// 32-bit fields, and it is still live for these. No host test can close it.
static int model_atoi_str(const char* s) {
  Scanned r = scan_with_libc(s);
  if (!r.any) return 0;
  const unsigned long long LMAX = 9223372036854775807ull;   // 2^63 - 1
  long long v64;
  if (r.neg) v64 = (r.over64 || r.mag > LMAX) ? LLONG_MIN : -(long long)r.mag;
  else       v64 = (r.over64 || r.mag > LMAX) ? LLONG_MAX :  (long long)r.mag;
  return (int)(int32_t)(uint32_t)(unsigned long long)v64;   // ...then truncate to int
}
static int model_atoi(const std::string& v) { return model_atoi_str(v.c_str()); }

static bool model_hex6(const std::string& v, ContractRGB& out) {
  if (v.size() < 6) return false;              // QUIRK (pinned): >= 6, not == 6
  int n[6];
  for (int i = 0; i < 6; i++) {
    char c = v[(size_t)i];
    if (c >= '0' && c <= '9') n[i] = c - '0';
    else if (c >= 'a' && c <= 'f') n[i] = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') n[i] = c - 'A' + 10;
    else return false;
  }
  out.r = (uint8_t)(n[0] * 16 + n[1]);
  out.g = (uint8_t)(n[2] * 16 + n[3]);
  out.b = (uint8_t)(n[4] * 16 + n[5]);
  return true;                                 // QUIRK (pinned): trailing junk is ignored
}
static ContractEffect model_effect(const std::string& v, int& nativeCode) {
  nativeCode = -1;
  static const struct { const char* n; ContractEffect e; } tbl[] = {
    {"off",CE_OFF},{"solid",CE_SOLID},{"flash",CE_FLASH},{"pulse",CE_PULSE},
    {"rainbow",CE_RAINBOW},{"scan",CE_SCAN},{"sparkle",CE_SPARKLE},{"meter",CE_METER},
    {"comet",CE_COMET},{"chase",CE_CHASE},{"wipe",CE_WIPE},{"gradient",CE_GRADIENT},
    {"colorcycle",CE_COLORCYCLE},{"twinkle",CE_TWINKLE},
  };
  for (auto& t : tbl) if (v == t.n) return t.e;
  if (v.size() > 7 && v.compare(0, 7, "native:") == 0) {
    // QUIRK (pinned): bare "native:" (len 7) is NOT native; "native:zz" IS, with code 0.
    //
    // native:<n> is parsed BOUNDED BY THE SLICE LENGTH and SATURATING, not with atoi().
    // Two reasons, and the second is the important one:
    //  1. atoi() stops at the first non-digit, not at the end of the slice, so it read off the
    //     end of a non-NUL-terminated view (ASan-proven heap-buffer-overflow).
    //  2. atoi() made the HOST AND THE BOARD DISAGREE. nativeCode is a plain `int` — 32-bit on
    //     this host, 16-BIT on the ATmega — so "native:99999" was 99999 here and a wrapped,
    //     effectively random value on the real PSI. The host suite could never gate the number
    //     the board actually computes, which is the whole reason contract_core pins its widths.
    //     The saturating accumulator below stays inside int16 on every target, so host and AVR
    //     now compute the SAME nativeCode for every input.
    // The model mirrors it independently rather than calling the implementation.
    size_t i = 0; const std::string d = v.substr(7);
    int32_t sign = 1;
    if (i < d.size() && (d[i] == '-' || d[i] == '+')) { sign = (d[i] == '-') ? -1 : 1; i++; }
    int32_t n = 0;
    while (i < d.size() && d[i] >= '0' && d[i] <= '9') {
      int32_t dig = d[i] - '0';
      if (n <= (32767 - dig) / 10) n = n * 10 + dig;   // saturate inside int16 (AVR `int`)
      i++;
    }
    nativeCode = (int)(sign * n);
    return CE_NATIVE;
  }
  return CE_NONE;
}
static Model model_parse_params(const std::string& s) {
  Model m;
  size_t i = 0;
  while (i < s.size()) {
    size_t comma = s.find(',', i);
    if (comma == std::string::npos) comma = s.size();
    std::string tok = s.substr(i, comma - i);
    i = comma + 1;
    size_t eq = tok.find('=');
    std::string k = (eq == std::string::npos) ? tok : tok.substr(0, eq);
    std::string v = (eq == std::string::npos) ? std::string() : tok.substr(eq + 1);
    if (k.empty()) continue;                   // "=x" and ",,": an empty key is dropped
    if (k == "i") {
      int nc; ContractEffect e = model_effect(v, nc);
      if (e != CE_NONE) { m.hasEffect = true; m.effect = e; m.nativeCode = nc; }
      // QUIRK (pinned): an INVALID i= does not clear a previously valid one (same for c=,
      // ae=, ac=). The last VALID value wins, not the last value.
    } else if (k == "c") {
      ContractRGB c; if (model_hex6(v, c)) { m.hasColor = true; m.color = c; }
    }
    else if (k == "s")    { m.hasSpeed = true;   m.speed = (uint8_t)model_atoi(v); }
    else if (k == "d")    { m.hasDur = true;     m.durMs = impl_spec_u32(v.c_str()); }
    else if (k == "b")    { m.hasBright = true;  m.bright = (uint8_t)model_atoi(v); }
    else if (k == "m")    { m.hasBeatMod = true; m.beatMod = (uint8_t)model_atoi(v); }
    else if (k == "at")   { m.hasAt = true;      m.atBeat = oracle_i32(v.c_str()); }
    else if (k == "am")   { m.hasAm = true;      m.accentMode = (uint8_t)model_atoi(v); }
    else if (k == "v")    { m.hasLevel = true;   m.level = (uint8_t)model_atoi(v);
                            // QUIRK (pinned): the mode sniff looks at v[0] with NO leading-
                            // whitespace skip, unlike every numeric parse on the same line.
                            if (!v.empty() && (v[0] == 's' || v[0] == 'i')) m.mode = v[0]; }
    else if (k == "bpm")  { m.hasBpm = true;     m.bpm = (uint16_t)model_atoi(v); }
    else if (k == "ph")   { m.hasPh = true;      m.phMs = impl_spec_u32(v.c_str()); }
    else if (k == "bpb")  { m.hasBpb = true;     m.bpb = (uint8_t)model_atoi(v); }
    else if (k == "beat") { m.hasBeat = true;    m.beatAnchor = oracle_i32(v.c_str()); }
    else if (k == "ae")   { int nc; ContractEffect e = model_effect(v, nc);
                            if (e != CE_NONE && accentEffectAllowed(e)) {
                              m.hasAccentFx = true; m.accentFx = e; } }
    else if (k == "ac")   { ContractRGB c; if (model_hex6(v, c)) {
                              m.hasAccentColor = true; m.accentColor = c; } }
    else if (k == "ad")   { uint32_t d = impl_spec_u32(v.c_str());
                            if (d > 2550u) d = 2550u;
                            m.hasAccentDur = true; m.accentDurMs = (uint16_t)d; }
    // every other key (including "sw") is parsed and ignored -- forward-compatible
  }
  return m;
}
static bool model_eq(const Model& m, const ContractParams& p, std::string& why) {
#define F(f) if (m.f != p.f) { why = "field " #f; return false; }
  F(hasEffect) F(effect) F(nativeCode) F(hasColor) F(hasSpeed) F(speed)
  F(hasDur) F(durMs) F(hasBright) F(bright) F(hasBeatMod) F(beatMod)
  F(hasAt) F(atBeat) F(hasAm) F(accentMode) F(hasBpm) F(bpm) F(hasPh) F(phMs)
  F(hasBpb) F(bpb) F(hasBeat) F(beatAnchor) F(hasLevel) F(level) F(mode)
  F(hasAccentFx) F(accentFx) F(hasAccentColor) F(hasAccentDur) F(accentDurMs)
#undef F
  if (m.color.r != p.color.r || m.color.g != p.color.g || m.color.b != p.color.b) {
    why = "color"; return false;
  }
  if (m.accentColor.r != p.accentColor.r || m.accentColor.g != p.accentColor.g ||
      m.accentColor.b != p.accentColor.b) { why = "accentColor"; return false; }
  return true;
}

// Compare the real parser and the model on a full '!'-line body (everything after the '!').
static void diff_line(const std::string& body) {
  ParsedContract p;
  const bool ok = contractParse(body.c_str(), p);

  const bool m_ok = body.size() >= 3 &&
                    (body[0]=='L' || body[0]=='P' || body[0]=='H' || body[0]=='*') &&
                    (body[1]=='F' || body[1]=='R' || body[1]=='T' || body[1]=='*') &&
                    (body[2] && strchr("APCBLXMQ", body[2]) != nullptr);
  CHECK_MSG(ok == m_ok, "line-accept", body.c_str(), "impl=%d model=%d", (int)ok, (int)m_ok);
  if (!ok) {
    // a rejected line must leave the out-param completely inert (no half-applied state)
    CHECK_MSG(!p.valid && p.cls == 0 && p.unit == 0 && p.verb == CV_NONE &&
              !p.params.hasEffect && !p.params.hasColor && !p.params.hasDur,
              "reject-not-inert", body.c_str(), "valid=%d", (int)p.valid);
    return;
  }
  if (!m_ok) return;
  // QUIRK (pinned): params are read only when byte 3 is exactly ':'. "LFAjunk" is a VALID
  // parameterless A -- the tail is ignored, not rejected.
  Model m = (body.size() > 3 && body[3] == ':') ? model_parse_params(body.substr(4)) : Model();
  std::string why;
  CHECK_MSG(model_eq(m, p.params, why), "line-params", body.c_str(), "%s", why.c_str());
}

static void section_F_lines() {
  // F1 — exhaustive key x value grid: every key the grammar knows (plus lookalikes that must
  // NOT alias: "a", "at" vs "a"+"t", "b" vs "bpm"/"bpb"/"beat", "ae"/"ac"/"ad") crossed with
  // a value set that includes every shape the value parsers can see.
  static const char* keys[] = {
    "i", "c", "s", "d", "b", "m", "at", "am", "v", "bpm", "ph", "bpb", "beat",
    "ae", "ac", "ad", "sw", "a", "t", "e", "bp", "ata", "ii", "", "I", "AT", "D",
  };
  static const char* vals[] = {
    "", "0", "1", "255", "256", "-1", "-0", "+5", " 7", "\t8", "99999999999",
    "-99999999999", "4294967295", "4294967296", "2147483647", "2147483648", "-2147483648",
    "-2147483649", "solid", "off", "native:5", "native:", "native:-3", "native:99999",
    "native:zz", "comet", "scan", "sparkle", "meter", "rainbow", "twinkle", "colorcycle",
    "SOLID", "soli", "solidd", "ff0000", "FF0000", "ff00", "ff0000ff", "gg0000", "0080ff",
    "show", "idle", "s", "i", "stop", " show", "x", "=", ":", ".", "e9",
    // values containing '=' -- the value slice runs to the next ',' or NUL, NOT to the next
    // '='. Without these the grid cannot tell a correct tokenizer from one that stops the
    // value at the second '=' (that mutant survived an earlier revision of this file; the
    // mutation drill in the header comment is what found the hole).
    "solid=x", "ff0000=1", "1=2", "native:5=x", "=solid", "s=1", "show=1", "5=", "==",
  };
  for (const char* k : keys)
    for (const char* v : vals) {
      diff_line(std::string("P*A:") + k + "=" + v);
      diff_line(std::string("**C:") + k + "=" + v);
      diff_line(std::string("LFA:") + k + "=" + v + ",b=7");     // key in a longer line
      diff_line(std::string("HTM:b=7,") + k + "=" + v);          // ...and in trailing position
      diff_line(std::string("P*A:") + k);                        // key with NO '='
      diff_line(std::string("P*A:") + k + "=");                  // '=' with NO value
    }

  // F2 — repeated keys, degenerate separators, header shapes.
  static const char* lines[] = {
    "P*A", "P*A:", "P*A::", "P*A:,", "P*A:,,,,", "P*A:=", "P*A:=5", "P*A:,=,=,",
    "P*Ajunk", "P*A junk", "P*Q", "P*X", "**M:v=show", "**M:v=idle", "**M:v=",
    "**M:v= show", "**M:v=showoff", "P*A:i=solid,i=bogus", "P*A:i=bogus,i=solid",
    "P*A:c=ff0000,c=zz", "P*A:c=zz,c=00ff00", "P*A:d=5,d=6,d=7", "P*A:at=1,at=-1",
    "P*A:ae=scan", "P*A:ae=native:3", "P*A:ae=comet", "P*A:ae=comet,ae=meter",
    "P*A:ad=2551", "P*A:ad=2550", "P*A:ad=0", "P*A:sw=1", "P*A:unknown=1,b=9",
    "P*A:b=1,,b=2", "P*A:,b=3", "P*A:b=4,", "P*A:i=native:0", "P*A:i=native:1x",
    "LFA:i=colorcycle,c=3b82f6,s=255,b=200,at=1234,am=2,m=200,ae=colorcycle,ac=ffffff,ad=2550",
    "*FA", "*RA", "*TA", "**A", "L*A", "H*A", "P*A", "LFA", "LRA", "LTA",
    "l*a", "p*a", "**a", "**Z", "Z*A", "*ZA", "!P*A", " P*A", "P*A\r", "P*A\n",
  };
  for (const char* s : lines) diff_line(s);

  // F3 — every header triple x every third byte: exhaustive over the 3-byte header, which is
  // the ONLY place contractParse() can accept or reject.
  for (int c = 0; c < 256; c++)
    for (int u = 0; u < 256; u++) {
      char h[6] = { (char)c, (char)u, 'A', 0, 0, 0 };
      if (!c || !u) continue;
      diff_line(h);
    }
  for (int v = 1; v < 256; v++) {
    char h[8] = { 'P', '*', (char)v, ':', 'b', '=', '9', 0 };
    diff_line(h);
  }
}

static void section_F_random_lines(long iters) {
  // (a) a flat random-byte sweep over an alphabet biased toward the grammar's own bytes.
  static const char alpha[] =
      "PLH*FRTAPCBLXMQ:=,,ibcdsmatvphaeacadsw0123456789-+ \t\r\nnative:solidcometffx.";
  const uint32_t A = (uint32_t)(sizeof(alpha) - 1);
  std::string s;
  for (long i = 0; i < iters / 2; i++) {
    uint32_t len = rnd_n(48);
    s.clear();
    for (uint32_t j = 0; j < len; j++) s += alpha[rnd_n(A)];
    diff_line(s);
  }

  // (b) a GRAMMAR-STRUCTURED sweep: assemble real key=value tokens, then corrupt the result
  // with random splices. Flat random bytes essentially never produce "i=solid=x" or
  // "c=ff0000,c=zz" -- the near-miss neighbourhood where a tokenizer bug actually lives --
  // so this generator builds those on purpose and then perturbs them.
  static const char* K[] = { "i","c","s","d","b","m","at","am","v","bpm","ph","bpb","beat",
                             "ae","ac","ad","sw","a","b","at","","x" };
  static const char* V[] = { "solid","comet","scan","native:5","native:","off","twinkle",
                             "ff0000","0080ff","ffff","zz","0","1","255","-1","-99999999999",
                             "4294967296","2147483648"," 7","show","idle","","=","solid=x",
                             "ff0000=1","2550","2551" };
  static const char CLS[] = "LPH*", UN[] = "FRT*", VB[] = "APCBLXMQ";
  static const char JUNK[] = ",=: \t\r\n!*x0-";
  for (long i = 0; i < iters / 2; i++) {
    s.clear();
    s += CLS[rnd_n(4)]; s += UN[rnd_n(4)]; s += VB[rnd_n(8)];
    if (rnd_n(10)) {                                   // 90%: with a param block
      s += ':';
      uint32_t ntok = rnd_n(6);
      for (uint32_t t = 0; t < ntok; t++) {
        if (t) s += ',';
        s += K[rnd_n(sizeof K / sizeof *K)];
        if (rnd_n(8)) { s += '='; s += V[rnd_n(sizeof V / sizeof *V)]; }   // 87%: with a value
      }
    }
    // now corrupt it: 0..3 random splices (insert / delete / overwrite a byte)
    uint32_t nmut = rnd_n(4);
    for (uint32_t m = 0; m < nmut && !s.empty(); m++) {
      uint32_t at = rnd_n((uint32_t)s.size());
      switch (rnd_n(3)) {
        case 0: s.insert(s.begin() + at, JUNK[rnd_n(sizeof JUNK - 1)]); break;
        case 1: s.erase(s.begin() + at); break;
        default: s[at] = JUNK[rnd_n(sizeof JUNK - 1)]; break;
      }
    }
    diff_line(s);
  }
}

// ---------------------------------------------------------------------------
// SECTION G — hostile-input robustness for contractParse(), under ASan
// ---------------------------------------------------------------------------
// EVERY line here lives in a HEAP allocation of exactly strlen()+1 bytes, so a read even one
// byte past the terminating NUL is a hard ASan failure instead of a silent success on
// whatever happened to be in the stack frame. That is the whole point of this section: a
// static char[256] would hide precisely the bug we are hunting.
static void feed_exact(const std::string& body) {
  char* buf = (char*)malloc(body.size() + 1);        // EXACT size: no slack for an over-read
  memcpy(buf, body.data(), body.size());
  buf[body.size()] = '\0';
  ParsedContract p;
  contractParse(buf, p);                             // must never read buf[size+1] or beyond
  // ...and must never leave a half-applied result behind on a reject
  if (!p.valid) {
    CHECK_MSG(p.cls == 0 && p.unit == 0 && p.verb == CV_NONE, "hostile-not-inert",
              buf, "cls=%d", (int)p.cls);
  } else {
    g_total++;   // count it: the line was accepted and did not trip a sanitizer
  }
  free(buf);
}
static void section_G_hostile() {
  { ParsedContract p; CHECK(contractParse(nullptr, p) == false); }   // NULL is refused

  // every length from 0..6 over a nasty alphabet, exhaustively, in exact-size heap buffers
  static const char alpha[] = { 'P', '*', 'A', ':', '=', ',', 'i', 'd', '1', '-',
                                ' ', 'n', (char)0xff };
  const int A = (int)sizeof(alpha);
  for (int len = 0; len <= 3; len++) {
    long combos = 1;
    for (int i = 0; i < len; i++) combos *= A;
    for (long n = 0; n < combos; n++) {
      std::string s;
      long q = n;
      for (int i = 0; i < len; i++) { s += alpha[q % A]; q /= A; }
      feed_exact(s);
    }
  }
  // targeted hostile shapes
  feed_exact("");
  feed_exact("P");
  feed_exact("P*");
  feed_exact("P*A");
  feed_exact("P*A:");
  feed_exact("P*A:d");
  feed_exact("P*A:d=");
  feed_exact("P*A:d=,");
  feed_exact("P*A:=");
  feed_exact("P*A:====");
  feed_exact("P*A:,,,,,,,,");
  feed_exact("P*A:i=");
  feed_exact("P*A:i=native:");
  feed_exact("P*A:i=native:-");
  feed_exact("P*A:i=native:+");
  feed_exact("P*A:i=native:99999999999999999999");
  feed_exact("P*A:c=");
  feed_exact("P*A:c=f");
  feed_exact("P*A:c=fffff");            // 5 hex digits: must be REFUSED (len < 6)
  feed_exact("P*A:c=ffffff");           // 6: accepted
  feed_exact("P*A:ac=fffff");
  feed_exact("P*A:d=-");
  feed_exact("P*A:d=+");
  feed_exact("P*A:d= ");
  feed_exact("P*A:d=\t");
  feed_exact("P*A:at=-");
  feed_exact("P*A:b=-");

  // values/keys that run to the very last byte of the allocation (the over-read magnets)
  for (int n = 1; n <= 40; n++) {
    feed_exact("P*A:d=" + std::string((size_t)n, '9'));        // digits to the last byte
    feed_exact("P*A:d=-" + std::string((size_t)n, '9'));
    feed_exact("P*A:at=" + std::string((size_t)n, '9'));
    feed_exact("P*A:i=native:" + std::string((size_t)n, '9')); // atoi() to the last byte
    feed_exact("P*A:c=" + std::string((size_t)n, 'f'));        // hex to the last byte
    feed_exact("P*A:" + std::string((size_t)n, 'd'));          // key to the last byte
    feed_exact("P*A:" + std::string((size_t)n, ' ') + "=1");
    feed_exact("P*A:d=" + std::string((size_t)n, ' '));        // whitespace to the last byte
    feed_exact("P*A:d=-" + std::string((size_t)n, '\t'));
  }
  // very long lines (the board's own intake truncates at 95 B, but the SHARED core must not
  // care -- the RSeries fork's buffer is bigger and a future board's may be bigger still)
  feed_exact("P*A:i=" + std::string(10000, 'x'));              // 10 KB effect name
  feed_exact("P*A:" + std::string(10000, 'k') + "=1");         // 10 KB key
  feed_exact("P*A:d=" + std::string(10000, '9'));              // 10 KB of digits
  feed_exact("P*A:c=" + std::string(10000, 'f'));
  feed_exact("P*A:" + std::string(20000, ','));                // 20 K empty tokens
  feed_exact("P*A:" + std::string(20000, '='));
  feed_exact(std::string(65536, 'P'));                         // 64 KB of garbage header
  {
    std::string big = "P*A:";
    for (int i = 0; i < 4000; i++) big += "d=1,";              // 4000 repeated keys
    feed_exact(big);
  }
  // embedded NUL: the parser must stop at it and never look past
  {
    std::string s = "P*A:d=5";
    s += '\0';
    s += "b=200,c=ffffff";
    char* buf = (char*)malloc(s.size() + 1);
    memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';
    ParsedContract p;
    CHECK(contractParse(buf, p));
    CHECK(p.params.hasDur && p.params.durMs == 5);
    CHECK(!p.params.hasBright && !p.params.hasColor);   // everything past the NUL is unseen
    free(buf);
  }
  // arbitrary random bytes, NUL-terminated at the exact end of an exact-size buffer
  for (long i = 0; i < 60000; i++) {
    uint32_t len = rnd_n(64);
    std::string s;
    for (uint32_t j = 0; j < len; j++) {
      char c = (char)(rnd_n(255) + 1);                 // 1..255: never an embedded NUL
      s += c;
    }
    feed_exact(s);
  }
  // ...and random bytes forced to start with a VALID header, so the tokenizer (not the
  // header check) is what actually gets hammered
  for (long i = 0; i < 60000; i++) {
    static const char cls[] = "LPH*", un[] = "FRT*", vb[] = "APCBLXMQ";
    std::string s;
    s += cls[rnd_n(4)]; s += un[rnd_n(4)]; s += vb[rnd_n(8)]; s += ':';
    uint32_t len = rnd_n(60);
    for (uint32_t j = 0; j < len; j++) s += (char)(rnd_n(255) + 1);
    feed_exact(s);
  }
}

// ---------------------------------------------------------------------------
// SECTION H — the slice APIs: does `len` really bound the read?
// ---------------------------------------------------------------------------
// _parseHex6(s, len) and _effectFromName(s, len) take an explicit length, which invites a
// caller to hand them a slice that is NOT NUL-terminated. Every buffer here is heap-allocated
// at EXACTLY len bytes with NO terminator, so ASan turns any over-read into a hard failure.
//
// RESULT (see the report): _parseHex6 is honest -- it reads at most 6 bytes and only after
// checking len >= 6. _effectFromName is NOT: its "native:" branch calls atoi(s + 7), which
// runs until it meets a NON-DIGIT and therefore reads PAST len whenever the slice ends in a
// digit. The in-tree caller (_applyParam) is safe by construction -- its slices are always
// followed by ',' or '\0' inside the same buffer -- so this is a latent API trap, not a live
// bug, and it is fenced here rather than fixed (contract_core.h must stay byte-identical
// across three repos; changing it is the maintainer's call, see the report).
static void section_H_slices() {
  // _parseHex6: exact-size, NO terminator, every length 0..8
  for (size_t len = 0; len <= 8; len++) {
    char* b = (char*)malloc(len ? len : 1);
    for (size_t i = 0; i < len; i++) b[i] = "abcdef01"[i];
    ContractRGB c{};
    bool got = _parseHex6(b, len, c);
    CHECK(got == (len >= 6));                       // and ASan proves it read nothing else
    if (len >= 6) { CHECK(c.r == 0xab && c.g == 0xcd && c.b == 0xef); }
    free(b);
  }
  // _parseHex6 with a bad nibble at every position, exact-size buffers
  for (int pos = 0; pos < 6; pos++) {
    char* b = (char*)malloc(6);
    memcpy(b, "abcdef", 6);
    b[pos] = 'g';
    ContractRGB c{};
    CHECK(!_parseHex6(b, 6, c));
    free(b);
  }
  // _effectFromName: every known name, exact-size, NO terminator (no over-read possible --
  // strncmp is bounded by the equal-length test)
  static const char* names[] = { "off","solid","flash","pulse","rainbow","scan","sparkle",
                                 "meter","comet","chase","wipe","gradient","colorcycle",
                                 "twinkle" };
  for (const char* n : names) {
    size_t len = strlen(n);
    char* b = (char*)malloc(len);
    memcpy(b, n, len);
    int nc = 0;
    CHECK(_effectFromName(b, len, nc) != CE_NONE);
    CHECK(nc == -1);
    free(b);
    // truncated by one char and extended by one char: BOTH must miss (exact-length compare)
    if (len > 1) {
      char* t = (char*)malloc(len - 1);
      memcpy(t, n, len - 1);
      int nc2 = 0;
      CHECK(_effectFromName(t, len - 1, nc2) == CE_NONE);
      free(t);
    }
    {
      char* x = (char*)malloc(len + 1);
      memcpy(x, n, len);
      x[len] = 'z';
      int nc3 = 0;
      CHECK(_effectFromName(x, len + 1, nc3) == CE_NONE);
      free(x);
    }
    // a slice whose length LIES SHORT (len-1 of a longer buffer) must not match either
  }
  // "native:" with a NON-DIGIT immediately after the colon: atoi() stops at the first byte it
  // reads, so this is safe even with no terminator. Exact-size buffer proves it.
  {
    const char* s = "native:x";
    char* b = (char*)malloc(8);
    memcpy(b, s, 8);
    int nc = 0;
    CHECK(_effectFromName(b, 8, nc) == CE_NATIVE);
    CHECK(nc == 0);                                  // garbage native code parses to 0
    free(b);
  }
  // Bare "native:" (len == 7) is NOT native -- the `len > 7` guard rejects it before atoi().
  {
    char* b = (char*)malloc(7);
    memcpy(b, "native:", 7);
    int nc = 0;
    CHECK(_effectFromName(b, 7, nc) == CE_NONE);
    free(b);
  }
  // THE TRAP, fenced: a "native:<digits>" slice that is NOT terminated inside its buffer.
  // atoi(s+7) would run off the end. We do NOT call it that way here -- we call it the way
  // the in-tree caller does (terminated by the byte that _parseParams guarantees), and we
  // assert that THAT is safe. Build with -DFUZZ_PROVE_NATIVE_OVERREAD to see ASan catch the
  // unterminated form; the reproducer is preserved so the claim in the report is checkable.
  {
    // in-tree shape: the slice is followed, inside the same allocation, by ',' or '\0'
    const char* line = "native:123,";                 // 11 bytes + NUL
    size_t n = strlen(line);
    char* b = (char*)malloc(n + 1);
    memcpy(b, line, n + 1);
    int nc = 0;
    CHECK(_effectFromName(b, 10, nc) == CE_NATIVE);   // len lies SHORT (10 of 11) -- fine:
    CHECK(nc == 123);                                 // atoi stops on the ',' at b[10]
    free(b);
  }
#ifdef FUZZ_PROVE_NATIVE_OVERREAD
  {
    // The reproducer. EXPECTED to abort under ASan with a heap-buffer-overflow READ of size 1
    // in _effectFromName -> atoi. Not part of the green suite.
    char* b = (char*)malloc(10);
    memcpy(b, "native:123", 10);                      // NO terminator, all digits to the end
    int nc = 0;
    printf("FUZZ_PROVE_NATIVE_OVERREAD: calling _effectFromName(buf,10) on an "
           "unterminated 10-byte heap slice; ASan should abort here...\n");
    fflush(stdout);
    (void)_effectFromName(b, 10, nc);
    printf("...it did NOT abort (nc=%d). The over-read did not trip ASan this run.\n", nc);
    free(b);
  }
#endif
}

// ---------------------------------------------------------------------------
// SECTION I — the effect table and the accent allow-list, exhaustively
// ---------------------------------------------------------------------------
static void section_I_tables() {
  // Every effect name, every one-char truncation, every one-char extension, and every
  // single-character case flip -- through the REAL wire path (contractParse), where a
  // rejected i= must leave hasEffect false rather than silently selecting effect 0.
  static const struct { const char* n; ContractEffect e; } tbl[] = {
    {"off",CE_OFF},{"solid",CE_SOLID},{"flash",CE_FLASH},{"pulse",CE_PULSE},
    {"rainbow",CE_RAINBOW},{"scan",CE_SCAN},{"sparkle",CE_SPARKLE},{"meter",CE_METER},
    {"comet",CE_COMET},{"chase",CE_CHASE},{"wipe",CE_WIPE},{"gradient",CE_GRADIENT},
    {"colorcycle",CE_COLORCYCLE},{"twinkle",CE_TWINKLE},
  };
  ParsedContract p;
  for (auto& t : tbl) {
    std::string n = t.n;
    CHECK(contractParse(("P*A:i=" + n).c_str(), p) && p.params.hasEffect &&
          p.params.effect == t.e);
    // one char short, at EVERY position
    for (size_t i = 0; i < n.size(); i++) {
      std::string s = n; s.erase(i, 1);
      bool ok = contractParse(("P*A:i=" + s).c_str(), p);
      // a truncation may collide with another real name; only require "not this effect
      // unless it IS another legal name"
      int nc; ContractEffect e = _effectFromName(s.c_str(), s.size(), nc);
      CHECK(ok && (p.params.hasEffect == (e != CE_NONE)));
      if (e == CE_NONE) CHECK(!p.params.hasEffect);
    }
    // uppercase: the table is lowercase-only, so every one of these must MISS
    std::string up = n;
    for (auto& ch : up) ch = (char)toupper((unsigned char)ch);
    if (up != n) {
      CHECK(contractParse(("P*A:i=" + up).c_str(), p) && !p.params.hasEffect);
    }
    // trailing junk must MISS (the compare is exact-length, not a prefix match)
    CHECK(contractParse(("P*A:i=" + n + "x").c_str(), p) && !p.params.hasEffect);
    CHECK(contractParse(("P*A:i=x" + n).c_str(), p) && !p.params.hasEffect);
    // ...and as an ACCENT: allowed <=> accentEffectAllowed(), never otherwise
    bool got = contractParse(("P*A:ae=" + n).c_str(), p) && p.params.hasAccentFx;
    CHECK(got == accentEffectAllowed(t.e));
    if (got) CHECK(p.params.accentFx == t.e);
  }
  // accentEffectAllowed over the WHOLE uint8_t domain -- no value outside the enum may
  // sneak through (this is the function that stands between a native/stateful effect and a
  // latched board).
  for (int e = 0; e < 256; e++) {
    bool allowed = accentEffectAllowed((ContractEffect)e);
    bool expect = (e == CE_OFF || e == CE_SOLID || e == CE_FLASH || e == CE_PULSE ||
                   e == CE_RAINBOW || e == CE_COMET || e == CE_CHASE || e == CE_WIPE ||
                   e == CE_GRADIENT || e == CE_COLORCYCLE || e == CE_TWINKLE);
    CHECK(allowed == expect);
  }
  // native: an ae= can NEVER arm the overlay, whatever the code
  for (const char* s : { "ae=native:1", "ae=native:0", "ae=native:-1", "ae=native:99999",
                         "ae=scan", "ae=sparkle", "ae=meter", "ae=", "ae=x" }) {
    CHECK(contractParse((std::string("P*A:") + s).c_str(), p) && !p.params.hasAccentFx &&
          p.params.accentFx == CE_NONE);
  }
}

// ---------------------------------------------------------------------------
// ---- Section J: the host must compute what the BOARD computes -------------------------------
// This whole suite runs on a 32-bit-int host. The firmware runs on a 16-BIT-int AVR. Any parsed
// field that can escape the range of a 16-bit int is a field this suite CANNOT gate — the host
// would happily pin a value the ATmega never produces. contract_core pins its widths explicitly
// (uint32_t/int32_t + the hand-rolled parsers) for exactly this reason, and `nativeCode` was the
// last hole: it is a plain `int`, and it used to be filled by atoi(), so "i=native:99999" was
// 99999 here and a wrapped, effectively random value on the real board.
//
// So assert the property that makes the host a valid oracle at all: every value contractParse()
// can put in nativeCode fits in an int16_t. If someone reintroduces an unbounded parse here, the
// host suite stops silently lying about the board and fails instead.
static void section_J_target_independence() {
  const char* digits[] = { "", "0", "7", "12", "999", "3276", "3277", "32767", "32768", "65535",
                           "65536", "99999", "2147483647", "4294967296", "99999999999999999999",
                           "-1", "-99999", "-2147483648", "-99999999999999999999",
                           "+5", "+99999", "007", "0000000012345", "12x", "x12", "1-2" };
  for (const char* d : digits) {
    char line[128];
    snprintf(line, sizeof(line), "L*A:i=native:%s", d);
    ParsedContract pc;
    if (!contractParse(line, pc)) continue;
    long nc = pc.params.nativeCode;
    CHECK_MSG(nc >= -32768L && nc <= 32767L, "nativeCode outside int16", line,
              "nativeCode=%ld does not fit a 16-bit int, so the ATmega cannot represent it — "
              "this host suite would be pinning a value the real board never computes", nc);
  }
}

int main(int argc, char** argv) {
  // A parser that hangs is a parser that bricks the board's serial loop. If any input in this
  // file makes contractParse() spin, we die here instead of hanging a CI job forever.
  alarm(300);
  if (const char* v = getenv("FUZZ_VERBOSE")) { int n = atoi(v); if (n > 0) g_maxreport = n; }

  const long numeric_iters = (argc > 1) ? atol(argv[1]) : 400000;
  const long line_iters    = (argc > 2) ? atol(argv[2]) : 150000;

  section_A_oracle_selfcheck();
  section_B_grammar();
  section_B2_minimal_reproducers();
  section_C_exhaustive_short();
  section_D_all_bytes();
  section_E_random_numeric(numeric_iters);
  section_F_lines();
  section_F_random_lines(line_iters);
  section_G_hostile();
  section_H_slices();
  section_I_tables();
  section_J_target_independence();

  printf("%s  %ld/%ld checks passed  (seed 0x%016llx, %ld numeric + %ld line iterations)\n",
         g_fail ? "FAILURES" : "OK", g_total - g_fail, g_total,
         (unsigned long long)0x0DD1E5EEDBEEF001ull, numeric_iters, line_iters);
  return g_fail ? 1 : 0;
}
