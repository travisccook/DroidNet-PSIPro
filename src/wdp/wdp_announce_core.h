// src/wdp/wdp_announce_core.h — pure, dependency-free WDP device announce (WDP-DA) logic.
// Host-compilable (no Arduino, no stdio). The board tells the WCB it is wired to what it is:
//
//     @WDP1 {"type":"PSI Front","fw":"fd352e7","caps":["droidnet.contract"]}
//
// one line every 25-30 s, as Greg Hulette's WCB firmware documents it (docs/WDP_DEVICE_ANNOUNCE.md
// in greghulette/Wireless_Communication_Board-WCB, WCB 7.0.1). The WCB consumes the line; it is
// never forwarded to another port or board.
//
// The firmware adapter calls wdpTick() every loop pass and writes exactly the bytes it is handed.
// It passes the room left in its serial TX buffer, so a write never blocks: an AVR
// HardwareSerial has 63 bytes free when empty and every fork's line is longer, so a line goes
// out in pieces over a few passes (about 20 ms at 9600 baud). Before the fork writes anything
// else to the same port (a contract reply, a status line), it writes wdpRest() first, so no
// other text lands inside an announce. "now" is passed in and never read here, the same seam
// contract_core.h and wifi_connect_core.h use.
//
// Shared byte-identically by the RSeries, PSI Pro and FlthyHPs forks.
// Part of the DroidNet board forks. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: LGPL-2.1-only
#pragma once
#include <stdint.h>
#include <stddef.h>

// The WCB keeps this many characters of each field (WCB_WDP.h WdpDaDevice). A longer one would
// be kept silently truncated, for good, so the core refuses it instead.
#define WDP_TYPE_MAX 24
#define WDP_FW_MAX   27
#define WDP_CAPS_MAX 48

// The line buffer: line + NUL. The longest fork line (FlthyHPs with a "-dirty" fw) is 106.
#ifndef WDP_LINE_MAX
#define WDP_LINE_MAX 128
#endif
static_assert(WDP_LINE_MAX <= 255, "line lengths are uint8_t");

#define WDP_FIRST_MIN_MS 500UL     // first announce, after boot
#define WDP_FIRST_MAX_MS 3000UL
#define WDP_EVERY_MIN_MS 25000UL   // then every 25-30 s, freshly jittered each time
#define WDP_EVERY_MAX_MS 30000UL

struct WdpAnnouncer {
    char     line[WDP_LINE_MAX];
    uint8_t  len;     // bytes in line while one is in flight; 0 = none
    uint8_t  pos;     // bytes of it already handed out
    uint32_t dueMs;   // when the next line starts
    uint32_t rng;     // xorshift32 state, never 0
};

// A byte the WCB can store as sent: no JSON escapes on its side (a quote or backslash ends the
// value early), and wdpScrub() turns ',' ']' and control bytes into '_'. UTF-8 is fine.
inline bool wdpFieldByteOk(char c) {
    uint8_t u = (uint8_t)c;
    return u >= 0x20 && u != 0x7F && c != '"' && c != '\\' && c != ',' && c != ']';
}

// Length of a field made of acceptable bytes, or -1 if one is not.
inline int wdpFieldLen(const char* s) {
    int n = 0;
    for (; s[n]; n++)
        if (!wdpFieldByteOk(s[n])) return -1;
    return n;
}

// Appends s[0..n) at out[*o], if it fits before cap (keeping a byte for the NUL).
inline bool wdpPut(char* out, size_t cap, size_t* o, const char* s, size_t n) {
    if (*o + n + 1 > cap) return false;
    for (size_t i = 0; i < n; i++) out[(*o)++] = s[i];
    return true;
}
inline bool wdpPutStr(char* out, size_t cap, size_t* o, const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return wdpPut(out, cap, o, s, n);
}

