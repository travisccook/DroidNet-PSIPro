// Host unit tests for wdp_announce_core.h — no external deps, tiny assert harness.
// Shared byte-identically by the RSeries, PSI Pro and FlthyHPs forks.
// Part of the DroidNet board forks. Copyright (c) 2026 Travis Cook. LGPL-2.1-only.
#include "../../src/wdp/wdp_announce_core.h"
#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0, g_total = 0;
#define CHECK(cond) do { g_total++; if(!(cond)){ g_fail++; \
  printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);} } while(0)

// Build into the announcer's own buffer size; "" when the core refuses the identity.
static std::string build(const char* type, const char* fw, const char* caps) {
  char out[WDP_LINE_MAX];
  uint8_t n = wdpBuildLine(out, sizeof(out), type, fw, caps);
  return n ? std::string(out, n) : std::string();
}

// Build into a buffer big enough for any field lengths, to test the field limits on their own.
static std::string buildRoomy(const char* type, const char* fw, const char* caps) {
  char out[255];
  uint8_t n = wdpBuildLine(out, sizeof(out), type, fw, caps);
  return n ? std::string(out, n) : std::string();
}

// Run the announcer from `now` to `until` in 1 ms steps with txFree bytes of room each pass,
// collecting every byte it hands out. Returns the bytes; *firstMs is when the first byte went.
static std::string drain(WdpAnnouncer& a, uint32_t now, uint32_t until, int txFree,
                         const char* type, uint32_t* firstMs = 0) {
  std::string got;
  for (uint32_t t = now; t != until; t++) {
    const char* p = 0;
    uint8_t n = wdpTick(a, t, type, "abc1234", "droidnet.contract", txFree, &p);
    if (n && got.empty() && firstMs) *firstMs = t;
    if (n) got.append(p, n);
  }
  return got;
}

// ── wdpBuildLine ──────────────────────────────────────────────────────────────

static void test_line_without_caps() {
  CHECK(build("PSI Front", "fd352e7", "") ==
        "@WDP1 {\"type\":\"PSI Front\",\"fw\":\"fd352e7\"}\n");
}

static void test_caps_become_a_json_array() {
  CHECK(build("Flthy HP Controller", "75755de", "droidnet.contract hp.servo hp.led") ==
        "@WDP1 {\"type\":\"Flthy HP Controller\",\"fw\":\"75755de\","
        "\"caps\":[\"droidnet.contract\",\"hp.servo\",\"hp.led\"]}\n");
}

static void test_null_caps_mean_none() {
  CHECK(build("PSI Rear", "fd352e7", 0) == "@WDP1 {\"type\":\"PSI Rear\",\"fw\":\"fd352e7\"}\n");
}

static void test_fields_at_the_wcb_limits_are_accepted() {
  // The WCB keeps 24 / 27 / 48 characters (WCB_WDP.h WdpDaDevice); exactly that many fit.
  std::string t24(24, 't'), f27(27, 'f'), c48(48, 'c');
  CHECK(!buildRoomy(t24.c_str(), f27.c_str(), c48.c_str()).empty());
}

static void test_fields_past_the_wcb_limits_are_refused() {
  // Sent anyway, the WCB would keep a silently truncated copy for good.
  std::string t25(25, 't'), f28(28, 'f'), c49(49, 'c');
  CHECK(buildRoomy(t25.c_str(), "fw", "").empty());
  CHECK(buildRoomy("type", f28.c_str(), "").empty());
  CHECK(buildRoomy("type", "fw", c49.c_str()).empty());
}

static void test_type_and_fw_are_required() {
  CHECK(build("", "fw", "").empty());
  CHECK(build(0, "fw", "").empty());
  CHECK(build("type", "", "").empty());
  CHECK(build("type", 0, "").empty());
}

static void test_characters_the_wcb_would_mangle_are_refused() {
  // The WCB has no JSON escapes (a quote or backslash ends the value early) and scrubs
  // ',' ']' and control bytes to '_' (WCB_WDP.cpp wdpScrub).
  const char* bad[] = { "a\"b", "a\\b", "a,b", "a]b", "a\tb", "a\nb", ("a\x7f" "b") };
  for (const char* s : bad) {
    CHECK(build(s, "fw", "").empty());
    CHECK(build("type", s, "").empty());
    CHECK(build("type", "fw", s).empty());
  }
}

