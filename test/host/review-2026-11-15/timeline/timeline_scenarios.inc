// test/host/timeline_scenarios.inc — v1.3 scenarios. Each R1 task appends its own.

// ---- S6: brightness ceiling (review H2) ------------------------------------------------------
static void ceiling_is_the_owners_brightness() {
  boot();                                              // the owner's brightness is 20
  at(1000, "!P*A:i=solid,c=ffffff");
  at(2000, "!P*A:i=solid,c=ffffff,b=255");
  at(3000, "!**B:v=10");
  runTo(3500);
  CHECK(shown(1500).scale == 20, "a cue with no b= must latch at the owner's 20, saw %u", shown(1500).scale);
  CHECK(shown(2500).scale == 20, "b=255 must clamp to the owner's 20, saw %u", shown(2500).scale);
  CHECK(shown(3400).scale == 10, "B:v=10 must dim to 10, saw %u", shown(3400).scale);
}
static void scored_b_is_carried() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=ffffff,b=8,at=0"); at(1002, "!P*A:i=solid,c=ffffff,at=4");
  at(1003, "!**C:bpm=120,bpb=4,beat=0");
  runTo(4000);
  CHECK(shown(2000).scale == 8, "section 0 carries b=8, saw %u", shown(2000).scale);
  CHECK(shown(3800).scale == 20, "section 1 has no b=, so it runs at the ceiling 20, saw %u", shown(3800).scale);
}

static void the_owners_3p_survives_a_show() {
  boot();
  at(500, "3P50"); at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00,b=255"); at(2000, "!**M:v=idle");
  runTo(3000);
  CHECK(shown(1900).scale == 50, "the ceiling is the owner's 3P 50, saw %u", shown(1900).scale);
  CHECK(shown(2900).scale == 50, "after the show the swipe is back at the owner's 50, saw %u", shown(2900).scale);
}

// ---- L1: one show reset (review H3, H4) ------------------------------------------------------
static void show_baseline_is_dark() {
  boot();
  at(1000, "!**M:v=show");
  runTo(1500);
  CHECK(shown(1400).lit == 0, "M:v=show on a cold board must be dark, saw %u lit", shown(1400).lit);
}
static void show_clears_a_live_deadline() {
  boot();
  at(1000, "!P*A:i=flash,c=ff0000,d=3000"); at(2000, "!**M:v=show");
  at(2001, "!P*A:i=solid,c=00ff00,at=0"); at(2002, "!P*A:i=solid,c=0000ff,at=8"); at(2003, "!**C:bpm=120,bpb=4,beat=0");
  runTo(7000);
  CHECK(shown(5000).allAre(0x00ff00), "the show must outlive the old d=3000 (green at 5 s)");
  CHECK(shown(6800).allAre(0x0000ff), "section 2 (blue, beat 8) must render");
}
static void show_stops_the_clock() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=101010,at=0"); at(2000, "!**C:bpm=120,bpb=4,beat=0");
  at(7000, "!**M:v=show"); at(7001, "!P*A:i=solid,c=ff0000,at=0"); at(7002, "!P*A:i=solid,c=00ff00,at=8");
  runTo(9000);
  CHECK(shown(7500).lit == 0 && shown(8900).lit == 0, "a resent show must wait, dark, for its own C");
}
static void show_after_x_is_dark() {
  boot();
  at(1000, "!P*A:i=solid,c=ff0000"); at(2000, "!P*X"); at(3000, "!**M:v=show");
  runTo(3500);
  CHECK(shown(3400).lit == 0, "M:v=show after X must not replay the last look, saw %u lit", shown(3400).lit);
}
static void x_in_a_show_stays_dark_until_idle() {
  boot();
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00"); at(2000, "!P*X"); at(5000, "!**M:v=idle");
  runTo(6000);
  CHECK(shown(1900).allAre(0x00ff00), "the live look holds until X");
  CHECK(shown(2100).lit == 0 && shown(4900).lit == 0, "X in a show blacks out and stays dark");
  CHECK(shown(5900).lit == 48 && shown(5900).scale == 20, "M:v=idle gives the panel back to the swipe at 20");
}
static void idle_returns_to_swipe_after_a_native_look() {
  boot(0xFF);                                          // blank EEPROM: the compiled alwaysOn default
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=native:15"); at(3000, "!**M:v=idle");
  runTo(8000);
  for (const Frame& f : between(3200, 8000))
    if (f.lit != 48) { CHECK(false, "t=%u: %u lit; Knight Rider is still running after M:v=idle", f.t, f.lit); break; }
}
static void x_outside_a_show_returns_to_swipe() {
  boot(0xFF);
  at(1000, "!P*A:i=native:15"); at(3000, "!P*X");
  runTo(8000);
  for (const Frame& f : between(3200, 8000))
    if (f.lit != 48) { CHECK(false, "t=%u: %u lit; Knight Rider is still running after X", f.t, f.lit); break; }
}
static void a_native_command_takes_the_panel_back() {
  boot();
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00"); at(2000, "0T1");
  runTo(3000);
  CHECK(!shown(2900).allAre(0x00ff00) && shown(2900).lit == 48 && shown(2900).scale == 20,
        "0T1 must end the show and bring the swipe back at 20");
}

