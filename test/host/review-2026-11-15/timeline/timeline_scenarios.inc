// timeline_scenarios.inc — contract v1.3 scenarios for the PSI (spec as amended 2026-11-16).
// Front PSI, owner brightness 20 (EEPROM 00,01,14) unless a scenario says otherwise. Section colours
// avoid blue and red: the front swipe is made of those, and a full-secondary swipe is solid red.
#define GREEN 0x00ff00
#define MAGENTA 0xff00ff
#define CYAN 0x00ffff
static bool swipeAt(uint32_t t) {     // the native idle look at the owner's brightness
  const Frame& f = shown(t);
  return f.lit == 48 && f.scale == 20 && !f.allAre(GREEN) && !f.allAre(MAGENTA) && !f.allAre(CYAN) && !f.allAre(0xffffff);
}

// ---- holds on every firmware version ---------------------------------------------------------
static void no_wd_means_no_watchdog() {
  boot();
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00");
  runTo(40000);
  CHECK(shown(39900).allAre(GREEN), "a show armed without wd= must hold with no host");
}

// ---- S6: brightness is a fraction of the owner's ceiling (review H2) --------------------------
static void brightness_is_a_fraction_of_the_owners() {
  boot();                                              // the owner's brightness is 20
  at(1000, "!P*A:i=solid,c=ffffff"); at(2000, "!P*A:i=solid,c=ffffff,b=255"); at(3000, "!**B:v=128");
  runTo(3500);
  CHECK(shown(1500).scale == 20, "a cue with no b= must latch at the owner's 20, saw %u", shown(1500).scale);
  CHECK(shown(2500).scale == 20, "b=255 is the ceiling, 20, saw %u", shown(2500).scale);
  CHECK(shown(3400).scale == 10, "B:v=128 is half the ceiling, 10, saw %u", shown(3400).scale);
}
static void scored_b_is_carried() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=ffffff,b=128,at=0"); at(1002, "!P*A:i=solid,c=ffffff,at=4");
  at(1003, "!**C:bpm=120,bpb=4,beat=0");
  runTo(4000);
  CHECK(shown(2000).scale == 10, "section 0 carries b=128: half of 20, saw %u", shown(2000).scale);
  CHECK(shown(3800).scale == 20, "section 1 has no b=, so it runs at the ceiling 20, saw %u", shown(3800).scale);
}
static void the_owners_3p_survives_a_show() {
  boot();
  at(500, "3P50"); at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00,b=255"); at(2000, "!**M:v=idle");
  runTo(3000);
  CHECK(shown(1900).scale == 50, "the ceiling is the owner's 3P 50, saw %u", shown(1900).scale);
  CHECK(shown(2900).scale == 50, "after the show the swipe is back at the owner's 50, saw %u", shown(2900).scale);
}

