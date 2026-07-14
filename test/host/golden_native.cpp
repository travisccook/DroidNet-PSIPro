// Golden-frame capture driver. Compiles Neil Hutchison's src/main.cpp
// UNMODIFIED against the host mocks and records every FastLED.show() frame.
// The golden contract: 1 kHz ideal millis, zero-cost show, cold-boot state,
// one process per capture (no cross-mode state leakage).
#include "native_mocks/Arduino.h"
#include "native_mocks/FastLED.h"
#include "native_mocks/EEPROM.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../src/main.cpp"  // the firmware, verbatim

static FILE* g_out = nullptr;
static uint32_t g_frameCount = 0;

static void frameHook(const CRGB* leds_, int n, uint8_t scale) {
  fwrite(&g_mock_millis, sizeof(uint32_t), 1, g_out);
  fputc(scale, g_out);
  fwrite(leds_, sizeof(CRGB), (size_t)n, g_out);
  g_frameCount++;
}

int main(int argc, char** argv) {
  const char* cmd = nullptr;
  const char* outPath = nullptr;
  const char* jumper = "front";
  unsigned long durMs = 10000, seed = 1;
  int pot = 512;
  unsigned ee0 = 0xFF, ee1 = 0xFF, ee2 = 0xFF;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--cmd")) cmd = argv[++i];
    else if (!strcmp(argv[i], "--ms")) durMs = strtoul(argv[++i], 0, 10);
    else if (!strcmp(argv[i], "--jumper")) jumper = argv[++i];
    else if (!strcmp(argv[i], "--pot")) pot = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--seed")) seed = strtoul(argv[++i], 0, 10);
    else if (!strcmp(argv[i], "--eeprom")) sscanf(argv[++i], "%x,%x,%x", &ee0, &ee1, &ee2);
    else if (!strcmp(argv[i], "--out")) outPath = argv[++i];
    else { fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
  }
  if (!cmd || !outPath) { fprintf(stderr, "need --cmd and --out\n"); return 2; }
  if (durMs >= 0x80000000UL) { fprintf(stderr, "duration too long\n"); return 2; }

  g_avr_next = (uint32_t)seed;
  g_mock_analog = pot;
  g_mock_pins[JUMP_FRONT_REAR] = strcmp(jumper, "rear") ? 1 : 0;
  // EEPROM image at the sketch's three config cells (include/config.h:349-352).
  // The placeholders in the task brief (ALWAYS_ON_CONFIG_ADDRESS,
  // INTERNAL_BRIGHTNESS_CONFIG_ADDRESS, GLOBAL_BRIGHTNESS_CONFIG_ADDRESS) do not
  // exist verbatim; config.h declares three plain `int` address variables instead:
  //   alwaysOnAddress = 0, externalPOTAddress = 1, internalBrightnessAddress = 2
  // ee0/ee1/ee2 are mapped by ADDRESS ORDER (0,1,2), matching both their declaration
  // order and the EEPROM.write() call order in main.cpp's P-command handler.
  EEPROM.mem[alwaysOnAddress] = (uint8_t)ee0;
  EEPROM.mem[externalPOTAddress] = (uint8_t)ee1;
  EEPROM.mem[internalBrightnessAddress] = (uint8_t)ee2;

  g_out = fopen(outPath, "wb");
  if (!g_out) { perror(outPath); return 2; }
  fprintf(g_out, "PSIGOLD1 cmd=%s seed=%lu clock=1ms dur_ms=%lu jumper=%s pot=%d eeprom=%02x,%02x,%02x\nFRAMES\n",
          cmd, seed, durMs, jumper, pot, ee0, ee1, ee2);

  g_frame_hook = frameHook;  // capture setup()-time frames too
  setup();
  char line[32];
  snprintf(line, sizeof line, "%s\r", cmd);
  Serial1.feed(line);
  for (uint32_t t = 1; t <= durMs; t++) {
    g_mock_millis = t;
    loop();
    serialEventRun();  // the Arduino core calls this after every loop()
  }
  fclose(g_out);
  fprintf(stderr, "%s: %u frames\n", outPath, g_frameCount);
  return 0;
}