// Builds the announce line, newline included, NUL-terminated. Returns its length, or 0 when the
// identity cannot be sent as is: type or fw missing, a field past its WCB limit, a byte the WCB
// would mangle, caps not single-space separated, or a line that does not fit cap.
// caps is space-separated tags ("droidnet.contract hp.servo"); null or "" sends none.
inline uint8_t wdpBuildLine(char* out, size_t cap, const char* type, const char* fw, const char* caps) {
    if (!type || !fw) return 0;
    if (!caps) caps = "";
    int tLen = wdpFieldLen(type), fLen = wdpFieldLen(fw), cLen = wdpFieldLen(caps);
    if (tLen < 1 || tLen > WDP_TYPE_MAX) return 0;
    if (fLen < 1 || fLen > WDP_FW_MAX) return 0;
    if (cLen < 0 || cLen > WDP_CAPS_MAX) return 0;
    if (cLen > 0 && (caps[0] == ' ' || caps[cLen - 1] == ' ')) return 0;
    for (int i = 1; i < cLen; i++)
        if (caps[i] == ' ' && caps[i - 1] == ' ') return 0;

    size_t o = 0;
    if (!wdpPutStr(out, cap, &o, "@WDP1 {\"type\":\"") || !wdpPut(out, cap, &o, type, tLen) ||
        !wdpPutStr(out, cap, &o, "\",\"fw\":\"") || !wdpPut(out, cap, &o, fw, fLen) ||
        !wdpPutStr(out, cap, &o, "\""))
        return 0;
    if (cLen > 0) {
        if (!wdpPutStr(out, cap, &o, ",\"caps\":[\"")) return 0;
        for (int i = 0; i < cLen; i++) {
            bool ok = caps[i] == ' ' ? wdpPutStr(out, cap, &o, "\",\"") : wdpPut(out, cap, &o, &caps[i], 1);
            if (!ok) return 0;
        }
        if (!wdpPutStr(out, cap, &o, "\"]")) return 0;
    }
    if (!wdpPutStr(out, cap, &o, "}\n")) return 0;
    out[o] = '\0';
    return (uint8_t)o;
}

// A uniformly spread value in [lo, hi], from the announcer's own generator.
inline uint32_t wdpJitter(WdpAnnouncer& a, uint32_t lo, uint32_t hi) {
    a.rng ^= a.rng << 13;
    a.rng ^= a.rng >> 17;
    a.rng ^= a.rng << 5;
    return lo + a.rng % (hi - lo + 1);
}

// seed must differ board to board (a chip ID, an esp_random(), a floating analog pin), or
// chained boards that power up together announce in lockstep and garble each other.
inline void wdpInit(WdpAnnouncer& a, uint32_t seed, uint32_t nowMs) {
    // murmur3's finalizer: nearby seeds (two boards' serial numbers) start far apart.
    seed ^= seed >> 16; seed *= 0x85EBCA6BUL;
    seed ^= seed >> 13; seed *= 0xC2B2AE35UL;
    seed ^= seed >> 16;
    a.rng = seed ? seed : 0x9E3779B9UL;
    a.len = a.pos = 0;
    a.line[0] = '\0';
    a.dueMs = nowMs + wdpJitter(a, WDP_FIRST_MIN_MS, WDP_FIRST_MAX_MS);
}

// Every loop pass. Starts a line when one is due (reading type/fw/caps then) and hands out as
// much of the line in flight as txFree allows: *chunk points at the bytes, the return value is
// how many to write. 0 = nothing to write this pass.
inline uint8_t wdpTick(WdpAnnouncer& a, uint32_t nowMs, const char* type, const char* fw,
                       const char* caps, int txFree, const char** chunk) {
    if (a.len == 0 && (int32_t)(nowMs - a.dueMs) >= 0) {
        a.dueMs = nowMs + wdpJitter(a, WDP_EVERY_MIN_MS, WDP_EVERY_MAX_MS);
        a.len = wdpBuildLine(a.line, sizeof(a.line), type, fw, caps);   // 0: refused, skip this beat
        a.pos = 0;
    }
    if (a.len == 0 || txFree <= 0) return 0;
    int n = a.len - a.pos;
    if (n > txFree) n = txFree;
    *chunk = a.line + a.pos;
    a.pos += (uint8_t)n;
    if (a.pos == a.len) a.len = a.pos = 0;
    return (uint8_t)n;
}

// The unsent rest of a line in flight, for the fork to write (blocking is fine there) before
// it writes its own text to the same port. 0 = nothing in flight.
inline uint8_t wdpRest(WdpAnnouncer& a, const char** chunk) {
    if (a.len == 0) return 0;
    uint8_t n = a.len - a.pos;
    *chunk = a.line + a.pos;
    a.len = a.pos = 0;
    return n;
}
