// include/FastLED.h — DroidNet PSI Pro clean-room WS2812 driver.
// Copyright (c) 2026 Travis Cook. SPDX-License-Identifier: MIT
//
// WHAT THIS IS
// ------------
// A from-scratch replacement for the parts of FastLED 3.5.0 that Neil Hutchison's
// PSI Pro firmware actually uses: the CRGB/CHSV colour types, a handful of lib8tion
// 8-bit scalers, CHSV->CRGB (hsv2rgb_rainbow), and one WS2812 strand on Arduino D4
// (PD4), GRB, 48 LEDs. Dropping FastLED from lib_deps buys back ~2 KB of flash on a
// board that is 98% full in its I2C configuration, and it removes the unconditional
// `sei` that FastLED's show() executes inside the render — the root of this fork's
// I2C ISR re-entrancy hazard (see the interrupt-budget note at the bottom of show()).
//
// HOW IT IS WIRED (the firmware source is UNCHANGED)
// --------------------------------------------------
// src/main.cpp, include/functions.h and include/psi_vm.h still say `#include <FastLED.h>`,
// byte-for-byte as before. Nothing in the firmware changed. Only two things did:
//   * platformio.ini no longer lists fastled/FastLED, so on the real AVR build the
//     angle-bracket <FastLED.h> resolves to THIS file (it is the only FastLED.h left
//     on the -I include/ path).
//   * The host golden harness is untouched: its builds put -I test/host/native_mocks
//      AHEAD of -I include, so for the host build <FastLED.h> still resolves to
//     test/host/native_mocks/FastLED.h. Same substitution mechanism, two targets —
//     the AVR firmware gets this driver, the host goldens get the mock, and neither
//     ever sees the other. That is why every one of the 31 goldens is unaffected by
//     this swap: goldens capture the raw leds[] buffer plus the scale byte at the
//     show() boundary, and they compile against the mock, not against this file.
//
// CLEAN-ROOM PROVENANCE
// ---------------------
// The WS2812 byte emitter (emitFrame, below) is written directly from the device
// timing spec (T0H ~350 ns, T1H ~700 ns, bit period ~1250 ns, all +/-150 ns; latch
// = line low > 50 us). No code from light_ws2812 or FAB_LED (both GPL) was read or
// used. The 8-bit scalers and hsv2rgb_rainbow reproduce FastLED 3.5.0 SEMANTICS
// (FASTLED_SCALE8_FIXED=1, its shipped default) so the rendered output is identical;
// the code here is original and its equivalence is PROVEN, not asserted, by two host
// oracles that diff this driver against the vendored FastLED 3.5.0 source over the
// full input domain:
//     test/host/test_scale_oracle.cpp   scale8 / scale8_video / nscale8x3_video / qmul8
//     test/host/test_hsv_oracle.cpp     CHSV -> CRGB (hsv2rgb_rainbow), all 16,777,216 inputs
// FastLED is (c) Daniel Garcia and contributors, MIT-licensed; its behaviour is the
// reference these oracles hold this file to. It is matched, not copied.
//
// NEVER FLASHED. This driver is cross-compiled and host-proven; the bit timing has
// been cycle-counted against the linked ELF but not yet seen on a scope. The README's
// STOP section still governs: no line here claims bench verification.

#ifndef DROIDNET_FASTLED_H
#define DROIDNET_FASTLED_H

#include <stdint.h>
#include <string.h>
#if defined(__AVR__)
#include <Arduino.h>            // also supplies `byte` etc. to functions.h, as FastLED.h did
#include <avr/io.h>
#include <avr/interrupt.h>
#endif
// The colour types and 8-bit math below are pure and host-portable: the scale/HSV
// oracles (test/host/test_scale_oracle.cpp, test_hsv_oracle.cpp) include THIS file
// on the host and hold these exact functions to the vendored FastLED 3.5.0 source.
// Only the WS2812 controller (emitter, pin/timer access) is AVR-only and is guarded
// out on the host — the AVR build is byte-for-byte unchanged by that guard.