static void test_utf8_is_accepted() {
  CHECK(build("Sab\xC3\xA9", "1.0", "") == "@WDP1 {\"type\":\"Sab\xC3\xA9\",\"fw\":\"1.0\"}\n");
}

static void test_caps_spacing_must_be_single() {
  // The 48-character check is on the WCB's single-space-joined form, so ours must match it.
  CHECK(build("type", "fw", "a  b").empty());
  CHECK(build("type", "fw", " a").empty());
  CHECK(build("type", "fw", "a ").empty());
}

static void test_a_line_that_does_not_fit_the_buffer_is_refused() {
  char small[20];
  CHECK(wdpBuildLine(small, sizeof(small), "PSI Front", "fd352e7", "") == 0);
  // Exactly fitting (line + NUL) is fine.
  const char* want = "@WDP1 {\"type\":\"P\",\"fw\":\"1\"}\n";
  char exact[64];
  CHECK(wdpBuildLine(exact, strlen(want) + 1, "P", "1", "") == strlen(want));
  CHECK(wdpBuildLine(exact, strlen(want), "P", "1", "") == 0);
}

static void test_the_longest_fork_lines_fit_wdp_line_max() {
  // The real identities, with a 13-character "-dirty" firmware string.
  CHECK(!build("R-Series Logics", "aa45d79-dirty", "droidnet.contract led.logic").empty());
  CHECK(!build("Flthy HP Controller", "75755de-dirty", "droidnet.contract hp.servo hp.led").empty());
  CHECK(!build("PSI Front", "fd352e7-dirty", "droidnet.contract").empty());
}

// ── wdpInit / wdpTick: schedule ───────────────────────────────────────────────

static void test_first_announce_comes_half_a_second_to_three_seconds_after_boot() {
  for (uint32_t seed = 1; seed <= 200; seed++) {
    WdpAnnouncer a; wdpInit(a, seed, 1000);
    uint32_t first = 0;
    std::string got = drain(a, 1000, 1000 + 3001, 255, "PSI Front", &first);
    CHECK(!got.empty());
    CHECK(first >= 1000 + 500 && first <= 1000 + 3000);
  }
}

static void test_announces_repeat_every_25_to_30_seconds() {
  WdpAnnouncer a; wdpInit(a, 7, 0);
  uint32_t prev = 0;
  drain(a, 0, 3001, 255, "PSI Front", &prev);
  for (int i = 0; i < 20; i++) {
    uint32_t next = 0;
    std::string got = drain(a, prev + 1, prev + 30001, 255, "PSI Front", &next);
    CHECK(!got.empty());
    CHECK(next - prev >= 25000 && next - prev <= 30000);
    prev = next;
  }
}

static void test_boards_with_different_seeds_do_not_announce_in_lockstep() {
  // Chained boards that power up together must spread out (Greg's WDP_DEVICE_ANNOUNCE.md).
  bool seen[2501] = {};
  int distinct = 0;
  for (uint32_t seed = 1; seed <= 100; seed++) {
    WdpAnnouncer a; wdpInit(a, seed, 0);
    uint32_t first = 0;
    drain(a, 0, 3001, 255, "PSI Front", &first);
    if (first >= 500 && first <= 3000 && !seen[first - 500]) { seen[first - 500] = true; distinct++; }
  }
  CHECK(distinct >= 90);
}

static void test_a_zero_seed_still_jitters() {
  WdpAnnouncer a; wdpInit(a, 0, 0);
  uint32_t first = 0;
  drain(a, 0, 3001, 255, "PSI Front", &first);
  CHECK(first >= 500 && first <= 3000);
  uint32_t second = 0;
  drain(a, first + 1, first + 30001, 255, "PSI Front", &second);
  CHECK(second - first >= 25000 && second - first <= 30000);
}

