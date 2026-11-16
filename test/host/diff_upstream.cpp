// test/host/diff_upstream.cpp — one scripted timeline through (a) Neil Hutchison's upstream PSIPro.ino
// v1.7 (-DUPSTREAM) or (b) the fork's src/main.cpp, on the fork's own host mocks. Dumps every show()
// frame as "<ms> <scale> <48 pixels in hex>". diff_upstream.sh builds both and compares.
#include "native_mocks/Arduino.h"
// (native_mocks/FastLED.h is not included here: main.cpp names the S9 frame gate and then includes
// <FastLED.h> itself. ContractPSI.h refuses to build if the mock was compiled without the gate.)
#include "native_mocks/EEPROM.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef UPSTREAM
#include "../../include/functions.h"   // a sketch gets its prototypes generated; a plain C++ build needs them
#include "upstream-v1.7/PSIPro.ino"    // Neil Hutchison's v1.7 (5e80597), unmodified; its own config.h sits beside it
#else
#include "../../src/main.cpp"          // the fork
#endif
static FILE* g_out; static unsigned long nframes = 0;
// An I2C line that arrives INSIDE a show(): FastLED's show() re-enables interrupts while it
// clocks the pixels out, so the TWI interrupt (receiveEvent, which parses and starts the mode at
// once, as upstream's does) can land in the middle of the mode step that is drawing. Armed by an
// event "<t> I2C:<cmd>"; taken by the first show() at or after t.
static const char* g_isrCmd = nullptr; static bool g_inIsr = false;
static void hook(const CRGB* l, int n, uint8_t scale) {
  fprintf(g_out, "%u %u ", (unsigned)g_mock_millis, scale);
  for (int i = 0; i < n; i++) fprintf(g_out, "%02x%02x%02x", l[i].r, l[i].g, l[i].b);
  fputc('\n', g_out); nframes++;
#if defined(UPSTREAM) || defined(PSI_ENABLE_I2C)
  if (g_isrCmd && !g_inIsr) {
    char line[40]; snprintf(line, sizeof line, "%s\r", g_isrCmd);
    g_isrCmd = nullptr; g_inIsr = true;
    Wire.feed(line); receiveEvent((int)strlen(line));
    g_inIsr = false;
  }
#endif
}
int main(int argc, char** argv) {
  // argv: out jumper ee0 ee1 ee2 pot seed dur [t cmd]...
  g_out = fopen(argv[1], "w");
  g_mock_pins[JUMP_FRONT_REAR] = strcmp(argv[2], "rear") ? 1 : 0;
  EEPROM.mem[0] = strtoul(argv[3], 0, 16); EEPROM.mem[1] = strtoul(argv[4], 0, 16); EEPROM.mem[2] = strtoul(argv[5], 0, 16);
  g_mock_analog = atoi(argv[6]); g_avr_next = strtoul(argv[7], 0, 10);
  unsigned long dur = strtoul(argv[8], 0, 10);
  g_frame_hook = hook; setup();
  int ei = 9;
  for (uint32_t t = 1; t <= dur; t++) {
    g_mock_millis = t;
    while (ei + 1 < argc && strtoul(argv[ei], 0, 10) == t) {
      if (!strncmp(argv[ei + 1], "I2C:", 4)) g_isrCmd = argv[ei + 1] + 4;
      else { Serial1.feed(argv[ei + 1]); Serial1.feed("\r"); }
      ei += 2;
    }
    loop(); serialEventRun();
  }
  fclose(g_out); fprintf(stderr, "%lu frames\n", nframes); return 0;
}