// ---- L1, L3, L4, L5: lifecycle (review H3, H4) ------------------------------------------------
static void show_keeps_the_swipe_until_its_first_line() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=00ff00,at=4"); at(3000, "!**C:bpm=120,bpb=4,beat=0");
  runTo(3500);
  CHECK(swipeAt(2900), "between Send and Play the swipe keeps running");
  CHECK(shown(3400).lit == 0, "the show's first C makes the baseline dark, saw %u lit", shown(3400).lit);
}
static void a_retired_d_does_not_end_a_look() {
  boot();
  at(1000, "!P*A:i=solid,c=00ff00,d=1000");
  runTo(6000);
  CHECK(shown(5900).allAre(GREEN), "d= on A is retired: the look holds");
}
static void a_resent_show_waits_for_its_own_seed() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=101010,at=0"); at(2000, "!**C:bpm=120,bpb=4,beat=0");
  at(7000, "!**M:v=show"); at(7001, "!P*A:i=solid,c=ff00ff,at=0"); at(7002, "!P*A:i=solid,c=00ffff,at=8");
  runTo(9000);
  CHECK(swipeAt(7500) && swipeAt(8900), "a resent show must not play on the old clock; the swipe runs until Play");
}
static void show_after_x_does_not_replay_the_last_look() {
  boot();
  at(1000, "!P*A:i=solid,c=00ff00"); at(2000, "!P*X"); at(3000, "!**M:v=show");
  runTo(3500);
  CHECK(shown(2900).lit == 0, "X blacks out and it stays dark");
  CHECK(swipeAt(3400), "M:v=show after X gives the idle look, not the last look");
}
static void x_in_a_show_stays_dark_until_idle() {
  boot();
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00"); at(2000, "!P*X"); at(5000, "!**M:v=idle");
  runTo(6000);
  CHECK(shown(1900).allAre(GREEN), "the live look holds until X");
  CHECK(shown(2100).lit == 0 && shown(4900).lit == 0, "X in a show blacks out and stays dark");
  CHECK(swipeAt(5900), "M:v=idle gives the panel back to the swipe at 20");
}
static void idle_returns_to_swipe_after_a_native_look() {
  boot(0xFF);                                          // blank EEPROM: the compiled alwaysOn default
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=native:15"); at(3000, "!**M:v=idle");
  runTo(8000);
  for (const Frame& f : between(3200, 8000))
    if (f.lit != 48) { CHECK(false, "t=%u: %u lit; Knight Rider is still running after M:v=idle", f.t, f.lit); break; }
}
static void x_is_dark_until_idle_then_the_swipe() {
  boot(0xFF);
  at(1000, "!P*A:i=native:15"); at(3000, "!P*X"); at(6000, "!**M:v=idle");
  runTo(9000);
  CHECK(shown(5900).lit == 0, "X with no show armed still blacks out and holds");
  for (const Frame& f : between(6200, 9000))
    if (f.lit != 48) { CHECK(false, "t=%u: %u lit; Knight Rider came back after M:v=idle", f.t, f.lit); break; }
}
static void a_native_command_takes_the_panel_back() {
  boot();
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00"); at(2000, "0T1");
  runTo(3000);
  CHECK(swipeAt(2900), "0T1 must end the show and bring the swipe back at 20");
}
static void pulse_outside_a_show_returns_to_native() {
  boot();
  at(1000, "!P*P:c=ffffff,d=200");
  runTo(2500);
  CHECK(shown(1100).allAre(0xffffff) && shown(1100).scale == 20, "the pulse shows at the owner's 20");
  for (const Frame& f : between(1500, 2500))
    if (f.scale != 20 || f.lit != 48 || f.allAre(0xffffff)) { CHECK(false, "t=%u: not the swipe at 20 after the pulse", f.t); break; }
}
static void pulse_over_a_native_mode_returns_to_that_mode() {
  boot(0x01);                                          // alwaysOn: Knight Rider keeps running
  at(500, "0T15"); at(3000, "!P*P:c=ffffff,d=200");
  runTo(5000);
  CHECK(shown(3100).allAre(0xffffff), "the pulse shows");
  unsigned most = 0;
  for (const Frame& f : between(3500, 5000)) if (f.lit > most) most = f.lit;
  CHECK(most > 0 && most <= 6, "Knight Rider must be back after the pulse (saw up to %u lit)", most);
}
static void an_a_with_no_look_is_solid() {
  boot();
  at(1000, "!P*A:c=00ff00");
  runTo(1500);
  CHECK(shown(1400).allAre(GREEN), "L5: an A with no i= and no look yet is solid");
}

