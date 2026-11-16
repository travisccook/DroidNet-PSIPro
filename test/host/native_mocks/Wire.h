#pragma once
// The I2C slave, as far as the firmware sees it: bytes a master sent, read from the receive
// callback. A test queues a line with feed() and calls the callback itself (the TWI interrupt).
struct MockWire {
  char rx[128]; int head = 0, tail = 0;
  void begin(int) {}
  void onReceive(void (*)(int)) {}
  void feed(const char* s) { head = tail = 0; while (*s && tail < (int)sizeof rx) rx[tail++] = *s++; }
  int available() { return tail - head; }
  int read() { return head < tail ? (unsigned char)rx[head++] : -1; }
};
inline MockWire Wire;
