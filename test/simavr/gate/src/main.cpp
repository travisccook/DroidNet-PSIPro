// Real-firmware S9 probe: the fork's src/main.cpp VERBATIM (setup/loop renamed) on the simulated
// ATmega32U4, fed two of the hostile line streams from test/host/test_property.cpp while simavr
// traces the LED data pin. check_gate.py decodes every frame off the wire and counts dark-to-lit
// onsets, so the limit is checked on what the real driver sends, with the real timer, and not on
// a mock of show().
//
//   1000..5000 ms   M:v=show, then every 170 ms: A solid white, X 50 ms later, A solid white 50 ms
//                   after that (a host relighting the panel right after a blackout)
//   5000..8000 ms   every 100 ms: X, then M:v=idle 50 ms later (dark, then the native look)
//
// The owner's brightness is set to 100 in EEPROM first, so the ceiling is known to the checker.
#include <Arduino.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#define setup fw_setup
#define loop fw_loop
#include "../../../../src/main.cpp"
#undef setup
#undef loop

static const char kShow[]  PROGMEM = "!**M:v=show";
static const char kWhite[] PROGMEM = "!PFA:i=solid,c=ffffff";
static const char kStop[]  PROGMEM = "!PFX";
static const char kIdle[]  PROGMEM = "!PFM:v=idle";
static void send(const char* flashLine) {
  char buf[32];
  strcpy_P(buf, flashLine);
  parseContract(buf);
}
static uint32_t gBase = 1000;
static uint8_t gStep = 0, gPhase = 0;      // phase 0: not started, 1: X between A lines, 2: X and idle, 3: done

void setup() {
  EEPROM.update(externalPOTAddress, 1);            // internal brightness, not the pot
  EEPROM.update(internalBrightnessAddress, 100);
  fw_setup();
}
void loop() {
  fw_loop();
  const uint32_t now = millis();
  if (gPhase == 0 && now >= 1000) { send(kShow); gPhase = 1; }
  if (gPhase == 1) {
    if (now >= 5000) { gPhase = 2; gBase = 5000; gStep = 0; return; }
    static const uint8_t at[3] = { 0, 50, 100 };
    if (now - gBase >= at[gStep]) {
      send(gStep == 1 ? kStop : kWhite);
      if (++gStep == 3) { gStep = 0; gBase += 170; }
    }
  } else if (gPhase == 2) {
    if (now >= 8000) {                               // done: stop the simulator (asleep with interrupts off)
      gPhase = 3; Serial1.println(F("gate done")); Serial1.flush();
      cli(); set_sleep_mode(SLEEP_MODE_PWR_DOWN); sleep_enable(); sleep_cpu();
    }
    static const uint8_t at[2] = { 0, 50 };
    if (now - gBase >= at[gStep]) {
      send(gStep == 0 ? kStop : kIdle);
      if (++gStep == 2) { gStep = 0; gBase += 100; }
    }
  }
}