// ---- L2: host-silence watchdog ---------------------------------------------------------------
static void watchdog_gives_the_panel_back() {
  boot();
  at(1000, "!**M:v=show,wd=2"); at(1100, "!P*A:i=solid,c=00ff00");
  runTo(4000);
  CHECK(shown(2900).allAre(GREEN), "inside wd the look holds");
  CHECK(swipeAt(3900), "2 s of silence must end the show (swipe back)");
}
static void watchdog_is_fed_by_any_addressed_line() {
  boot();
  at(1000, "!**M:v=show,wd=2"); at(1100, "!P*A:i=solid,c=00ff00");
  for (uint32_t t = 2500; t < 9000; t += 1500) at(t, "!**Q");      // an unanswered wildcard Q is a keepalive
  runTo(9000);
  CHECK(shown(8900).allAre(GREEN), "a line every 1.5 s keeps a wd=2 show alive");
}

// ---- C1, C2, C6: the clock (review M6) --------------------------------------------------------
static void reanchor_does_not_refire_the_accent() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=000040,at=0,am=2,ae=solid,ac=ffffff,ad=180");
  at(2000, "!**C:bpm=60,bpb=4,beat=0"); at(4500, "!**C:ph=1500,beat=4");
  runTo(6400);
  for (const Frame& f : between(4300, 4950))
    if (f.allAre(0xffffff)) { CHECK(false, "t=%u: an accent fired mid-beat after the re-anchor", f.t); break; }
  CHECK(shown(5100).allAre(0xffffff), "the next real beat (5000) still accents");
}
static void a_nudge_never_flips_the_section_back() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=ff00ff,at=0"); at(1002, "!P*A:i=solid,c=00ffff,at=8");
  at(2000, "!**C:bpm=120,bpb=4,beat=0");               // beat 8 lands at 6000
  at(6040, "!**C:ph=50,beat=8");                       // a correction: "beat 8 is still 50 ms away"
  runTo(6500);
  CHECK(shown(6030).allAre(CYAN), "section 2 started at beat 8");
  for (const Frame& f : between(6040, 6500))
    if (f.allAre(MAGENTA)) { CHECK(false, "t=%u: the correction flipped the show back to section 1", f.t); break; }
}
static void a_hold_freezes_the_show() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=ff00ff,at=0"); at(1002, "!P*A:i=solid,c=00ffff,at=4");
  at(2000, "!**C:bpm=120,bpb=4,beat=0");               // beat 4 would land at 4000
  at(3000, "!**C:bpm=0");                              // Pause
  at(7000, "!**C:bpm=120,bpb=4,beat=2");               // Resume at beat 2: beat 4 lands at 8000
  runTo(8500);
  CHECK(shown(6900).allAre(MAGENTA), "C:bpm=0 holds the look and freezes the cursor");
  CHECK(shown(7900).allAre(MAGENTA) && shown(8400).allAre(CYAN), "the show resumes from the new seed");
}

// ---- S1, S2: the pump cannot strobe (review H5) -----------------------------------------------
static unsigned pumpPeaks(uint32_t a, uint32_t b) {
  unsigned peaks = 0; uint8_t prev = 255; bool rising = false;
  for (const Frame& f : between(a, b)) {
    if (f.scale > prev + 4) rising = true;
    else if (f.scale + 4 < prev && rising) { peaks++; rising = false; }
    prev = f.scale;
  }
  return peaks;
}
static void out_of_range_bpm_is_ignored() {
  boot(0x00, 0x01, 200);                               // owner brightness 200: the pump's swing is visible
  at(1000, "!**C:bpm=600,bpb=4,beat=0"); at(1001, "!P*A:i=solid,c=ffffff,m=255,am=2");
  runTo(3100);
  CHECK(pumpPeaks(1100, 3100) == 0, "bpm=600 must not start a pump, saw %u peaks in 2 s", pumpPeaks(1100, 3100));
}
static void pump_recurs_no_faster_than_340ms() {
  boot(0x00, 0x01, 200);
  at(1000, "!**C:bpm=240,bpb=4,beat=0"); at(1001, "!P*A:i=solid,c=ffffff,m=255,am=2");
  runTo(5100);
  unsigned p = pumpPeaks(1100, 5100);                  // 240 BPM is 250 ms a beat; k = 2 gives 500 ms
  CHECK(p >= 6 && p <= 9, "at 240 BPM the pump must ride every second beat (8 in 4 s), saw %u", p);
}