// ---- L3: a pulse outside a show (found while planning) ----------------------------------------
static void pulse_outside_a_show_returns_to_native() {
  boot();
  at(1000, "!P*P:c=ffffff,d=200");
  runTo(2500);
  CHECK(shown(1100).allAre(0xffffff), "the pulse must show");
  CHECK(shown(1100).scale == 20, "the pulse runs at the owner's 20, saw %u", shown(1100).scale);
  CHECK(shown(2400).lit == 48 && shown(2400).scale == 20 && !shown(2400).allAre(0xffffff),
        "after the pulse the swipe must be back at 20 (saw scale %u)", shown(2400).scale);
  bool blueHold = true;
  for (const Frame& f : between(1500, 2500)) if (f.scale != 20) blueHold = false;
  CHECK(blueHold, "no frame after the pulse may leave the owner's brightness");
}

// ---- L2: host-silence watchdog ---------------------------------------------------------------
static void watchdog_gives_the_panel_back() {
  boot();
  at(1000, "!**M:v=show,wd=2"); at(1100, "!P*A:i=solid,c=00ff00");
  runTo(4000);
  CHECK(shown(2900).allAre(0x00ff00), "inside wd the look holds");
  CHECK(shown(3900).lit == 48 && !shown(3900).allAre(0x00ff00), "2 s of silence must end the show (swipe back)");
}
static void watchdog_is_fed_by_corrections() {
  boot();
  at(1000, "!**M:v=show,wd=2"); at(1100, "!P*A:i=solid,c=00ff00"); at(1200, "!**C:bpm=120,bpb=4,beat=0");
  for (uint32_t t = 2500; t < 9000; t += 1500) at(t, "!**C:ph=0,beat=0");
  runTo(9000);
  CHECK(shown(8900).allAre(0x00ff00), "a C every 1.5 s keeps a wd=2 show alive");
}
static void no_wd_means_no_watchdog() {
  boot();
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00");
  runTo(40000);
  CHECK(shown(39900).allAre(0x00ff00), "a show armed without wd= must hold with no host");
}

// ---- C1: a re-anchor does not re-fire the accent (review M6) ---------------------------------
static void reanchor_does_not_refire_the_accent() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=000040,at=0,am=2,ae=solid,ac=ffffff,ad=180");
  at(2000, "!**C:bpm=60,bpb=4,beat=0"); at(4500, "!**C:ph=1500,beat=4");
  runTo(6400);
  for (const Frame& f : between(4300, 4950))
    if (f.allAre(0xffffff)) { CHECK(false, "t=%u: an accent fired mid-beat after the re-anchor", f.t); break; }
  CHECK(shown(5100).allAre(0xffffff), "the next real beat (5000) still accents");
}