// ---------------------------------------------------------------------------
// lib8tion-compatible 8-bit math.
// Semantics == FastLED 3.5.0 C reference with FASTLED_SCALE8_FIXED == 1
// (fastled_config.h:37, the shipped default). Proven byte-exact by
// test/host/test_scale_oracle.cpp over all 256*256 inputs.
// ---------------------------------------------------------------------------

// scale8(i, s) = i * (s + 1) / 256. The +1 (the FIXED variant) makes
// scale8(x, 255) == x exactly. FastLED ref: scale8.h:24.
static inline uint8_t scale8(uint8_t i, uint8_t scale) {
  return (uint8_t)(((uint16_t)i * (1u + (uint16_t)scale)) >> 8);
}

// "video" scale: an all-or-nothing floor of 1 so a lit channel never dims to black
// while the scale is nonzero. FastLED ref: scale8.h:101 (SCALE8_C path).
static inline uint8_t scale8_video(uint8_t i, uint8_t scale) {
  return (uint8_t)(((uint16_t)i * (uint16_t)scale) >> 8) + ((i && scale) ? 1 : 0);
}

// Per-channel video scale of an (r,g,b). FastLED ref: scale8.h:344 (SCALE8_C path):
// nonzero channels get the +1 floor iff scale != 0; zero channels stay zero.
static inline void nscale8x3_video(uint8_t &r, uint8_t &g, uint8_t &b, uint8_t scale) {
  uint8_t nz = (scale != 0) ? 1 : 0;
  r = (r == 0) ? 0 : (uint8_t)(((uint16_t)r * (uint16_t)scale) >> 8) + nz;
  g = (g == 0) ? 0 : (uint8_t)(((uint16_t)g * (uint16_t)scale) >> 8) + nz;
  b = (b == 0) ? 0 : (uint8_t)(((uint16_t)b * (uint16_t)scale) >> 8) + nz;
}

// Saturating 8x8 multiply. FastLED ref: math8.h:379 (QMUL8_C path).
static inline uint8_t qmul8(uint8_t i, uint8_t j) {
  uint16_t p = (uint16_t)i * (uint16_t)j;
  return (p > 255) ? 255 : (uint8_t)p;
}

// ---------------------------------------------------------------------------
// CHSV / CRGB
// ---------------------------------------------------------------------------

struct CHSV {
  uint8_t h, s, v;
  CHSV() {}
  CHSV(uint8_t ih, uint8_t is, uint8_t iv) : h(ih), s(is), v(iv) {}
};

struct CRGB;
static void hsv2rgb_rainbow(const CHSV &hsv, CRGB &rgb);

struct CRGB {
  uint8_t r, g, b;

  // The named colours this firmware uses. Values match FastLED's HTML colour table;
  // note Green is HTML green 0x008000 (main.cpp:51 deliberately swapped White->Grey).
  enum HTMLColorCode : uint32_t {
    Black = 0x000000,
    Red   = 0xFF0000,
    Green = 0x008000,
    Grey  = 0x808080,
    White = 0xFFFFFF
  };

  CRGB() {}  // deliberately uninitialised, like FastLED
  CRGB(uint8_t ir, uint8_t ig, uint8_t ib) : r(ir), g(ig), b(ib) {}
  CRGB(uint32_t colorcode)
      : r((uint8_t)(colorcode >> 16)), g((uint8_t)(colorcode >> 8)), b((uint8_t)colorcode) {}
  CRGB(HTMLColorCode c) : CRGB((uint32_t)c) {}
  CRGB(const CHSV &hsv) { hsv2rgb_rainbow(hsv, *this); }