// ---- section 6: capacity, every-N accents, no accent on a native section, the meter -----------
static void twelve_sections_all_play() {
  boot();
  at(1000, "!**M:v=show");
  static char buf[12][48];
  for (int i = 0; i < 12; i++) { snprintf(buf[i], sizeof buf[i], "!P*A:i=solid,c=00%02x00,at=%d", 0x10 + i, i * 2); at(1001 + i, buf[i]); }
  at(1100, "!**C:bpm=120,bpb=4,beat=0");
  runTo(13500);
  for (int i = 0; i < 12; i++) {
    uint32_t t = 1100 + (uint32_t)(i * 2) * 500 + 600;
    CHECK(shown(t).px[20].g == 0x10 + i, "section %d (beat %d) did not play: green=%02x at t=%u", i, i * 2, shown(t).px[20].g, t);
  }
}
static unsigned whiteStarts(uint32_t a, uint32_t b, uint32_t* first = nullptr) {
  unsigned n = 0; bool was = false;
  for (const Frame& f : between(a, b)) {
    bool w = f.allAre(0xffffff);
    if (w && !was) { if (!n && first) *first = f.t; n++; }
    was = w;
  }
  return n;
}
static void every_second_beat_accents() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=000040,at=0,am=2,an=2,ap=1,ae=solid,ac=ffffff");
  at(2000, "!**C:bpm=60,bpb=4,beat=0");                // beats at 2000, 3000, ...: odd beats at 3000, 5000, 7000
  runTo(8500);
  uint32_t first = 0;
  CHECK(whiteStarts(2000, 8400, &first) == 3, "an=2,ap=1 accents beats 1, 3 and 5 only, saw %u", whiteStarts(2000, 8400));
  CHECK(first >= 3000 && first < 3100, "the first accent is on beat 1 (t=3000), saw t=%u", first);
}
static void no_accent_on_a_native_section() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=native:15,at=0,am=2,ae=solid,ac=ffffff");
  at(2000, "!**C:bpm=60,bpb=4,beat=0");
  runTo(6000);
  CHECK(whiteStarts(2000, 6000) == 0, "a native section takes no overlay in R1");
}
static void a_meter_with_no_level_feed_walks() {
  boot();
  at(1000, "!P*A:i=meter"); at(1100, "!P*L:v=128");
  runTo(8000);
  unsigned fedKinds = 0, walkKinds = 0; unsigned seenFed[49] = {0}, seenWalk[49] = {0};
  for (const Frame& f : between(1500, 3000)) if (!seenFed[f.lit]++) fedKinds++;
  for (const Frame& f : between(4000, 8000)) if (!seenWalk[f.lit]++) walkKinds++;
  CHECK(fedKinds == 1, "while L feeds it the meter is a flat bar");
  CHECK(walkKinds >= 3, "2 s after the last L the stock random walk runs (saw %u shapes)", walkKinds);
}

