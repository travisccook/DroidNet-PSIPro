// Real-firmware lifecycle probe: the fork's src/main.cpp VERBATIM (setup/loop renamed) on the
// simulated ATmega32U4, taken through a show, X, M:v=idle, a native take-over, a pulse over the
// native look, a watchdog expiry and a V that does not match.
//
// Why it exists: the host timeline scenarios check the same things, but on mocks with an ideal
// clock (one loop pass a millisecond, a free show()). What the panel returns to after a show
// depends on WHEN upstream's loop samples lastPSIeventCode, patternRunning and firstTime against
// its 26 ms frame gate, so these checkpoints run on the real timer, real interrupts and a real
// 1.5 ms masked frame.
//
// Each row is "<ms>|<line>". Just before a row's line is fed, the probe prints what is on the
// panel: "cp<n> t=<ms> lit=<n> green=<n> white=<n> code=<lastPSIeventCode> run=<patternRunning>".
// A line that starts with '!' goes to parseContract(), any other to the native parseCommand().
// check_lifecycle.py holds the expectations. The table is in flash: the firmware leaves little SRAM.
#include <Arduino.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#define setup fw_setup
#define loop fw_loop
#include "../../../../src/main.cpp"
#undef setup
#undef loop

static const char kRows[] PROGMEM =
  "1500|!**M:v=show\n"                 // cp0  the idle swipe
  "2500|!P*A:i=solid,c=00ff00\n"       // cp1  armed, nothing shown yet: still the swipe
  "3500|!P*X\n"                        // cp2  the live look: all green
  "4500|!**M:v=idle\n"                 // cp3  X: dark, and it stayed dark
  "6500|!**M:v=show\n"                 // cp4  2 s after idle: the swipe, and it stayed
  "6600|!P*A:i=native:15\n"            // cp5  armed again: the swipe
  "8600|0T1\n"                         // cp6  the show's native look: Knight Rider
  "10600|!P*P:c=ffffff,d=200\n"        // cp7  2 s after the native command: the swipe
  "10700|\n"                           // cp8  the pulse over the native look: all white
  "11500|!**M:v=show\n"                // cp9  after the pulse: the swipe again
  "11600|!P*A:i=solid,c=00ff00\n"      // cp10 armed
  "12600|!**M:v=idle\n"                // cp11 all green
  "14600|!**M:v=show,wd=2\n"           // cp12 2 s after idle: the swipe
  "14700|!P*A:i=solid,c=00ff00\n"      // cp13 armed, with a 2 s watchdog
  "16000|\n"                           // cp14 1.3 s of silence: the look holds
  "17500|!**M:v=show\n"                // cp15 2.8 s of silence: the watchdog gave the panel back
  "17600|!P*A:i=solid,c=00ff00,at=0\n" // cp16 armed, a section loaded
  "17700|!**C:bpm=120,bpb=4,beat=0\n"  // cp17 before Play: still the swipe
  "18700|!PFV:n=2,k=0000\n"            // cp18 playing: all green
  "20700|\n";                          // cp19 2 s after a V that does not match: the swipe
static const char* gRow = kRows;
static uint8_t gCp = 0;

static uint32_t rowTime() {
  uint32_t t = 0;
  for (const char* p = gRow;; p++) { char c = (char)pgm_read_byte(p); if (c < '0' || c > '9') break; t = t * 10 + (uint8_t)(c - '0'); }
  return t;
}
static void checkpoint() {
  uint8_t lit = 0, green = 0, white = 0;
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    const CRGB& c = leds[i];
    if (c.r | c.g | c.b) lit++;
    if (c.r == 0 && c.g == 255 && c.b == 0) green++;
    if (c.r == 255 && c.g == 255 && c.b == 255) white++;
  }
  Serial1.print(F("cp")); Serial1.print(gCp++);
  Serial1.print(F(" t=")); Serial1.print(millis());
  Serial1.print(F(" lit=")); Serial1.print(lit);
  Serial1.print(F(" green=")); Serial1.print(green);
  Serial1.print(F(" white=")); Serial1.print(white);
  Serial1.print(F(" code=")); Serial1.print(lastPSIeventCode);
  Serial1.print(F(" run=")); Serial1.println(patternRunning ? 1 : 0);
  Serial1.flush();
}
void setup() { fw_setup(); }
void loop() {
  fw_loop();
  if (pgm_read_byte(gRow) == 0) return;
  if (millis() < rowTime()) return;
  checkpoint();
  char buf[40]; uint8_t n = 0; const char* p = gRow;
  while ((char)pgm_read_byte(p) != '|') p++;
  for (p++;; p++) { char c = (char)pgm_read_byte(p); if (c == '\n') break; if (n < sizeof buf - 1) buf[n++] = c; }
  buf[n] = 0;
  gRow = p + 1;
  if (n) { if (buf[0] == '!') parseContract(buf); else parseCommand(buf); }
  if (pgm_read_byte(gRow) == 0) { Serial1.println(F("lifecycle done")); Serial1.flush(); cli(); set_sleep_mode(SLEEP_MODE_PWR_DOWN); sleep_enable(); sleep_cpu(); }   // simavr exits
}
