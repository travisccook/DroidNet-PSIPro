// Host EEPROM mock: fresh-chip 0xFF image, seedable by the capture driver.
#pragma once
#include <stdint.h>
#include <string.h>
struct EEPROMMock {
  uint8_t mem[1024];
  EEPROMMock() { memset(mem, 0xFF, sizeof mem); }
  uint8_t read(int a) { return mem[a]; }
  void write(int a, uint8_t v) { mem[a] = v; }
};
inline EEPROMMock EEPROM;