// ---- sections 7 and 10: verb V and the routed Q reply -----------------------------------------
static const char* kL0 = "P*A:i=solid,c=00ff00,at=0";
static const char* kL1 = "P*A:i=solid,c=ff00ff,at=8";
static void loadTwo(uint16_t k, int n, bool twice = false) {
  static char a[64], b[64], v[48];
  snprintf(a, sizeof a, "!%s", kL0); snprintf(b, sizeof b, "!%s", kL1); snprintf(v, sizeof v, "!PFV:n=%d,k=%04x", n, k);
  at(1000, "!**M:v=show"); at(1001, a); at(1002, b); if (twice) at(1003, a);
  at(1004, v); at(1005, "!PFQ:r=0"); at(1100, "!**C:bpm=120,bpb=4,beat=0");
}
static void a_verified_load_plays_and_reports_ok() {
  boot();
  loadTwo((uint16_t)(crc16(kL0) ^ crc16(kL1)), 2);
  runTo(3000);
  CHECK(shown(2900).allAre(GREEN), "a good load plays");
  char want[64]; snprintf(want, sizeof want, ";S0!PFq:ver=1.3,rel=1,cap=%d,n=2,ok=1,bpm=0\r", (int)PSI_SCORE_CAP);
  CHECK(strstr(Serial1.txLog, want) != nullptr, "Q:r=0 after a good V (tx: %s)", Serial1.txLog);
}
static void a_line_delivered_twice_is_harmless() {
  boot();
  loadTwo((uint16_t)(crc16(kL0) ^ crc16(kL1)), 2, true);
  runTo(3000);
  CHECK(shown(2900).allAre(GREEN) && strstr(Serial1.txLog, ",n=2,ok=1,"), "a repeated line changes neither n nor k");
}
static void a_bad_checksum_gives_the_panel_back() {
  boot();
  loadTwo((uint16_t)(crc16(kL0) ^ crc16(kL1) ^ 1), 2);
  runTo(3000);
  CHECK(swipeAt(2900), "a bad load must return to the swipe");
  CHECK(strstr(Serial1.txLog, ",ok=2,") != nullptr, "Q after a bad V must report ok=2 (tx: %s)", Serial1.txLog);
}
static void a_short_count_gives_the_panel_back() {
  boot();
  loadTwo((uint16_t)(crc16(kL0) ^ crc16(kL1)), 3);
  runTo(3000);
  CHECK(swipeAt(2900), "a missing line must return to the swipe");
}
static void an_overflowing_load_is_bad() {
  boot();
  at(1000, "!**M:v=show");
  static char buf[40][48], v[40];
  uint16_t k = 0;
  for (int i = 0; i < 40; i++) {                       // 40 sections: more than any PSI table holds
    snprintf(buf[i], sizeof buf[i], "!P*A:i=solid,c=00ff00,at=%d", i * 4);
    at(1001 + i, buf[i]); k ^= crc16(buf[i] + 1);
  }
  snprintf(v, sizeof v, "!PFV:n=40,k=%04x", k);
  at(1100, v); at(1200, "!**C:bpm=120,bpb=4,beat=0");
  runTo(3000);
  CHECK(swipeAt(2900), "a load larger than the table must be refused");
}
static void a_query_with_no_route_is_not_answered() {
  boot();
  at(1000, "!PFQ"); at(1100, "!**Q:r=0");
  runTo(2000);
  CHECK(strstr(Serial1.txLog, "q:ver=") == nullptr, "no r=, or a wildcard: no reply (tx: %s)", Serial1.txLog);
}

// ---- S7: native allow-list -------------------------------------------------------------------
static void native_codes_off_the_list_are_dropped() {
  boot();
  at(1000, "!P*A:i=solid,c=00ff00"); at(2000, "!P*A:i=native:2,c=ff00ff"); at(3000, "!P*A:i=native:99"); at(4000, "!P*A:i=native:3");
  runTo(5000);
  for (const Frame& f : between(1100, 5000))
    if (!f.allAre(GREEN)) { CHECK(false, "t=%u: the look changed; a line naming native 2, 3 or 99 is dropped whole", f.t); break; }
}

// ---- L9: the lightsaber ignores a command timing, as upstream means it to ---------------------
static void the_saber_ignores_a_command_timing() {
  boot();
  at(2000, "0T19|3");
  runTo(9000);
  uint32_t last = 0;
  for (const Frame& f : between(2000, 9000)) if (f.lit > 0 && f.lit < 48) last = f.t;
  CHECK(last >= 7300, "the battle must play to its end (5.4 s), last saber frame at t=%u", last);
}

