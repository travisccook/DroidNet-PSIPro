// Host checks for src/wdp/wdp_fork.h's identity: the type, fw and caps the PSI really sends build
// a valid line for both roles in the fork's own WDP_LINE_MAX (sized to the front line, not the
// core's 128), with the longest fw git gives a build. Fork-local (not shared with the RSeries or
// Flthy forks).
// Part of the DroidNet PSI Pro fork. Copyright (c) 2026 Travis Cook. SPDX-License-Identifier: MIT

// The longest fw scripts/wdp_fw.py gives a build: a 7-char sha plus "-dirty".
#define WDP_FW "abcdef0-dirty"
#define WDP_FORK_IDENTITY_ONLY   // the identity alone: no Arduino, no serialPort
#include "../../src/wdp/wdp_fork.h"
#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0, g_total = 0;
#define CHECK(cond) do { g_total++; if(!(cond)){ g_fail++; \
  printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);} } while(0)

static const char kFront[] =
    "@WDP1 {\"type\":\"PSI Front\",\"fw\":\"abcdef0-dirty\",\"caps\":[\"droidnet.contract\"]}\n";
static const char kRear[] =
    "@WDP1 {\"type\":\"PSI Rear\",\"fw\":\"abcdef0-dirty\",\"caps\":[\"droidnet.contract\"]}\n";

static void check_line(const char* type, const char* want, size_t wantLen) {
  char out[WDP_LINE_MAX];
  uint8_t n = wdpBuildLine(out, sizeof(out), type, WDP_FW, WDP_FORK_CAPS);
  CHECK(n == wantLen);
  CHECK(std::string(out, n) == std::string(want, wantLen));
  CHECK(n > 0 && out[n - 1] == '\n' && out[n] == '\0');
  printf("    %-9s line (%u bytes): %.*s\\n\n", type, (unsigned)n, n ? (int)n - 1 : 0, out);
}

static void test_the_real_identity_builds_both_lines() {
  CHECK(strcmp(WDP_FORK_TYPE_FRONT, "PSI Front") == 0);
  CHECK(strcmp(WDP_FORK_TYPE_REAR, "PSI Rear") == 0);
  check_line(WDP_FORK_TYPE_FRONT, kFront, sizeof(kFront) - 1);
  check_line(WDP_FORK_TYPE_REAR, kRear, sizeof(kRear) - 1);
}

static void test_the_line_buffer_is_exactly_the_front_line() {
  // 77 bytes and the NUL: the front line fills it, so a byte less would refuse it (and with it
  // every announce); the rear line is one shorter.
  CHECK(WDP_LINE_MAX == sizeof(kFront));
  CHECK(WDP_LINE_MAX == 78);
  char small[WDP_LINE_MAX - 1];
  CHECK(wdpBuildLine(small, sizeof(small), WDP_FORK_TYPE_FRONT, WDP_FW, WDP_FORK_CAPS) == 0);
  CHECK(wdpBuildLine(small, sizeof(small), WDP_FORK_TYPE_REAR, WDP_FW, WDP_FORK_CAPS) == sizeof(kRear) - 1);
}

int main() {
  test_the_real_identity_builds_both_lines();
  test_the_line_buffer_is_exactly_the_front_line();
  printf("%s  %d/%d checks passed\n", g_fail ? "FAILURES" : "OK", g_total - g_fail, g_total);
  return g_fail ? 1 : 0;
}
