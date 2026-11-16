// Real-firmware millis-rate probe: the fork's src/main.cpp VERBATIM (setup/loop renamed), driven
// with contract lines through its own parseContract(), measured against Timer1 (which the masked
// LED frame cannot disturb). One line per look: "<name>: millis=<ms> true_us=<us> err_ppm=<n>".
// The firmware leaves under 300 B of stack, so the probe keeps its table in flash and nothing in
// SRAM but 15 bytes of state. Build with board_upload.maximum_size = 32256: simavr has no bootloader.
#include <Arduino.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#define setup fw_setup
#define loop fw_loop
#include "../../../../src/main.cpp"
#undef setup
#undef loop
volatile uint16_t t1ovf = 0;
ISR(TIMER1_OVF_vect) { t1ovf++; }
static uint32_t trueTicks() {          // 4 us a tick
  uint8_t s = SREG; cli();
  uint16_t t = TCNT1; uint16_t o = t1ovf;
  if ((TIFR1 & _BV(TOV1)) && t < 32768) o++;
  SREG = s;
  return ((uint32_t)o << 16) | t;
}
// name|line|line|line, one look per row. Every row starts from M:v=idle.
static const char kLooks[] PROGMEM =
  "warmup|\n"
  "idle-swipe|\n"
  "solid|!P*A:i=solid,c=202020\n"
  "solid+pump|!P*A:i=solid,c=202020,m=128,am=2|!**C:bpm=120,bpb=4,beat=0\n"
  "scored-comet+accent|!**M:v=show|!P*A:i=comet,c=202020,at=0,m=128,am=2,ae=solid,ac=101010|!**C:bpm=120,bpb=4,beat=0\n"
  "twinkle|!P*A:i=twinkle,c=202020\n"
  "wipe+pump|!P*A:i=wipe,c=202020,m=128,am=2|!**C:bpm=120,bpb=4,beat=0\n"
  "chase|!P*A:i=chase,c=202020\n"
  "gradient|!P*A:i=gradient\n"
  "colorcycle|!P*A:i=colorcycle\n"
  "meter|!P*A:i=meter\n"
  "scan|!P*A:i=scan,c=202020\n"
  "native1|!P*A:i=native:1\n"
  "native15|!P*A:i=native:15\n";
static const char* gRow = kLooks;      // the row being measured (flash address)
static uint32_t m0, t0;
static bool started = false;
static void startRow() {
  char buf[96]; uint8_t n = 0; bool name = true;
  strcpy_P(buf, PSTR("!**M:v=idle")); parseContract(buf);
  delay(200);                          // past the 170 ms live-look hold
  for (const char* p = gRow;; p++) {
    char c = (char)pgm_read_byte(p);
    if (c == '|' || c == '\n') {
      buf[n] = 0;
      if (!name && n) parseContract(buf);
      name = false; n = 0;
      if (c == '\n') break;
    } else if (n < sizeof buf - 1) buf[n++] = c;
  }
  m0 = millis(); t0 = trueTicks();
}
void setup() {
  fw_setup();
  TCCR1A = 0; TCCR1B = _BV(CS11) | _BV(CS10); TIMSK1 = _BV(TOIE1);
}
void loop() {
  if (!started) { started = true; startRow(); }
  fw_loop();
  uint32_t t = trueTicks();
  if ((t - t0) < 4000000UL) return;    // 16 s of true time a look: 1 ms of millis() is 62 ppm
  uint32_t dm = millis() - m0, dus = (t - t0) * 4UL;
  for (const char* p = gRow;; p++) { char c = (char)pgm_read_byte(p); if (c == '|' || c == '\n') break; Serial1.write(c); }
  Serial1.print(F(": millis=")); Serial1.print(dm);
  Serial1.print(F(" true_us=")); Serial1.print(dus);
  Serial1.print(F(" err_ppm=")); Serial1.println((long)(((int32_t)(dm * 1000UL) - (int32_t)dus) / 16));   // dus is 16,000,0xx
  Serial1.flush();
  while (pgm_read_byte(gRow) != '\n') gRow++;
  gRow++;
  if (pgm_read_byte(gRow) == 0) { Serial1.println(F("fwprobe done")); Serial1.flush(); cli(); set_sleep_mode(SLEEP_MODE_PWR_DOWN); sleep_enable(); sleep_cpu(); }   // asleep with interrupts off: simavr exits
  startRow();
}
