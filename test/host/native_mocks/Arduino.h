// Host mock of the Arduino AVR surface the PSI Pro sketch touches.
// Golden-frame harness support — test instrument, not production code.
// NOTE: defines NOTHING that src/main.cpp defines (ODR audit: keep it that way).
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef uint8_t byte;
typedef bool boolean;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

// ---- clock: 1 kHz ideal millis, driven by the capture loop ----
inline uint32_t g_mock_millis = 0;
inline unsigned long millis() { return g_mock_millis; }
inline unsigned long micros() { return g_mock_millis * 1000UL; }
inline void delay(unsigned long ms) { g_mock_millis += (uint32_t)ms; }

// ---- exact avr-libc random(): Park–Miller minimal standard.
// Constants verified against the shipped firmware.elf disassembly
// (16807 / 127773 / -2836 / zero-seed 123459876, default seed 1).
// NEVER call host libc random()/rand() anywhere in the harness.
inline uint32_t g_avr_next = 1;
inline long avr_random_raw() {
  int32_t x = (int32_t)g_avr_next;
  if (x == 0) x = 123459876L;
  int32_t hi = x / 127773L;
  int32_t lo = x % 127773L;
  x = 16807L * lo - 2836L * hi;
  if (x < 0) x += 0x7FFFFFFFL;
  g_avr_next = (uint32_t)x;
  return (long)x;
}
inline void randomSeed(unsigned long s) { if (s) g_avr_next = (uint32_t)s; }
// Arduino WMath wrappers, verbatim semantics.
inline long random(long howbig) {
  if (howbig == 0) return 0;
  return avr_random_raw() % howbig;
}
inline long random(long howsmall, long howbig) {
  if (howsmall >= howbig) return howsmall;
  long diff = howbig - howsmall;
  return random(diff) + howsmall;
}

// ---- pins ----
inline uint8_t g_mock_pins[32] = {0};
inline void pinMode(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t pin) { return g_mock_pins[pin]; }
inline int g_mock_analog = 512;
inline int analogRead(uint8_t) { return g_mock_analog; }
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
inline void sei() {}
inline void cli() {}
// The status register, as <avr/io.h> provides it: the WDP seed saves it around cli().
inline uint8_t SREG = 0x80;

// ---- PROGMEM ----
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t*)(a))
// Deliberately a ONE-BYTE read. The sketch calls pgm_read_dword on byte
// matrices and truncates into a byte; on AVR the observable value is the
// addressed byte, and a true 4-byte host read is an out-of-bounds ASan trap
// at row ends (main.cpp displayMatrixColor rows 2-6).
#define pgm_read_dword(a) ((uint32_t)(*(const uint8_t*)(a)))
// Pointer reads (VM program/bitmap tables) MUST use pgm_read_ptr, never
// pgm_read_word — host pointers are 64-bit.
#define pgm_read_ptr(a) (*(void* const*)(a))

// ---- serial ----
class Stream {
 public:
  uint8_t rx[512];
  int rxHead = 0, rxTail = 0;
  char txLog[1024];
  int txLen = 0;
  void begin(unsigned long) {}
  int available() { return rxTail - rxHead; }
  int read() { return (rxHead < rxTail) ? rx[rxHead++] : -1; }
  void feed(const char* s) { while (*s && rxTail < (int)sizeof rx) rx[rxTail++] = (uint8_t)*s++; }
  void print(const char* s) { while (*s && txLen < (int)sizeof txLog - 1) { txLog[txLen++] = *s++; } txLog[txLen] = 0; }
  void print(char c) { if (txLen < (int)sizeof txLog - 1) { txLog[txLen++] = c; txLog[txLen] = 0; } }
  void println(const char* s) { print(s); print('\n'); }
  void println() { print('\n'); }
  // Print's buffer write and availableForWrite(), for the WDP announce (src/wdp/wdp_fork.h).
  // txRoom is what availableForWrite() reports: 63, an idle AVR HardwareSerial's free TX room,
  // unless a test models the buffer filling and draining. txWrites/txBiggest count the writes.
  int txRoom = 63;
  int txWrites = 0, txBiggest = 0;
  int availableForWrite() { return txRoom; }
  size_t write(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n && txLen < (int)sizeof txLog - 1; i++) txLog[txLen++] = (char)p[i];
    txLog[txLen] = 0;
    txWrites++;
    if ((int)n > txBiggest) txBiggest = (int)n;
    return n;
  }
};
using HardwareSerial = Stream;
inline Stream Serial;
inline Stream Serial1;
