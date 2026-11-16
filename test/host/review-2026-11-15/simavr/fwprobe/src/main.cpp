#include <Arduino.h>
#include <EEPROM.h>
// Real-firmware millis-rate probe: the fork's src/main.cpp VERBATIM (setup/loop renamed), driven
// with contract lines through its own parseContract(), measured against Timer1. Scratch only.
#define setup fw_setup
#define loop fw_loop
#include "../../../../../../src/main.cpp"
#undef setup
#undef loop
volatile uint16_t t1ovf = 0;
ISR(TIMER1_OVF_vect) { t1ovf++; }
static uint32_t trueTicks() { uint8_t s = SREG; cli(); uint16_t t = TCNT1; uint16_t o = t1ovf; if ((TIFR1 & _BV(TOV1)) && t < 32768) o++; SREG = s; return ((uint32_t)o << 16) | t; }
struct Cfg { const char* name; const char* l1; const char* l2; const char* l3; };
static const Cfg cfgs[] = {
  {"warmup", "!P*X", 0, 0},
  {"idle-swipe", "!P*X", 0, 0},
  {"solid", "!P*A:i=solid,c=202020,b=20", 0, 0},
  {"solid+pump", "!P*A:i=solid,c=202020,b=20,m=128,am=2", "!**C:bpm=120,bpb=4,beat=0", 0},
  {"scored-comet+accent", "!**M:v=show", "!P*A:i=comet,c=202020,at=0,m=128,am=2,ae=solid,ac=101010", "!**C:bpm=120,bpb=4,beat=0"},
  {"twinkle", "!P*A:i=twinkle,c=202020,b=20,m=0", 0, 0},
  {"wipe+pump", "!P*A:i=wipe,c=202020,b=20,m=128,am=2", "!**C:bpm=120,bpb=4,beat=0", 0},
  {"chase", "!P*A:i=chase,c=202020,b=20,m=0", 0, 0},
  {"gradient", "!P*A:i=gradient,b=20,m=0", 0, 0},
  {"colorcycle", "!P*A:i=colorcycle,b=20,m=0", 0, 0},
  {"meter", "!P*A:i=meter,b=20,m=0", 0, 0},
  {"scan", "!P*A:i=scan,c=202020,b=20,m=0", 0, 0},
  {"native1", "!P*A:i=native:1,b=20,m=0", 0, 0},
  {"native15", "!P*A:i=native:15,b=20,m=0", 0, 0},
};
static uint8_t ci = 0; static uint32_t m0, t0; static bool started = false;
static void startCfg() {
  char buf[96];
  parseContract("!P*X");
  const char* ls[3] = {cfgs[ci].l1, cfgs[ci].l2, cfgs[ci].l3};
  for (uint8_t i = 0; i < 3; i++) if (ls[i]) { strncpy(buf, ls[i], 95); buf[95] = 0; parseContract(buf); }
  m0 = millis(); t0 = trueTicks();
}
void setup() {
  fw_setup();
  TCCR1A = 0; TCCR1B = _BV(CS11) | _BV(CS10); TIMSK1 = _BV(TOIE1);
}
void loop() {
  if (!started) { started = true; startCfg(); }
  fw_loop();
  uint32_t t = trueTicks();
  if ((t - t0) >= 2000000UL) {            // 8 s of true time
    uint32_t dm = millis() - m0; uint32_t dus = (t - t0) * 4UL;
    Serial1.print(cfgs[ci].name); Serial1.print(F(": millis=")); Serial1.print(dm); Serial1.print(F(" true_us=")); Serial1.print(dus);
    Serial1.print(F(" err_ppm=")); Serial1.println((long)(((float)dm * 1000.0f - (float)dus) / (float)dus * 1e6f)); Serial1.flush();
    if (++ci >= sizeof(cfgs) / sizeof(cfgs[0])) { Serial1.println(F("fwprobe done")); Serial1.flush(); for (;;) {} }
    startCfg();
  }
}