static void test_schedule_survives_millis_wrap() {
  // millis() wraps every ~49.7 days; the next announce must still come 25-30 s later.
  WdpAnnouncer a; wdpInit(a, 3, 0xFFFFF000u);
  uint32_t first = 0;
  drain(a, 0xFFFFF000u, 0xFFFFF000u + 3001, 255, "PSI Front", &first);
  CHECK(first != 0);
  uint32_t second = 0;
  std::string got = drain(a, first + 1, first + 30001, 255, "PSI Front", &second);
  CHECK(!got.empty());
  CHECK((uint32_t)(second - first) >= 25000 && (uint32_t)(second - first) <= 30000);
}

static void test_one_whole_line_per_announce() {
  WdpAnnouncer a; wdpInit(a, 11, 0);
  std::string got = drain(a, 0, 3001, 255, "PSI Front");
  CHECK(got == build("PSI Front", "abc1234", "droidnet.contract"));
}

// ── wdpTick: non-blocking writes ──────────────────────────────────────────────

static void test_a_line_longer_than_the_tx_buffer_goes_out_in_pieces() {
  // An AVR HardwareSerial has 63 bytes free when empty; every fork line is longer.
  WdpAnnouncer a; wdpInit(a, 5, 0);
  const char* p = 0;
  uint32_t t = 0;
  uint8_t n = 0;
  while (!(n = wdpTick(a, t, "Flthy HP Controller", "75755de", "droidnet.contract hp.servo hp.led", 63, &p)))
    t++;
  std::string want = build("Flthy HP Controller", "75755de", "droidnet.contract hp.servo hp.led");
  CHECK(want.size() > 63);
  CHECK(n == 63);
  std::string got(p, n);
  n = wdpTick(a, t + 1, "Flthy HP Controller", "75755de", "droidnet.contract hp.servo hp.led", 63, &p);
  got.append(p, n);
  CHECK(got == want);
  CHECK(wdpTick(a, t + 2, "Flthy HP Controller", "75755de", "droidnet.contract hp.servo hp.led", 63, &p) == 0);
}

static void test_never_hands_out_more_than_the_room_reported() {
  WdpAnnouncer a; wdpInit(a, 5, 0);
  const char* p = 0;
  std::string got;
  for (uint32_t t = 0; t < 3001; t++) {
    uint8_t n = wdpTick(a, t, "PSI Front", "abc1234", "droidnet.contract", 4, &p);
    CHECK(n <= 4);
    got.append(p ? p : "", n);
  }
  CHECK(got == build("PSI Front", "abc1234", "droidnet.contract"));
}

static void test_a_full_tx_buffer_waits_without_losing_bytes() {
  WdpAnnouncer a; wdpInit(a, 9, 0);
  const char* p = 0;
  uint32_t t = 0;
  // Due, but no room: nothing yet.
  for (; t < 3001; t++) CHECK(wdpTick(a, t, "PSI Rear", "abc1234", "droidnet.contract", 0, &p) == 0);
  // A negative room (a driver quirk) is no room.
  CHECK(wdpTick(a, t++, "PSI Rear", "abc1234", "droidnet.contract", -1, &p) == 0);
  std::string got = drain(a, t, t + 5, 255, "PSI Rear");
  CHECK(got == build("PSI Rear", "abc1234", "droidnet.contract"));
}

static void test_no_new_line_starts_while_one_is_in_flight() {
  WdpAnnouncer a; wdpInit(a, 13, 0);
  const char* p = 0;
  uint32_t t = 0;
  std::string got;
  // Start a line, then starve it of room past the next due time.
  for (; got.empty(); t++) {
    uint8_t n = wdpTick(a, t, "PSI Front", "abc1234", "droidnet.contract", 10, &p);
    got.append(p ? p : "", n);
  }
  for (uint32_t end = t + 31000; t < end; t++)
    CHECK(wdpTick(a, t, "PSI Front", "abc1234", "droidnet.contract", 0, &p) == 0);
  // Room again: the rest of THAT line, not a second line's head.
  uint8_t n = wdpTick(a, t++, "PSI Front", "abc1234", "droidnet.contract", 255, &p);
  got.append(p, n);
  CHECK(got == build("PSI Front", "abc1234", "droidnet.contract"));
}