// ---- C2: a correction never moves the section cursor backwards -------------------------------
static void a_nudge_never_flips_the_section_back() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=ff0000,at=0"); at(1002, "!P*A:i=solid,c=0000ff,at=8");
  at(2000, "!**C:bpm=120,bpb=4,beat=0");               // beat 8 lands at 6000
  at(6040, "!**C:ph=50,beat=8");                       // a correction: "beat 8 is still 50 ms away"
  runTo(6500);
  CHECK(shown(6030).allAre(0x0000ff), "section 2 started at beat 8");
  for (const Frame& f : between(6040, 6500))
    if (f.allAre(0xff0000)) { CHECK(false, "t=%u: the correction flipped the show back to section 1", f.t); break; }
}

// ---- S1, S2: the pump cannot strobe (review H5) ----------------------------------------------
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
  unsigned p = pumpPeaks(1100, 5100);                  // 240 BPM = 250 ms a beat; k = 2 => 500 ms
  CHECK(p >= 6 && p <= 9, "at 240 BPM the pump must ride every second beat (8 in 4 s), saw %u", p);
}

// ---- section 6: capacity and overflow (review M7) --------------------------------------------
static void twelve_sections_all_play() {
  boot();
  at(1000, "!**M:v=show");
  static char buf[12][48];
  for (int i = 0; i < 12; i++) { snprintf(buf[i], sizeof buf[i], "!P*A:i=solid,c=%02x0000,at=%d", 0x10 + i, i * 2); at(1001 + i, buf[i]); }
  at(1100, "!**C:bpm=120,bpb=4,beat=0");
  runTo(13500);
  for (int i = 0; i < 12; i++) {
    uint32_t t = 1100 + (uint32_t)(i * 2) * 500 + 600;
    CHECK(shown(t).px[20].r == 0x10 + i, "section %d (beat %d) did not play: red=%02x at t=%u", i, i * 2, shown(t).px[20].r, t);
  }
}

// ---- section 7 and 10: verb V and the Q reply ------------------------------------------------
static const char* kL0 = "P*A:i=solid,c=00ff00,at=0";
static const char* kL1 = "P*A:i=solid,c=0000ff,at=8";
static void loadTwo(uint16_t k, int n) {
  static char a[64], b[64], v[48];
  snprintf(a, sizeof a, "!%s", kL0); snprintf(b, sizeof b, "!%s", kL1); snprintf(v, sizeof v, "!P*V:n=%d,k=%04x", n, k);
  at(1000, "!**M:v=show"); at(1001, a); at(1002, b); at(1003, v); at(1004, "!PFQ"); at(1100, "!**C:bpm=120,bpb=4,beat=0");
}
static void a_verified_load_plays_and_reports_ok() {
  boot();
  loadTwo((uint16_t)(crc16(kL0) ^ crc16(kL1)), 2);
  runTo(3000);
  CHECK(shown(2900).allAre(0x00ff00), "a good load plays");
  CHECK(strstr(Serial1.txLog, "!PFq:ver=1.3,cap=") && strstr(Serial1.txLog, ",n=2,ok=1,bpm=0\r"),
        "Q after a good V must report n=2,ok=1 (tx: %s)", Serial1.txLog);
}
static void a_bad_checksum_gives_the_panel_back() {
  boot();
  loadTwo((uint16_t)(crc16(kL0) ^ crc16(kL1) ^ 1), 2);
  runTo(3000);
  CHECK(shown(2900).lit == 48 && !shown(2900).allAre(0x00ff00), "a bad load must return to the swipe");
  CHECK(strstr(Serial1.txLog, ",ok=2,") != nullptr, "Q after a bad V must report ok=2 (tx: %s)", Serial1.txLog);
}
static void a_short_count_gives_the_panel_back() {
  boot();
  loadTwo((uint16_t)(crc16(kL0) ^ crc16(kL1)), 3);
  runTo(3000);
  CHECK(shown(2900).lit == 48 && !shown(2900).allAre(0x00ff00), "a missing line must return to the swipe");
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
  snprintf(v, sizeof v, "!P*V:n=40,k=%04x", k);
  at(1100, v); at(1200, "!**C:bpm=120,bpb=4,beat=0");
  runTo(3000);
  CHECK(shown(2900).lit == 48 && !shown(2900).allAre(0x00ff00), "a load larger than the table must be refused");
}