  CRGB &operator=(uint32_t colorcode) {
    r = (uint8_t)(colorcode >> 16);
    g = (uint8_t)(colorcode >> 8);
    b = (uint8_t)colorcode;
    return *this;
  }
  // FastLED: %= is nscale8x3_video (video floor).
  CRGB &operator%=(uint8_t scaledown) {
    nscale8x3_video(r, g, b, scaledown);
    return *this;
  }
  CRGB &nscale8_video(uint8_t scaledown) {
    nscale8x3_video(r, g, b, scaledown);
    return *this;
  }
  // FastLED: *= is per-channel saturating qmul8.
  CRGB &operator*=(uint8_t d) {
    r = qmul8(r, d);
    g = qmul8(g, d);
    b = qmul8(b, d);
    return *this;
  }
};
static_assert(sizeof(CRGB) == 3, "wire staging assumes a packed 3-byte CRGB");

static inline void fill_solid(CRGB *leds, int numToFill, const CRGB &color) {
  for (int i = 0; i < numToFill; ++i) leds[i] = color;
}

// ---------------------------------------------------------------------------
// CHSV -> CRGB : FastLED 3.5.0 hsv2rgb_rainbow, default config (Y1=1, Y2=0,
// G2=0, Gscale=0 — hsv2rgb.cpp:288-297). The PSI's only CHSV call site is
// ContractPSI.h:216, CE_RAINBOW, at s=255,v=255 (the sat/val tails below are
// then skipped); this reproduces the whole function so the equivalence holds
// for every (h,s,v), which is what test/host/test_hsv_oracle.cpp checks.
//
// This is the perceptually re-balanced "rainbow" wheel (eight 32-wide hue
// sections ramped in thirds), NOT the raw spectrum wheel — CE_RAINBOW and the
// Studio visualiser's JS parity vectors both assume FastLED's rainbow.
// ---------------------------------------------------------------------------
static void hsv2rgb_rainbow(const CHSV &hsv, CRGB &rgb) {
  const uint8_t hue = hsv.h;
  const uint8_t sat = hsv.s;
  uint8_t val = hsv.v;

  const uint8_t offset8 = (uint8_t)((hue & 0x1F) << 3);   // 0..248
  const uint8_t third = scale8(offset8, 85);              // (offset8 * 86) >> 8, max 83

  uint8_t r, g, b;
  if (!(hue & 0x80)) {
    if (!(hue & 0x40)) {
      if (!(hue & 0x20)) {                 // 000  R -> O
        r = 255 - third; g = third; b = 0;
      } else {                             // 001  O -> Y   (Y1 boost)
        r = 171; g = (uint8_t)(85 + third); b = 0;
      }
    } else {
      if (!(hue & 0x20)) {                 // 010  Y -> G   (Y1 boost)
        uint8_t twothirds = scale8(offset8, 170);         // (offset8 * 171) >> 8, max 165
        r = (uint8_t)(171 - twothirds); g = (uint8_t)(170 + third); b = 0;
      } else {                             // 011  G -> A
        r = 0; g = 255 - third; b = third;
      }
    }
  } else {
    if (!(hue & 0x40)) {
      if (!(hue & 0x20)) {                 // 100  A -> B
        uint8_t twothirds = scale8(offset8, 170);
        r = 0; g = (uint8_t)(171 - twothirds); b = (uint8_t)(85 + twothirds);
      } else {                             // 101  B -> P
        r = third; g = 0; b = 255 - third;
      }
    } else {
      if (!(hue & 0x20)) {                 // 110  P -> K
        r = (uint8_t)(85 + third); g = 0; b = (uint8_t)(171 - third);
      } else {                             // 111  K -> R
        r = (uint8_t)(170 + third); g = 0; b = (uint8_t)(85 - third);
      }
    }
  }
  // G2 == 0 and Gscale == 0 in the default config: the two green trims are skipped.

  // Desaturate toward a white floor (hsv2rgb.cpp:439-465). FASTLED_SCALE8_FIXED path:
  // plain scale8 (no +1), then add the desat floor. uint8_t arithmetic wraps exactly
  // as FastLED's does.
  if (sat != 255) {
    if (sat == 0) {
      r = g = b = 255;
    } else {
      uint8_t desat = scale8_video((uint8_t)(255 - sat), (uint8_t)(255 - sat));
      uint8_t satscale = (uint8_t)(255 - desat);
      r = scale8(r, satscale);
      g = scale8(g, satscale);
      b = scale8(b, satscale);
      r = (uint8_t)(r + desat);
      g = (uint8_t)(g + desat);
      b = (uint8_t)(b + desat);
    }
  }

  // Scale down by value (hsv2rgb.cpp:468-486). FIXED path: plain scale8.
  if (val != 255) {
    val = scale8_video(val, val);
    if (val == 0) {
      r = g = b = 0;
    } else {
      r = scale8(r, val);
      g = scale8(g, val);
      b = scale8(b, val);
    }
  }

  rgb.r = r;
  rgb.g = g;
  rgb.b = b;
}