// ---- S4: live look changes are spaced, and merge key by key -----------------------------------
static void live_looks_inside_170ms_are_held_and_merged() {
  boot(0x00, 0x01, 200);
  at(1000, "!P*A:i=solid,c=00ff00"); at(1050, "!P*A:c=ff00ff"); at(1090, "!P*A:b=128");
  runTo(1400);
  CHECK(shown(1150).allAre(GREEN) && shown(1150).scale == 200, "inside 170 ms the first look holds");
  CHECK(shown(1300).allAre(MAGENTA) && shown(1300).scale == 100, "then the held lines apply together: colour and b=128");
}

static const Scenario kScenarios[] = {
  {"no_wd_means_no_watchdog", no_wd_means_no_watchdog},
  {"brightness_is_a_fraction_of_the_owners", brightness_is_a_fraction_of_the_owners},
  {"scored_b_is_carried", scored_b_is_carried},
  {"the_owners_3p_survives_a_show", the_owners_3p_survives_a_show},
  {"show_keeps_the_swipe_until_its_first_line", show_keeps_the_swipe_until_its_first_line},
  {"a_retired_d_does_not_end_a_look", a_retired_d_does_not_end_a_look},
  {"a_resent_show_waits_for_its_own_seed", a_resent_show_waits_for_its_own_seed},
  {"show_after_x_does_not_replay_the_last_look", show_after_x_does_not_replay_the_last_look},
  {"x_in_a_show_stays_dark_until_idle", x_in_a_show_stays_dark_until_idle},
  {"idle_returns_to_swipe_after_a_native_look", idle_returns_to_swipe_after_a_native_look},
  {"x_is_dark_until_idle_then_the_swipe", x_is_dark_until_idle_then_the_swipe},
  {"a_native_command_takes_the_panel_back", a_native_command_takes_the_panel_back},
  {"pulse_outside_a_show_returns_to_native", pulse_outside_a_show_returns_to_native},
  {"pulse_over_a_native_mode_returns_to_that_mode", pulse_over_a_native_mode_returns_to_that_mode},
  {"an_a_with_no_look_is_solid", an_a_with_no_look_is_solid},
  {"watchdog_gives_the_panel_back", watchdog_gives_the_panel_back},
  {"watchdog_is_fed_by_any_addressed_line", watchdog_is_fed_by_any_addressed_line},
  {"reanchor_does_not_refire_the_accent", reanchor_does_not_refire_the_accent},
  {"a_nudge_never_flips_the_section_back", a_nudge_never_flips_the_section_back},
  {"a_hold_freezes_the_show", a_hold_freezes_the_show},
  {"out_of_range_bpm_is_ignored", out_of_range_bpm_is_ignored},
  {"pump_recurs_no_faster_than_340ms", pump_recurs_no_faster_than_340ms},
  {"twelve_sections_all_play", twelve_sections_all_play},
  {"every_second_beat_accents", every_second_beat_accents},
  {"no_accent_on_a_native_section", no_accent_on_a_native_section},
  {"a_meter_with_no_level_feed_walks", a_meter_with_no_level_feed_walks},
  {"a_verified_load_plays_and_reports_ok", a_verified_load_plays_and_reports_ok},
  {"a_line_delivered_twice_is_harmless", a_line_delivered_twice_is_harmless},
  {"a_bad_checksum_gives_the_panel_back", a_bad_checksum_gives_the_panel_back},
  {"a_short_count_gives_the_panel_back", a_short_count_gives_the_panel_back},
  {"an_overflowing_load_is_bad", an_overflowing_load_is_bad},
  {"a_query_with_no_route_is_not_answered", a_query_with_no_route_is_not_answered},
  {"native_codes_off_the_list_are_dropped", native_codes_off_the_list_are_dropped},
  {"the_saber_ignores_a_command_timing", the_saber_ignores_a_command_timing},
  {"live_looks_inside_170ms_are_held_and_merged", live_looks_inside_170ms_are_held_and_merged},
};
