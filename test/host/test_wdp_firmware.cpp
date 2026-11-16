// Host check of the WDP device announce as the firmware runs it: src/main.cpp compiled verbatim
// against the golden harness's mocks (like golden_native.cpp), with Serial1 modelled as an AVR
// HardwareSerial's 63-byte TX buffer draining at about 1 byte/ms (9600 baud). Proves:
//   * setup() schedules the first line 0.5-3 s after boot, then one every 25-30 s;
//   * no loop() pass writes more than availableForWrite() said there was room for;
//   * a Q that arrives mid-line is answered after the whole line, never inside it, in a pass
//     that has room for the whole reply, and the tail of the line is not sent twice;
//   * the type follows the front/rear jumper at each announce;
//   * the seed differs between the front and rear PSI, and between chips.
// Part of the DroidNet PSI Pro fork. Copyright (c) 2026 Travis Cook. SPDX-License-Identifier: MIT
#include "native_mocks/Arduino.h"
// (native_mocks/FastLED.h is not included here: main.cpp names the S9 frame gate and then includes
// <FastLED.h> itself. ContractPSI.h refuses to build if the mock was compiled without the gate.)
#include "native_mocks/EEPROM.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <utility>

#include "../../src/main.cpp"  // the firmware, verbatim

static int g_fail = 0, g_total = 0;
#define CHECK(cond) do { g_total++; if(!(cond)){ g_fail++; \
  printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);} } while(0)

static std::string line(const char* type) {
  return std::string("@WDP1 {\"type\":\"") + type + "\",\"fw\":\"" WDP_FW "\",\"caps\":[\"droidnet.contract\",\"" WDP_FORK_CONTRACT "\"]}\n";
}

// One millisecond of the board: room in the TX buffer, loop(), the core's serialEventRun(), then
// the UART drains a byte. Returns the bytes loop() wrote (the announce and the Q reply: both are
// written from loop(), never from the parser) and counts every pass that wrote more than the room
// it was offered.
static int g_pending = 0;    // bytes in the modelled TX buffer
static int g_overRoom = 0;   // loop() passes that wrote past availableForWrite()
static int tick(uint32_t t) {
  g_mock_millis = t;
  Serial1.txRoom = g_pending >= 63 ? 0 : 63 - g_pending;
  const int room = Serial1.txRoom, before = Serial1.txLen;
  loop();
  const int wrote = Serial1.txLen - before;
  if (wrote > room) g_overRoom++;
  serialEventRun();
  g_pending += Serial1.txLen - before;   // a blocking write past the room: the time it waited
  if (g_pending > 0) g_pending--;
  return wrote;
}

static void test_seed_differs_by_role_and_by_chip() {
  uint8_t row[sizeof g_mock_sigrow];
  memcpy(row, g_mock_sigrow, sizeof row);
  const uint32_t front = wdpForkSeed(true), rear = wdpForkSeed(false);
  CHECK(front != rear);
  const uint8_t saved = g_mock_sigrow[0x17];
  g_mock_sigrow[0x17] ^= 0x01;                       // a chip one bit away
  CHECK(wdpForkSeed(true) != front);
  CHECK(wdpForkSeed(false) != rear);
  g_mock_sigrow[0x17] = saved;
  std::swap(g_mock_sigrow[0x14], g_mock_sigrow[0x15]);   // two bytes swapped (an X/Y-style pair)
  CHECK(wdpForkSeed(true) != front);
  std::swap(g_mock_sigrow[0x14], g_mock_sigrow[0x15]);
  CHECK(wdpForkSeed(true) == front);                 // and it is a pure function of the row
  for (int a = 0x0E; a < 0x18; a++) g_mock_sigrow[a] = 0xFF;   // a blank serial: roles still differ
  CHECK(wdpForkSeed(true) != wdpForkSeed(false));
  memcpy(g_mock_sigrow, row, sizeof row);
}

int main() {
  test_seed_differs_by_role_and_by_chip();

  g_mock_pins[JUMP_FRONT_REAR] = 1;   // jumper open: front
  g_mock_millis = 0;
  setup();

  // The first line: due 0.5-3 s after boot, out in two pieces (63 bytes, then the rest).
  uint32_t t = 1, firstAt = 0;
  for (; t <= 3000 && !firstAt; t++)
    if (tick(t)) firstAt = t;
  CHECK(firstAt >= 500 && firstAt <= 3000);
  const std::string front = line("PSI Front");
  CHECK(Serial1.txLen == 63);
  CHECK(std::string(Serial1.txLog) == front.substr(0, 63));

  // A Q lands while the rest of that line is in flight. The reply is owed, not written: the
  // parser never waits on the port. It goes out after the line, whole, in a pass that has room
  // for all of it (57 bytes at most), which is some 50 ms later as the UART drains.
  Serial1.feed("!PFQ:r=0\r");
  tick(t++);
  CHECK(std::string(Serial1.txLog).find("q:ver=") == std::string::npos);   // not from the parser
  uint32_t replyAt = 0;
  for (uint32_t end = t + 300; t < end; t++) { tick(t); if (!replyAt && strstr(Serial1.txLog, "q:ver=")) replyAt = t; }
  std::string got(Serial1.txLog);
  CHECK(got.compare(0, front.size(), front) == 0);   // the line first, unbroken
  const std::string reply = got.substr(front.size());
  CHECK(reply.compare(0, 11, ";S0!PFq:ver") == 0 && reply.back() == '\r');
  CHECK(replyAt != 0);
  const size_t afterAck = got.size();
  for (; t < firstAt + 25000; t++) tick(t);
  CHECK(Serial1.txLen == (int)afterAck);             // the tail was not sent a second time, nor the reply

  // The next line 25-30 s after the first; the jumper is fitted before it: "PSI Rear".
  g_mock_pins[JUMP_FRONT_REAR] = 0;
  uint32_t secondAt = 0;
  for (; t <= firstAt + 30000 + 1; t++)
    if (tick(t) && !secondAt) secondAt = t;
  CHECK(secondAt >= firstAt + 25000 && secondAt <= firstAt + 30000);
  for (uint32_t end = t + 100; t < end; t++) tick(t);   // let the second piece out
  got = Serial1.txLog;
  CHECK(got.substr(afterAck) == line("PSI Rear"));
  CHECK(Serial1.txBiggest <= 63);
  CHECK(g_overRoom == 0);

  printf("    front %u B + reply %u B (%u ms after the Q), rear %u B; first line at %u ms, second %u ms later\n",
         (unsigned)front.size(), (unsigned)reply.size(), (unsigned)(replyAt - firstAt), (unsigned)line("PSI Rear").size(),
         (unsigned)firstAt, (unsigned)(secondAt - firstAt));
  printf("%s  %d/%d checks passed\n", g_fail ? "FAILURES" : "OK", g_total - g_fail, g_total);
  return g_fail ? 1 : 0;
}