// ---------------------------------------------------------------------------
// Controller shim: the FastLED call surface main.cpp uses, and nothing else.
// ---------------------------------------------------------------------------

// Wire-order tags (octal digit per channel, FastLED-style). Only GRB is honoured by
// the emitter; the rest exist so the names resolve. Like FastLED, the enumerator
// `RGB` would name-hide a `struct RGB` — the contract layer already renamed its
// struct to ContractRGB for exactly this reason (contract_core.h:12).
enum EOrder { RGB = 0012, RBG = 0021, GRB = 0102, GBR = 0120, BRG = 0201, BGR = 0210 };

// Chipset tag. The emitter always produces WS2812 timing; the tag keeps the
// FastLED.addLeds<WS2812, PIN, ORDER>(...) call site unchanged.
struct WS2812 {};

#if defined(__AVR__)  // WS2812 controller: AVR-only (pin access + cycle-exact bit-bang + timer)
class CFastLED {
public:
  // Compile-time pin binding, like FastLED's. This firmware only ever drives
  // Arduino D4 == PD4 on the ATmega32U4; a static_assert nails that so a future
  // wiring change can't silently emit on the wrong pin.
  template <typename CHIPSET, uint8_t DATA_PIN, EOrder ORDER>
  CFastLED &addLeds(CRGB *data, int nLeds) {
    static_assert(DATA_PIN == 4, "clean-room emitter is bound to Arduino D4 / PD4");
    static_assert(ORDER == GRB, "clean-room emitter writes GRB wire order only");
    m_leds = data;
    m_n = (nLeds > kMaxLeds) ? kMaxLeds : nLeds;
    DDRD |= _BV(4);            // PD4 output
    PORTD &= (uint8_t)~_BV(4); // idle low
    return *this;
  }

  void setBrightness(uint8_t s) { m_scale = s; }

  // FastLED::clear(false): zero the CRGB array; does not latch.
  void clear() {
    if (m_leds) memset((void *)m_leds, 0, (size_t)m_n * sizeof(CRGB));
  }

  void show() { show(m_scale); }  // no-arg show uses the stored master brightness

