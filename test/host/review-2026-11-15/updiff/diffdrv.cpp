// Differential driver: same scripted timeline through (a) Neil's upstream PSIPro.ino @5e80597 and
// (b) the fork's src/main.cpp, both on the fork's own host mocks. Dumps every show() frame.
#include "native_mocks/Arduino.h"
#include "native_mocks/FastLED.h"
#include "native_mocks/EEPROM.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef UPSTREAM
#include "functions.h"
#include FW
#else
#include FW
#endif
static FILE* g_out; static unsigned long nframes = 0;
static void hook(const CRGB* l, int n, uint8_t scale) {
  fprintf(g_out, "%u %u ", (unsigned)g_mock_millis, scale);
  for (int i = 0; i < n; i++) fprintf(g_out, "%02x%02x%02x", l[i].r, l[i].g, l[i].b);
  fputc('\n', g_out); nframes++;
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
    while (ei + 1 < argc && strtoul(argv[ei], 0, 10) == t) { Serial1.feed(argv[ei + 1]); Serial1.feed("\r"); ei += 2; }
    loop(); serialEventRun();
  }
  fclose(g_out); fprintf(stderr, "%lu frames\n", nframes); return 0;
}
