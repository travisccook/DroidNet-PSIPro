#pragma once
struct MockWire { void begin(int) {} void onReceive(void (*)(int)) {} int available() { return 0; } int read() { return -1; } };
inline MockWire Wire;