  // Per-call master brightness, matching CFastLED::show(uint8_t scale). With the
  // default (uncorrected) colour correction and temperature this firmware never
  // changes, FastLED's per-channel adjustment reduces to exactly `scale`
  // (controller.h:150-168, computeAdjustment(scale,0xFFFFFF,0xFFFFFF) == scale),
  // and its temporal dither term is FORCED to zero here because FastLED disables
  // dithering whenever the measured frame rate is < 100 fps (FastLED.cpp:58) and
  // this firmware is gated to ~40 fps. So FastLED's wire value for a channel is
  // scale8(channel, scale); this computes the same. Proven equivalent by the
  // scale oracle over all 256*256 (channel, scale) pairs.
  void show(uint8_t scale) {
    if (!m_leds || m_n <= 0) return;
    // Stage the frame: per-channel scale8 into GRB wire order, interrupts still on.
    uint8_t *w = sWire;
    for (int i = 0; i < m_n; ++i) {
      *w++ = scale8(m_leds[i].g, scale);
      *w++ = scale8(m_leds[i].r, scale);
      *w++ = scale8(m_leds[i].b, scale);
    }
    const uint8_t nbytes = (uint8_t)(m_n * 3);  // 48*3 = 144; fits uint8_t up to 85 LEDs

    // Bit-bang with interrupts masked for the whole frame. SREG save/restore rather
    // than a blind sei: if show() is reached from inside an ISR (the -e PSIPro-i2c
    // build's TWI slave path can render from receiveEvent -> allOFF -> show), the
    // I-bit was already clear, so it STAYS clear across the frame and the ISR cannot
    // re-enter itself. FastLED's showPixels() ends with an unconditional sei() here,
    // which is precisely what let that ISR nest (psi-i2c-isr-nesting-hazard). This
    // driver removes that route structurally; see the interrupt-budget note below.
    const uint8_t sreg = SREG;
    cli();
    emitFrame(sWire, nbytes);
    SREG = sreg;

    // Clock correction, matching FastLED-on-AVR (clockless_trinket.h:130-159, active
    // because FASTLED_ALLOW_INTERRUPTS==0 and NO_CLOCK_CORRECTION is undefined): the
    // frame masked interrupts for ~nbytes*10 us (8 bits * 1.25 us/bit). One timer0
    // overflow (~1024 us) is queued by hardware and serviced the instant interrupts
    // return, so only the masked time BEYOND that one tick is lost to millis(). Add it
    // back through a microsecond accumulator. Same intent as FastLED; both approximate.
    {
      extern volatile unsigned long timer0_millis;  // Arduino core wiring.c, non-static
      static uint16_t carry_us = 0;
      uint16_t frame_us = (uint16_t)nbytes * 10u;   // 144 -> 1440 us
      if (frame_us > 1024u) {
        carry_us += (uint16_t)(frame_us - 1024u);   // ~416 us/frame
        while (carry_us >= 1000u) { carry_us -= 1000u; timer0_millis += 1u; }
      }
    }
  }

private:
  static const int kMaxLeds = 48;  // == NUM_LEDS (config.h is included after this header)

  // Staged GRB wire bytes. +1 spare: the emitter preloads the NEXT byte at each byte
  // boundary before testing the counter, so on the final byte it reads one byte past
  // the live data; the value is discarded, but keep that read inside our own array.
  static uint8_t sWire[(size_t)kMaxLeds * 3 + 1];