// ---- S7: native allow-list -------------------------------------------------------------------
static void native_codes_off_the_list_are_ignored() {
  boot();
  at(1000, "!P*A:i=solid,c=00ff00"); at(2000, "!P*A:i=native:2"); at(3000, "!P*A:i=native:99"); at(4000, "!P*A:i=native:3");
  runTo(5000);
  for (const Frame& f : between(1100, 5000))
    if (!f.allAre(0x00ff00)) { CHECK(false, "t=%u: the look changed; native 2, 3 and 99 must be ignored", f.t); break; }
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

// ---- S4: live look changes are spaced --------------------------------------------------------
static void a_second_live_look_inside_170ms_is_held() {
  boot();
  at(1000, "!P*A:i=solid,c=ff0000"); at(1050, "!P*A:i=solid,c=00ff00"); at(1090, "!P*A:i=solid,c=0000ff");
  runTo(1400);
  CHECK(shown(1150).allAre(0xff0000), "inside 170 ms the first look holds");
  CHECK(shown(1300).allAre(0x0000ff), "after the window the newest held look applies");
}

static const Scenario kScenarios[] = {
  {"ceiling_is_the_owners_brightness", ceiling_is_the_owners_brightness},
  {"scored_b_is_carried", scored_b_is_carried},
  {"the_owners_3p_survives_a_show", the_owners_3p_survives_a_show},
  {"show_baseline_is_dark", show_baseline_is_dark},
  {"show_clears_a_live_deadline", show_clears_a_live_deadline},
  {"show_stops_the_clock", show_stops_the_clock},
  {"show_after_x_is_dark", show_after_x_is_dark},
  {"x_in_a_show_stays_dark_until_idle", x_in_a_show_stays_dark_until_idle},
  {"idle_returns_to_swipe_after_a_native_look", idle_returns_to_swipe_after_a_native_look},
  {"x_outside_a_show_returns_to_swipe", x_outside_a_show_returns_to_swipe},
  {"a_native_command_takes_the_panel_back", a_native_command_takes_the_panel_back},
  {"pulse_outside_a_show_returns_to_native", pulse_outside_a_show_returns_to_native},
  {"watchdog_gives_the_panel_back", watchdog_gives_the_panel_back},
  {"watchdog_is_fed_by_corrections", watchdog_is_fed_by_corrections},
  {"no_wd_means_no_watchdog", no_wd_means_no_watchdog},
  {"reanchor_does_not_refire_the_accent", reanchor_does_not_refire_the_accent},
  {"a_nudge_never_flips_the_section_back", a_nudge_never_flips_the_section_back},
  {"out_of_range_bpm_is_ignored", out_of_range_bpm_is_ignored},
  {"pump_recurs_no_faster_than_340ms", pump_recurs_no_faster_than_340ms},
  {"twelve_sections_all_play", twelve_sections_all_play},
  {"a_verified_load_plays_and_reports_ok", a_verified_load_plays_and_reports_ok},
  {"a_bad_checksum_gives_the_panel_back", a_bad_checksum_gives_the_panel_back},
  {"a_short_count_gives_the_panel_back", a_short_count_gives_the_panel_back},
  {"an_overflowing_load_is_bad", an_overflowing_load_is_bad},
  {"native_codes_off_the_list_are_ignored", native_codes_off_the_list_are_ignored},
  {"the_saber_ignores_a_command_timing", the_saber_ignores_a_command_timing},
  {"a_second_live_look_inside_170ms_is_held", a_second_live_look_inside_170ms_is_held},
};
