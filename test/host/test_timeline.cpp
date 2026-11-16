// test/host/test_timeline.cpp — the firmware (src/main.cpp, verbatim) on the golden harness's host
// mocks, driven by a scripted wire timeline, with assertions on the frames it shows.
//
// One scenario per process (the firmware keeps static state): `test_timeline --list` prints the
// names, `test_timeline <name>` runs one and exits 0 (pass) or 1 (fail). run.sh loops over the list.
// Assertions read only what is observable on the wire and at the LEDs: the frames FastLED.show()
// latched (time, master scale, pixels) and the bytes written to Serial1. They do not read contract
// globals, so they hold across refactors of the layer under test.
#include "native_mocks/Arduino.h"
#include "native_mocks/FastLED.h"
#include "native_mocks/EEPROM.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "../../src/main.cpp"  // the firmware, verbatim

struct Frame {
  uint32_t t;
  uint8_t scale;
  uint8_t lit;          // pixels with any channel on
  CRGB px[NUM_LEDS];
  bool allAre(uint32_t rgb) const {
    for (int i = 0; i < NUM_LEDS; i++)
      if (px[i].r != ((rgb >> 16) & 0xFF) || px[i].g != ((rgb >> 8) & 0xFF) || px[i].b != (rgb & 0xFF)) return false;
    return true;
  }
};
struct Ev { uint32_t t; std::string line; bool i2c; };

static std::vector<Frame> g_frames;
static std::vector<Ev> g_evs;
static uint32_t g_now = 0;
static int g_fails = 0;

static void hook(const CRGB* l, int n, uint8_t scale) {
  Frame f; f.t = g_mock_millis; f.scale = scale; f.lit = 0;
  for (int i = 0; i < n && i < NUM_LEDS; i++) { f.px[i] = l[i]; if (l[i].r | l[i].g | l[i].b) f.lit++; }
  g_frames.push_back(f);
}
// Cold boot. ee: alwaysOn, pot-vs-internal, internal brightness (see golden_native.cpp for the gates).
static void boot(uint8_t ee0 = 0x00, uint8_t ee1 = 0x01, uint8_t ee2 = 20, bool front = true, int pot = 512) {
  g_avr_next = 1; g_mock_analog = pot; g_mock_pins[JUMP_FRONT_REAR] = front ? 1 : 0;
  EEPROM.mem[alwaysOnAddress] = ee0; EEPROM.mem[externalPOTAddress] = ee1; EEPROM.mem[internalBrightnessAddress] = ee2;
  g_frame_hook = hook;
  setup();
}
static void at(uint32_t t, const char* line) { g_evs.push_back({t, line, false}); }
#ifdef PSI_ENABLE_I2C
// A line from an I2C master at time t. The receive callback runs between two loop passes, as the
// TWI interrupt would run it.
static void atI2c(uint32_t t, const char* line) { g_evs.push_back({t, std::string(line) + "\r", true}); }
#endif
static void runTo(uint32_t t) {
  while (g_now < t) {
    g_now++; g_mock_millis = g_now;
    // The mock's RX buffer is 512 bytes for the life of the process and never rewinds; rewind it
    // whenever the firmware has drained it, or a long timeline silently loses its later lines.
    if (Serial1.rxHead == Serial1.rxTail) Serial1.rxHead = Serial1.rxTail = 0;
    for (const Ev& e : g_evs) if (e.t == g_now) {
#ifdef PSI_ENABLE_I2C
      if (e.i2c) { Wire.feed(e.line.c_str()); receiveEvent((int)e.line.size()); continue; }
#endif
      Serial1.feed(e.line.c_str()); Serial1.feed("\r");
    }
    loop(); serialEventRun();
  }
}
// The frame on the panel at time t: the last one latched at or before t.
static const Frame& shown(uint32_t t) {
  static Frame none{};
  const Frame* f = &none;
  for (const Frame& x : g_frames) { if (x.t <= t) f = &x; else break; }
  return *f;
}
// Frames latched in [a, b].
static std::vector<Frame> between(uint32_t a, uint32_t b) {
  std::vector<Frame> out;
  for (const Frame& x : g_frames) if (x.t >= a && x.t <= b) out.push_back(x);
  return out;
}
// Times at which the panel BECAME one solid colour (a frame that is all `rgb` after one that is not).
static std::vector<uint32_t> onsets(uint32_t a, uint32_t b, uint32_t rgb) {
  std::vector<uint32_t> out; bool was = false;
  for (const Frame& x : g_frames) {
    if (x.t > b) break;
    const bool is = x.allAre(rgb);
    if (x.t >= a && is && !was) out.push_back(x.t);
    was = is;
  }
  return out;
}
#define CHECK(cond, ...) do { if (!(cond)) { printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); g_fails++; } } while (0)

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final XOR), as verb V folds it.
static uint16_t crc16(const char* s) {
  uint16_t c = 0xFFFF;
  for (; *s; s++) { c ^= (uint16_t)((uint8_t)*s) << 8; for (int i = 0; i < 8; i++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1); }
  return c;
}

// ---------------------------------------------------------------------------------------------
// Scenarios. Front PSI, internal brightness 20 (EEPROM 00,01,14) unless a scenario says otherwise:
// the owner's brightness is 20, so native frames latch at scale 20.
// ---------------------------------------------------------------------------------------------

// Holds today: a pushed score mixes native modes and contract effects (review S8).
static void score_mixes_native_and_contract() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=native:1,at=0"); at(1002, "!P*A:i=comet,c=00ff00,at=4");
  at(1003, "!P*A:i=native:15,at=8"); at(1004, "!**C:bpm=120,bpb=4,beat=0");
  runTo(6000);
  CHECK(shown(2500).lit == 48, "beats 0-3 should be the swipe (48 lit), saw %u lit", shown(2500).lit);
  const Frame& c = shown(4000);
  CHECK(c.lit > 0 && c.lit < 48 && c.px[20].r == 0 && c.px[20].b == 0, "beats 4-7 should be a green comet");
  unsigned most = 0;
  for (const Frame& f : between(5100, 6000)) if (f.lit > most) most = f.lit;
  CHECK(most > 0 && most <= 6, "beats 8+ should be Knight Rider (one column, <= 6 lit), saw up to %u lit", most);
}

#ifndef PSI_SCORE_CAP
#define PSI_SCORE_CAP 8   // firmware before v1.3
#endif
struct Scenario { const char* name; void (*fn)(); };
// -DTIMELINE_SCENARIOS='"<file>"' runs another scenario file through the same harness.
#ifndef TIMELINE_SCENARIOS
#define TIMELINE_SCENARIOS "timeline_scenarios.inc"
#endif
#include TIMELINE_SCENARIOS
static const Scenario kBase[] = { {"score_mixes_native_and_contract", score_mixes_native_and_contract} };

int main(int argc, char** argv) {
  std::vector<Scenario> all(kBase, kBase + sizeof kBase / sizeof kBase[0]);
  for (const Scenario& s : kScenarios) all.push_back(s);
  if (argc == 2 && !strcmp(argv[1], "--list")) { for (const Scenario& s : all) puts(s.name); return 0; }
  if (argc != 2) { fprintf(stderr, "usage: %s --list | <scenario>\n", argv[0]); return 2; }
  for (const Scenario& s : all) if (!strcmp(s.name, argv[1])) {
    s.fn();
    printf("%s %s\n", g_fails ? "FAIL" : "ok  ", s.name);
    return g_fails ? 1 : 0;
  }
  fprintf(stderr, "no scenario named %s\n", argv[1]);
  return 2;
}