  // ---- WS2812 byte emitter, ATmega32U4 @ 16 MHz (62.5 ns/cycle), PD4 ------------
  // Written from the WS2812 timing spec: T0H 350 ns, T1H 700 ns, bit period 1250 ns,
  // each +/-150 ns; latch when line low > 50 us. Pin changes land at the END of an
  // sbi/cbi (2 cycles each). T0 below = the instant the sbi completes (line HIGH).
  //
  // Per-bit budget: 20 cycles = 1250 ns.
  //   sbi   2cy   line HIGH at T0
  //   lsl   1cy   MSB -> carry                                (T0+1)
  //   brcs  1cy not taken (0-bit) / 2cy taken (1-bit)
  //   0-bit: cbi 2cy -> LOW at T0+4  => T0H = 4cy  = 250 ns, in [200..500] spec window
  //          rjmp 2cy -> both paths converge at T0+6
  //   1-bit: nop nop nop            -> converges at T0+6 (paths cycle-balanced)
  //   nop x4                                                  (T0+10)
  //   cbi   2cy  -> LOW at T0+12    => T1H = 12cy = 750 ns, in [550..850] spec window
  //              (no-op for a 0-bit: line is already low)
  //   dec bits 1cy                                            (T0+13)
  //   breq  1cy not taken (mid-byte)                          (T0+14)
  //   nop nop                                                 (T0+16)
  //   rjmp  2cy -> next sbi completes at T0+20. Bit period exactly 20 cy.
  //
  // Byte boundary (breq taken, T0+15): ld X+ 2cy (T0+17), dec n 1cy (T0+18),
  // brne 2cy (T0+20), ldi bits 1cy (T0+21), sbi -> next HIGH at T0+23. The last bit
  // of each byte is therefore 23 cy = 1437.5 ns: the stretch is entirely LOW-side
  // tail, which WS2812s tolerate up to the >50 us latch threshold (the highs are what
  // the sampling window cares about).
  //
  // After the final byte, brne falls through with the line low; the caller's next
  // show() is >= 10 ms away at every call site, so the > 50 us latch happens for free.
  static void emitFrame(const uint8_t *p, uint8_t nbytes) {
    uint8_t cur, bits;
    asm volatile(
        "    ld   %[cur], %a[ptr]+          \n\t"  // preload first byte (outside frame)
        "Lbyte_%=:                          \n\t"
        "    ldi  %[bits], 8                \n\t"
        "Lbit_%=:                           \n\t"
        "    sbi  %[port], %[pin]           \n\t"  // 2  line HIGH at T0
        "    lsl  %[cur]                    \n\t"  // 1  MSB -> C
        "    brcs Lone_%=                   \n\t"  // 1/2
        "    cbi  %[port], %[pin]           \n\t"  // 2  0-bit: LOW at T0+4 (T0H 250 ns)
        "    rjmp Lmid_%=                   \n\t"  // 2  -> T0+6
        "Lone_%=:                           \n\t"  //    1-bit path: here at T0+3
        "    nop                            \n\t"  // 1  T0+4
        "    nop                            \n\t"  // 1  T0+5
        "    nop                            \n\t"  // 1  T0+6 (paths balanced)
        "Lmid_%=:                           \n\t"
        "    nop                            \n\t"  // 1  T0+7
        "    nop                            \n\t"  // 1  T0+8
        "    nop                            \n\t"  // 1  T0+9
        "    nop                            \n\t"  // 1  T0+10
        "    cbi  %[port], %[pin]           \n\t"  // 2  1-bit: LOW at T0+12 (T1H 750 ns)
        "    dec  %[bits]                   \n\t"  // 1  T0+13
        "    breq Lnext_%=                  \n\t"  // 1  mid-byte / 2 byte boundary
        "    nop                            \n\t"  // 1  T0+15
        "    nop                            \n\t"  // 1  T0+16
        "    rjmp Lbit_%=                   \n\t"  // 2  -> sbi HIGH at T0+20
        "Lnext_%=:                          \n\t"  //    byte boundary: here at T0+15
        "    ld   %[cur], %a[ptr]+          \n\t"  // 2  preload next byte (see sWire +1)
        "    dec  %[n]                      \n\t"  // 1  T0+18
        "    brne Lbyte_%=                  \n\t"  // 2  -> ldi, sbi: next HIGH at T0+23
        : [ptr] "+e"(p), [cur] "=&r"(cur), [bits] "=&d"(bits), [n] "+r"(nbytes)
        : [port] "I"(_SFR_IO_ADDR(PORTD)), [pin] "I"(4)
        : "memory");
  }

  CRGB *m_leds = nullptr;
  int m_n = 0;
  uint8_t m_scale = 255;
};

uint8_t CFastLED::sWire[(size_t)CFastLED::kMaxLeds * 3 + 1];

// Single-TU firmware (src/main.cpp is the only compiled unit; the other headers are
// #included into it), so defining the instance in the header is safe.
static CFastLED FastLED;
#endif  // defined(__AVR__)

#endif  // DROIDNET_FASTLED_H
