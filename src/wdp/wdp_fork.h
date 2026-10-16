// src/wdp/wdp_fork.h — the PSI Pro fork's WDP device announce: this board's identity and the
// calls src/main.cpp and ContractPSI.h make. wdp_announce_core.h (shared byte-identically with
// the RSeries and FlthyHPs forks) builds and paces the line; this file only says what the board
// is and where the line goes:
//
//     @WDP1 {"type":"PSI Front","fw":"<commit>","caps":["droidnet.contract"]}
//
// "PSI Rear" on a board with the front/rear jumper fitted. Front and rear PSIs can share one WCB
// port, and the WCB keeps one record per (port, type), so the role is part of the type.
//
// The port is serialPort: Serial1, the header pins the WCB is wired to, at 9600 baud (USB Serial
// only in a USB_SERIAL debug build). The line leaves in pieces no bigger than the room in the TX
// buffer (63 bytes when empty; the line is 70-77), so loop() never waits on it. The one other
// write to serialPort, the verb-Q ack in ContractPSI.h, calls wdpFinish() first, so it never
// lands inside an announce. DEBUG_PRINT/DEBUG_PRINT_LN (include/config.h) also write there and do
// not call it; DEBUG is off (compiled out) in every build of this fork.
//
// Included by src/main.cpp after `Stream* serialPort` and BEFORE contract/ContractPSI.h, so the
// ack can call wdpFinish().
//
// Part of the DroidNet PSI Pro fork. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
#pragma once

// The build's commit: scripts/wdp_fw.py (platformio.ini extra_scripts) defines it from
// `git rev-parse --short=7 HEAD`, with "-dirty" for a tree with changes. Without it: "unknown".
#ifndef WDP_FW
#define WDP_FW "unknown"
#endif
#define WDP_FORK_TYPE_FRONT "PSI Front"   // jumper open (pin HIGH)
#define WDP_FORK_TYPE_REAR  "PSI Rear"    // jumper fitted (pin LOW)
#define WDP_FORK_CAPS       "droidnet.contract"

// The line buffer: exactly the longer of this build's two lines ("PSI Front") and its NUL, 72 B
// for a clean build and 78 B for a "-dirty" one, instead of the core's 128-byte default: this
// board has 2,560 B of SRAM. The literal spells the line the core builds (test/host stages 1c and
// 1d compare the two byte for byte); "" WDP_FW compiles only when WDP_FW is a string literal, so
// a define that lost its quotes fails the build here instead of announcing garbage.
#define WDP_LINE_MAX \
  sizeof("@WDP1 {\"type\":\"" WDP_FORK_TYPE_FRONT "\",\"fw\":\"" WDP_FW "\",\"caps\":[\"" WDP_FORK_CAPS "\"]}\n")
#include "wdp_announce_core.h"

static_assert(sizeof(WDP_FORK_TYPE_REAR) <= sizeof(WDP_FORK_TYPE_FRONT), "WDP_LINE_MAX is sized by the front line");
static_assert(sizeof(WDP_FORK_TYPE_FRONT) - 1 <= WDP_TYPE_MAX, "a WDP type is longer than the WCB keeps");
static_assert(sizeof("" WDP_FW) - 1 <= WDP_FW_MAX, "WDP_FW must be a string of at most WDP_FW_MAX chars");
static_assert(sizeof(WDP_FORK_CAPS) - 1 <= WDP_CAPS_MAX, "WDP_FORK_CAPS is longer than the WCB keeps");

#ifndef WDP_FORK_IDENTITY_ONLY   // host tests of the identity alone define this (no Arduino)
#include <avr/boot.h>

// Defined in contract/ContractPSI.h, included after this file.
inline bool contractAddressed(char cls, char unit);

static WdpAnnouncer gWdp;

// Front or rear, decided by the contract's own test: this board is "PSI Front" exactly when "!PF"
// addresses it (JUMP_FRONT_REAR HIGH), so the announce and the contract cannot disagree. Read on
// every tick; the core uses it only when a line starts.
inline const char* wdpForkType() {
  return contractAddressed('P', 'F') ? WDP_FORK_TYPE_FRONT : WDP_FORK_TYPE_REAR;
}

// The jitter seed. It must differ board to board, or PSIs that power up together announce in
// lockstep, and between the front and rear PSI, which may share a WCB port.
//
// The chip's factory serial number: the 10 bytes at 0x0E-0x17 of the ATmega32U4 signature row.
// The datasheet (Atmel-7766J, 27.7.10, Table 27-6) documents only 0x00-0x04 there and calls the
// rest reserved; LUFA reads these 80 bits as the unique USB serial number of every ATmegaxxU4
// (Device_AVR8.h: INTERNAL_SERIAL_START_ADDRESS 0x0E, INTERNAL_SERIAL_LENGTH_BITS 80), and QMK's
// AVR hardware ID reads the same ten bytes. boot_signature_byte_get() (avr-libc <avr/boot.h>)
// does not disable interrupts, and the LPM must follow its SPMCSR write within three cycles
// (datasheet 27.7.10), so interrupts are off for the ten reads, as LUFA does.
//
// FNV-1a over the ten bytes, then the jumper as a last byte: two values that differ only in the
// last byte stay different after the multiply (the FNV prime is odd), so the front and rear PSI
// never get the same seed, even on chips whose serial reads alike.
inline uint32_t wdpForkSeed(bool front) {
  uint32_t h = 2166136261UL;
  uint8_t sreg = SREG;
  cli();
  for (uint8_t a = 0x0E; a < 0x0E + 10; a++) h = (h ^ boot_signature_byte_get(a)) * 16777619UL;
  SREG = sreg;
  return (h ^ (front ? 1u : 2u)) * 16777619UL;
}

// Once, in setup() after serialPort is begun (and after the jumper pin's pull-up is on).
inline void wdpForkSetup() {
  wdpInit(gWdp, wdpForkSeed(contractAddressed('P', 'F')), (uint32_t)millis());
}

// Every loop() pass, outside the 25 ms render gate. Writes only what fits in the TX buffer now.
inline void wdpForkTick() {
  const char* chunk = 0;
  uint8_t n = wdpTick(gWdp, (uint32_t)millis(), wdpForkType(), WDP_FW, WDP_FORK_CAPS,
                      serialPort->availableForWrite(), &chunk);
  if (n) serialPort->write((const uint8_t*)chunk, n);
}

// Before any other write to serialPort: sends the rest of a line in flight, if there is one. This
// can wait for room in the TX buffer: at most the time to send that rest, under 0.1 s at 9600.
inline void wdpFinish() {
  const char* chunk = 0;
  uint8_t n = wdpRest(gWdp, &chunk);
  if (n) serialPort->write((const uint8_t*)chunk, n);
}
#endif  // WDP_FORK_IDENTITY_ONLY
