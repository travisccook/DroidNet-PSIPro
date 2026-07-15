// Host mock of the FastLED 3.5.0 surface the PSI Pro sketch uses.
// Pixel math matches FastLED 3.5.0 semantics (FASTLED_SCALE8_FIXED build as
// shipped); re-implemented from observed behavior, no FastLED code copied.
#pragma once
#include "Arduino.h"

// ---- 8-bit math ----
inline uint8_t qmul8(uint8_t i, uint8_t j) {
  unsigned p = (unsigned)i * (unsigned)j;
  return (p > 255) ? 255 : (uint8_t)p;
}
// nscale8x3_video semantics: zero stays zero; nonzero gains +1 iff scale != 0.
inline uint8_t nscale8_video_ch(uint8_t c, uint8_t scale) {
  uint8_t bump = (scale != 0) ? 1 : 0;
  return (c == 0) ? 0 : (uint8_t)((((int)c * (int)scale) >> 8) + bump);
}

struct CHSV {
  uint8_t h, s, v;
  CHSV(uint8_t H = 0, uint8_t S = 0, uint8_t V = 0) : h(H), s(S), v(V) {}
};

struct CRGB {
  uint8_t r, g, b;
  enum HTMLColorCode : uint32_t {
    Black = 0x000000, White = 0xFFFFFF, Red = 0xFF0000,
    Green = 0x008000,  // NOT 0x00FF00 — HTML green.
    Grey  = 0x808080,
  };
  CRGB() : r(0), g(0), b(0) {}
  CRGB(uint8_t R, uint8_t G, uint8_t B) : r(R), g(G), b(B) {}
  CRGB(uint32_t c) : r((c >> 16) & 0xFF), g((c >> 8) & 0xFF), b(c & 0xFF) {}
  CRGB(HTMLColorCode c) : CRGB((uint32_t)c) {}
  CRGB(const CHSV& hsv);  // below
  CRGB& operator=(uint32_t c) { r = (c >> 16) & 0xFF; g = (c >> 8) & 0xFF; b = c & 0xFF; return *this; }
  CRGB& nscale8_video(uint8_t s) {
    r = nscale8_video_ch(r, s); g = nscale8_video_ch(g, s); b = nscale8_video_ch(b, s);
    return *this;
  }
  CRGB& operator%=(uint8_t s) { return nscale8_video(s); }
  CRGB& operator*=(uint8_t d) { r = qmul8(r, d); g = qmul8(g, d); b = qmul8(b, d); return *this; }
};
static_assert(sizeof(CRGB) == 3, "golden format assumes packed 3-byte CRGB");

// Spectrum-style HSV->RGB. NEVER exercised by goldens (the contract layer is
// idle in every capture; only CE_RAINBOW uses CHSV). Present so ContractPSI.h
// compiles. NOT FastLED's hsv2rgb_rainbow — do not capture goldens through it.
inline CRGB::CRGB(const CHSV& hsv) {
  uint8_t region = hsv.h / 43;
  uint8_t rem = (hsv.h - region * 43) * 6;
  uint8_t p = (uint16_t)hsv.v * (255 - hsv.s) >> 8;
  uint8_t q = (uint16_t)hsv.v * (255 - ((uint16_t)hsv.s * rem >> 8)) >> 8;
  uint8_t t = (uint16_t)hsv.v * (255 - ((uint16_t)hsv.s * (255 - rem) >> 8)) >> 8;
  switch (region) {
    case 0: r = hsv.v; g = t; b = p; break;
    case 1: r = q; g = hsv.v; b = p; break;
    case 2: r = p; g = hsv.v; b = t; break;
    case 3: r = p; g = q; b = hsv.v; break;
    case 4: r = t; g = p; b = hsv.v; break;
    default: r = hsv.v; g = p; b = q; break;
  }
}

enum EOrder { RGB = 0012, RBG = 0021, GRB = 0102, GBR = 0120, BRG = 0201, BGR = 0210 };
template <uint8_t DATA_PIN, EOrder RGB_ORDER = RGB> class WS2812 {};

// Frame capture hook — the whole point of the harness. Installed by the
// golden driver; every show() (with or without explicit scale) emits a frame.
inline void (*g_frame_hook)(const CRGB* leds, int n, uint8_t scale) = nullptr;

struct CFastLED {
  CRGB* m_leds = nullptr;
  int m_n = 0;
  uint8_t m_Scale = 255;
  template <template <uint8_t, EOrder> class CHIPSET, uint8_t DATA_PIN, EOrder RGB_ORDER>
  void addLeds(CRGB* l, int n) { m_leds = l; m_n = n; }
  void setBrightness(uint8_t s) { m_Scale = s; }
  void clear() { if (m_leds) memset((void*)m_leds, 0, (size_t)m_n * sizeof(CRGB)); }
  void show(uint8_t scale) { if (g_frame_hook) g_frame_hook(m_leds, m_n, scale); }
  void show() { show(m_Scale); }  // no-arg show latches at the STORED scale (FastLED.h:502 semantics)
};
inline CFastLED FastLED;

inline void fill_solid(CRGB* l, int n, const CRGB& c) { for (int i = 0; i < n; i++) l[i] = c; }