static void test_the_type_is_read_when_each_line_starts() {
  // The PSI reads its front/rear jumper at every announce; a change mid-line must not
  // splice two identities into one line.
  WdpAnnouncer a; wdpInit(a, 17, 0);
  const char* p = 0;
  uint32_t t = 0;
  std::string first;
  for (; first.empty(); t++) {
    uint8_t n = wdpTick(a, t, "PSI Front", "abc1234", "", 10, &p);
    first.append(p ? p : "", n);
  }
  for (; first.size() < build("PSI Front", "abc1234", "").size(); t++) {
    uint8_t n = wdpTick(a, t, "PSI Rear", "abc1234", "", 10, &p);
    first.append(p ? p : "", n);
  }
  CHECK(first == build("PSI Front", "abc1234", ""));
  std::string second;
  for (uint32_t end = t + 30001; t < end && second.empty(); t++) {
    uint8_t n = wdpTick(a, t, "PSI Rear", "abc1234", "", 255, &p);
    second.append(p ? p : "", n);
  }
  CHECK(second == build("PSI Rear", "abc1234", ""));
}

static void test_a_refused_identity_never_sends() {
  WdpAnnouncer a; wdpInit(a, 19, 0);
  std::string got;
  for (uint32_t t = 0; t < 100000; t += 7) {
    const char* p = 0;
    uint8_t n = wdpTick(a, t, "bad,type", "abc1234", "", 255, &p);
    got.append(p ? p : "", n);
  }
  CHECK(got.empty());
}

// ── wdpRest: another writer is about to use the port ──────────────────────────

static void test_rest_hands_out_the_unsent_tail_once() {
  WdpAnnouncer a; wdpInit(a, 23, 0);
  const char* p = 0;
  uint32_t t = 0;
  std::string got;
  for (; got.empty(); t++) {
    uint8_t n = wdpTick(a, t, "PSI Front", "abc1234", "droidnet.contract", 30, &p);
    got.append(p ? p : "", n);
  }
  uint8_t n = wdpRest(a, &p);
  got.append(p, n);
  CHECK(got == build("PSI Front", "abc1234", "droidnet.contract"));
  // Nothing in flight now: neither wdpRest nor the next pass repeats any of it.
  CHECK(wdpRest(a, &p) == 0);
  CHECK(wdpTick(a, t, "PSI Front", "abc1234", "droidnet.contract", 255, &p) == 0);
}

static void test_rest_with_nothing_in_flight_is_empty() {
  WdpAnnouncer a; wdpInit(a, 29, 0);
  const char* p = 0;
  CHECK(wdpRest(a, &p) == 0);
}

int main() {
  test_line_without_caps();
  test_caps_become_a_json_array();
  test_null_caps_mean_none();
  test_fields_at_the_wcb_limits_are_accepted();
  test_fields_past_the_wcb_limits_are_refused();
  test_type_and_fw_are_required();
  test_characters_the_wcb_would_mangle_are_refused();
  test_utf8_is_accepted();
  test_caps_spacing_must_be_single();
  test_a_line_that_does_not_fit_the_buffer_is_refused();
  test_the_longest_fork_lines_fit_wdp_line_max();
  test_first_announce_comes_half_a_second_to_three_seconds_after_boot();
  test_announces_repeat_every_25_to_30_seconds();
  test_boards_with_different_seeds_do_not_announce_in_lockstep();
  test_a_zero_seed_still_jitters();
  test_schedule_survives_millis_wrap();
  test_one_whole_line_per_announce();
  test_a_line_longer_than_the_tx_buffer_goes_out_in_pieces();
  test_never_hands_out_more_than_the_room_reported();
  test_a_full_tx_buffer_waits_without_losing_bytes();
  test_no_new_line_starts_while_one_is_in_flight();
  test_the_type_is_read_when_each_line_starts();
  test_a_refused_identity_never_sends();
  test_rest_hands_out_the_unsent_tail_once();
  test_rest_with_nothing_in_flight_is_empty();
  printf("%s  %d/%d checks passed\n", g_fail ? "FAILURES" : "OK", g_total - g_fail, g_total);
  return g_fail ? 1 : 0;
}
