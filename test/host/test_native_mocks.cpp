// Known-answer tests for the mock layer. The PRNG vector is the canonical
// minstd0 sequence from seed 1 — if this fails, every golden is a lie.
#include "native_mocks/Arduino.h"
#include "native_mocks/EEPROM.h"
#include <assert.h>
#include <stdio.h>

int main() {
  // Park–Miller from seed 1: canonical first five raw outputs.
  long expect[5] = {16807L, 282475249L, 1622650073L, 984943658L, 1144108930L};
  for (int i = 0; i < 5; i++) assert(avr_random_raw() == expect[i]);
  // Arduino wrapper semantics.
  g_avr_next = 1;
  assert(random(0) == 0);              // howbig==0 -> 0, consumes no draw
  assert(random(6) == 16807L % 6);     // modulo of the first raw draw
  assert(random(220, 250) == 220 + (282475249L % 30));
  // map() must use long math (AVR parity).
  assert(map(512, 0, 1023, 0, 255) == 127);
  // millis is externally driven.
  g_mock_millis = 42;
  assert(millis() == 42);
  // EEPROM fresh chip reads 0xFF.
  assert(EEPROM.read(0) == 0xFF);
  EEPROM.write(3, 7);
  assert(EEPROM.read(3) == 7);
  // pgm_read_dword is a one-byte read by design.
  static const uint8_t m[2] = {0x2A, 0xFF};
  assert(pgm_read_dword(&m[0]) == 0x2AUL);
  // Serial FIFO round-trip.
  Serial1.feed("0T2\r");
  assert(Serial1.available() == 4);
  assert(Serial1.read() == '0');
  puts("test_native_mocks: OK");
  return 0;
}
