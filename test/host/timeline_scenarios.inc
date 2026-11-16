// test/host/timeline_scenarios.inc — contract scenarios for test_timeline.cpp. Each task that changes
// what the panel does adds its scenarios here and to kScenarios. Front PSI, owner brightness 20
// (EEPROM 00,01,14) unless a scenario says otherwise. Section colours avoid blue and red: the front
// swipe is made of those, and a full-secondary swipe is solid red.
//
// run.sh builds this file twice: as the default serial-only firmware, and as the PSIPro-i2c
// environment is built (-DPSI_ENABLE_I2C -DPSI_NO_STRIP_LOOKS). Scenarios that only make sense in
// one of them sit under #ifdef.
#define GREEN 0x00ff00
#define MAGENTA 0xff00ff
#define CYAN 0x00ffff
#define WHITE 0xffffff
static bool swipeAt(uint32_t t) {     // the native idle look at the owner's brightness
  const Frame& f = shown(t);
  return f.lit == 48 && f.scale == 20 && !f.allAre(GREEN) && !f.allAre(MAGENTA) && !f.allAre(CYAN) && !f.allAre(WHITE);
}
static unsigned mostLit(uint32_t a, uint32_t b) {
  unsigned most = 0;
  for (const Frame& f : between(a, b)) if (f.lit > most) most = f.lit;
  return most;
}
static uint8_t peakScale(uint32_t a, uint32_t b) {
  uint8_t m = 0;
  for (const Frame& f : between(a, b)) if (f.lit && f.scale > m) m = f.scale;
  return m;
}
// A peak is a rise of the frame's brightness scale followed by a fall.
static unsigned pumpPeaks(uint32_t a, uint32_t b) {
  unsigned peaks = 0; uint8_t prev = 255; bool rising = false;
  for (const Frame& f : between(a, b)) {
    if (f.scale > prev + 4) rising = true;
    else if (f.scale + 4 < prev && rising) { peaks++; rising = false; }
    prev = f.scale;
  }
  return peaks;
}
static unsigned countOf(const char* hay, const char* needle) {
  unsigned n = 0;
  for (const char* q = hay; (q = strstr(q, needle)) != nullptr; q++) n++;
  return n;
}

// A show armed without wd= has no watchdog: it holds with no host (a show stored in a WCB macro).
static void no_wd_means_no_watchdog() {
  boot();
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00");
  runTo(40000);
  CHECK(shown(39900).allAre(0x00ff00), "a show armed without wd= must hold with no host");
}

// ---- S1, S2: the pump cannot strobe -----------------------------------------------------------
// S1: a tempo outside 30..300 is ignored whole, so no clock runs and nothing pumps.
static void out_of_range_bpm_is_ignored() {
  boot(0x00, 0x01, 200);                               // owner brightness 200: the pump's swing is visible
  at(1000, "!**C:bpm=600,bpb=4,beat=0"); at(1001, "!P*A:i=solid,c=ffffff,m=255,am=2");
  runTo(3100);
  CHECK(pumpPeaks(1100, 3100) == 0, "bpm=600 must not start a pump, saw %u peaks in 2 s", pumpPeaks(1100, 3100));
}
// S2: the pump peaks no more often than every 340 ms. 240 BPM is 250 ms a beat, so it rides every
// second beat: 8 peaks in 4 s, not 16.
static void pump_recurs_no_faster_than_340ms() {
  boot(0x00, 0x01, 200);
  at(1000, "!**C:bpm=240,bpb=4,beat=0"); at(1001, "!P*A:i=solid,c=ffffff,m=255,am=2");
  runTo(5100);
  unsigned p = pumpPeaks(1100, 5100);
  CHECK(p >= 6 && p <= 9, "at 240 BPM the pump must ride every second beat (8 in 4 s), saw %u", p);
}


// ---- S6: brightness is a fraction of the owner's ceiling ----------------------------------------
static void brightness_is_a_fraction_of_the_owners() {
  boot();                                              // the owner's brightness is 20
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=ffffff"); at(2000, "!P*A:i=solid,c=ffffff,b=255");
  at(3000, "!**B:v=128");
  runTo(3500);
  CHECK(shown(1500).scale == 20, "a cue with no b= must latch at the owner's 20, saw %u", shown(1500).scale);
  CHECK(shown(2500).scale == 20, "b=255 is the ceiling, 20, saw %u", shown(2500).scale);
  CHECK(shown(3400).scale == 10, "B:v=128 is half the ceiling, 10, saw %u", shown(3400).scale);
}
static void b_is_only_honoured_in_a_show() {
  boot();
  at(1000, "!P*A:i=solid,c=ffffff"); at(2000, "!**B:v=128");
  runTo(2500);
  CHECK(shown(2400).scale == 20, "B with no show armed changes nothing, saw %u", shown(2400).scale);
}
static void b_is_cleared_by_a_reset() {
  boot();
  at(1000, "!**M:v=show"); at(1100, "!**B:v=128"); at(1200, "!P*A:i=solid,c=ffffff");
  at(2000, "!**M:v=show"); at(2200, "!P*A:i=solid,c=ffffff");
  runTo(3000);
  CHECK(shown(1900).scale == 10, "B dims the show it was sent in, saw %u", shown(1900).scale);
  CHECK(shown(2900).scale == 20, "the next show starts at the ceiling again, saw %u", shown(2900).scale);
}
// S9: b= on P belongs to that pulse. It does not become the base look's level.
static void a_pulse_b_does_not_stick() {
  boot(0x00, 0x01, 200);
  at(1000, "!P*A:i=solid,c=00ff00"); at(2000, "!P*P:c=ffffff,d=200,b=64");
  runTo(3000);
  CHECK(shown(2100).allAre(WHITE) && shown(2100).scale == 50, "the pulse draws at its own b=64 (50 of 200), saw %u",
        shown(2100).scale);
  CHECK(shown(2900).allAre(GREEN) && shown(2900).scale == 200, "after P:b=64 the look is back at 200, saw %u",
        shown(2900).scale);
}
static void scored_b_is_carried() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=ffffff,b=128,at=0"); at(1002, "!P*A:i=solid,c=ffffff,at=4");
  at(1003, "!**C:bpm=120,bpb=4,beat=0");
  runTo(4000);
  CHECK(shown(2000).scale == 10, "section 0 carries b=128: half of 20, saw %u", shown(2000).scale);
  CHECK(shown(3800).scale == 20, "section 1 has no b=, so it runs at the ceiling 20, saw %u", shown(3800).scale);
}
// S6: the ceiling is the owner's brightness when the show ARMS. Raising it between Send and Play
// does not raise the show.
static void the_ceiling_is_taken_when_the_show_arms() {
  boot();
  at(500, "3P50"); at(1000, "!**M:v=show"); at(1500, "3P100"); at(2000, "!P*A:i=solid,c=00ff00");
  runTo(3000);
  CHECK(shown(2900).allAre(GREEN) && shown(2900).scale == 50, "the show runs at the 50 it armed with, saw %u", shown(2900).scale);
}
static void the_owners_3p_survives_a_show() {
  boot();
  at(500, "3P50"); at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00,b=255"); at(2000, "!**M:v=idle");
  runTo(3000);
  CHECK(shown(1900).scale == 50, "the ceiling is the owner's 3P 50, saw %u", shown(1900).scale);
  CHECK(shown(2900).scale == 50, "after the show the swipe is back at the owner's 50, saw %u", shown(2900).scale);
}
// The owner changes the brightness DURING a show. The show in progress keeps the ceiling it armed
// with, but the owner's value is not lost: a Resend arms with it, and the board is given back at it.
static void the_owners_3p_mid_show_is_kept() {
  boot();                                              // EEPROM brightness 20
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00"); at(2000, "3P80");
  at(3000, "!**M:v=show"); at(3100, "!P*A:i=solid,c=00ff00"); at(4000, "!**M:v=idle");
  at(6000, "!**M:v=show"); at(6100, "!P*A:i=solid,c=00ff00"); at(7000, "3P0"); at(8000, "!**M:v=idle");
  runTo(9000);
  CHECK(shown(1900).scale == 20, "the show arms at the owner's 20, saw %u", shown(1900).scale);
  CHECK(shown(2900).allAre(GREEN) && shown(2900).scale == 20, "3P80 mid-show: this show keeps its ceiling, saw %u", shown(2900).scale);
  CHECK(shown(3900).allAre(GREEN) && shown(3900).scale == 80, "a Resend arms with the owner's 80, saw %u", shown(3900).scale);
  CHECK(shown(5900).lit == 48 && shown(5900).scale == 80, "after M:v=idle the swipe is at the owner's 80, saw %u", shown(5900).scale);
  CHECK(shown(7900).allAre(GREEN) && shown(7900).scale == 80, "3P0 mid-show: this show keeps its ceiling, saw %u", shown(7900).scale);
  CHECK(shown(8900).lit == 48 && shown(8900).scale == 20, "3P0 gave the EEPROM value back: the swipe is at 20, saw %u", shown(8900).scale);
}
// ...and the pot, which the contract never writes: turned mid-show, it is what a Resend and the
// give-back see.
static void the_pot_turned_mid_show_is_kept() {
  boot(0x00, 0x00, 20, true, 300);                     // brightness from the pot
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00");
  at(4000, "!**M:v=show"); at(4100, "!P*A:i=solid,c=00ff00"); at(5000, "!**M:v=idle");
  runTo(2000);
  const uint8_t before = shown(1900).scale;
  g_mock_analog = 700;                                 // the owner turns the pot up
  runTo(7000);
  CHECK(before > 0 && shown(3900).scale == before, "the show keeps the ceiling it armed with (%u), saw %u", before, shown(3900).scale);
  CHECK(shown(4900).allAre(GREEN) && shown(4900).scale > before, "a Resend arms with the pot as it is now: %u, was %u", shown(4900).scale, before);
  CHECK(shown(6900).lit == 48 && shown(6900).scale == shown(4900).scale, "and the swipe comes back at it (%u)", shown(6900).scale);
}

// ---- L1, L3, L4, L5, L6: lifecycle ------------------------------------------------------------
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
// A resent show REPLACES the last one: none of the old sections may play, with room in the table
// or with the table full.
static void a_resent_show_replaces_the_last() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=101010,at=0"); at(1002, "!P*A:i=solid,c=ff00ff,at=200");
  at(1100, "!**C:bpm=120,bpb=4,beat=210");
  at(2000, "!**M:v=show"); at(2001, "!P*A:i=solid,c=00ffff,at=0"); at(2002, "!P*A:i=solid,c=00ff00,at=240");
  at(2100, "!**C:bpm=120,bpb=4,beat=250");
  char line[PSI_SCORE_CAP][48];
  at(3000, "!**M:v=show");
  for (int k = 0; k < PSI_SCORE_CAP; k++) { snprintf(line[k], sizeof line[k], "!P*A:i=solid,c=ff00ff,at=%d", k * 4); at(3001 + k, line[k]); }
  at(3100, "!**C:bpm=120,bpb=4,beat=0");
  at(4000, "!**M:v=show"); at(4001, "!P*A:i=solid,c=00ffff,at=0"); at(4002, "!P*A:i=solid,c=00ff00,at=8");
  at(4100, "!**C:bpm=120,bpb=4,beat=4");               // beat 8 lands at 6100
  runTo(7000);
  CHECK(shown(1900).allAre(MAGENTA), "show A plays its at=200 section at beat 210");
  CHECK(shown(2900).allAre(GREEN), "at beat 250 the resent show plays its own at=240 section");
  CHECK(shown(3900).allAre(MAGENTA), "the full table plays");
  CHECK(shown(5000).allAre(CYAN) && shown(6900).allAre(GREEN), "a resend onto a full table plays only the new show");
}
static void x_clears_the_score() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=00ff00,at=0"); at(1100, "!**C:bpm=120,bpb=4,beat=0");
  at(3000, "!P*X"); at(4000, "!**C:bpm=120,bpb=4,beat=0");
  runTo(6000);
  CHECK(shown(2900).allAre(GREEN), "the section plays until X");
  CHECK(mostLit(3100, 6000) == 0, "X cleared the score: a new seed plays nothing (saw %u lit)", mostLit(3100, 6000));
}
static void idle_clears_the_score() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=00ff00,at=0"); at(1100, "!**C:bpm=120,bpb=4,beat=0");
  at(3000, "!**M:v=idle"); at(4000, "!**C:bpm=120,bpb=4,beat=0");
  runTo(6000);
  for (const Frame& f : between(3200, 6000))
    if (f.allAre(GREEN) || f.lit != 48) { CHECK(false, "t=%u: not the swipe; M:v=idle must clear the score", f.t); break; }
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
  CHECK(mostLit(1500, 2900) <= 6, "the show's native:15 is Knight Rider (one column)");
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
  CHECK(shown(1900).allAre(GREEN), "the show holds the panel");
  for (const Frame& f : between(2002, 3000))
    if (f.allAre(GREEN)) { CHECK(false, "t=%u: a frame of the show after the native command", f.t); break; }
  CHECK(swipeAt(2900), "0T1 must end the show and bring the swipe back at 20");
}
// L6 on a show that is armed and has shown nothing yet: the native command runs, and the show's
// later lines find no show (its score is gone, so Play shows nothing of it).
static void a_native_command_ends_an_armed_show() {
  boot(0x01);                                          // alwaysOn: the native mode keeps running
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=00ff00,at=0"); at(2000, "0T15");
  at(3000, "!**C:bpm=120,bpb=4,beat=0");
  runTo(5000);
  CHECK(mostLit(2500, 5000) > 0 && mostLit(2500, 5000) <= 6, "Knight Rider runs on, saw up to %u lit", mostLit(2500, 5000));
}
static void pulse_outside_a_show_returns_to_native() {
  boot();
  at(1000, "!P*P:c=ffffff,d=200");
  runTo(2500);
  CHECK(shown(1100).allAre(WHITE) && shown(1100).scale == 20, "the pulse shows at the owner's 20");
  for (const Frame& f : between(1500, 2500))
    if (f.scale != 20 || f.lit != 48 || f.allAre(WHITE)) { CHECK(false, "t=%u: not the swipe at 20 after the pulse", f.t); break; }
}
static void pulse_over_a_native_mode_returns_to_that_mode() {
  boot(0x01);                                          // alwaysOn: Knight Rider keeps running
  at(500, "0T15"); at(3000, "!P*P:c=ffffff,d=200");
  runTo(5000);
  CHECK(shown(3100).allAre(WHITE), "the pulse shows");
  CHECK(mostLit(3500, 5000) > 0 && mostLit(3500, 5000) <= 6, "Knight Rider must be back after the pulse (saw up to %u lit)",
        mostLit(3500, 5000));
}
static void an_a_with_no_look_is_solid() {
  boot();
  at(1000, "!P*A:c=00ff00");
  runTo(1500);
  CHECK(shown(1400).allAre(GREEN), "L5: an A with no i= and no look yet is solid");
}
// C6: C:bpm=0 holds. The look stays, the cursor is frozen, and a C with a tempo resumes.
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

// ---- every look reaches the LEDs ----------------------------------------------------------------
// Each look, a second apiece: frames keep latching (a look that stages pixels and never shows them
// is a dark panel), and every look but `off` lights something. The slow ones (sparkle, the meter,
// Knight Rider) latch on their own step, 6 to 9 times in the 0.9 s measured.
static void every_look_latches_frames() {
  static const char* const looks[] = {"off", "solid", "flash", "pulse", "rainbow", "scan", "comet", "chase", "wipe",
                                      "gradient", "colorcycle", "twinkle", "sparkle", "meter", "native:15"};
  const int n = (int)(sizeof looks / sizeof looks[0]);
  char line[15][48];
  boot();
  for (int k = 0; k < n; k++) { snprintf(line[k], sizeof line[k], "!P*A:i=%s,c=00ff00,s=200", looks[k]); at(1000 + 1000 * k, line[k]); }
  runTo(1000 + 1000 * n);
  for (int k = 0; k < n; k++) {
    const uint32_t a = 1100 + 1000 * k, b = 1990 + 1000 * k;
    CHECK(between(a, b).size() >= 5, "i=%s latched %u frames in 0.9 s", looks[k], (unsigned)between(a, b).size());
    if (k == 0) CHECK(mostLit(a, b) == 0, "i=off is dark");
    else        CHECK(mostLit(a, b) > 0, "i=%s lit nothing", looks[k]);
  }
}
#ifndef PSI_NO_STRIP_LOOKS
// The strip looks are drawn column by column from the core's own maths: a comet has a tail of
// falling brightness, a wipe grows, a gradient spreads hues, a colour cycle is one colour that moves.
static void strip_looks_draw_their_shapes() {
  boot();
  at(1000, "!P*A:i=comet,c=00ff00,s=200"); at(3000, "!P*A:i=wipe,c=00ff00,s=200");
  at(5000, "!P*A:i=gradient,s=200"); at(7000, "!P*A:i=colorcycle,s=200");
  runTo(9000);
  unsigned levels = 0;
  for (const Frame& f : between(1200, 2900)) {
    bool seen[256] = {false}; unsigned k = 0;
    for (int i = 0; i < NUM_LEDS; i++) if (f.px[i].g && !seen[f.px[i].g]) { seen[f.px[i].g] = true; k++; }
    if (k > levels) levels = k;
  }
  CHECK(levels >= 3, "a comet has a tail: at least 3 brightness levels in one frame, saw %u", levels);
  bool litCounts[49] = {false}; unsigned distinct = 0;
  for (const Frame& f : between(3200, 4900)) if (!litCounts[f.lit]) { litCounts[f.lit] = true; distinct++; }
  CHECK(distinct >= 4, "a wipe grows: at least 4 different lit counts, saw %u", distinct);
  const Frame& g = shown(6000);
  CHECK(g.lit == 48 && !g.allAre(((uint32_t)g.px[0].r << 16) | ((uint32_t)g.px[0].g << 8) | g.px[0].b),
        "a gradient is more than one colour at once");
  const Frame& c1 = shown(7500); const Frame& c2 = shown(8500);
  const uint32_t rgb1 = ((uint32_t)c1.px[0].r << 16) | ((uint32_t)c1.px[0].g << 8) | c1.px[0].b;
  CHECK(c1.allAre(rgb1) && !c2.allAre(rgb1), "a colour cycle is one colour at a time, and it moves");
}
// The one colour-scaling helper those looks share (uint8 * uint8 overflows a 16-bit int on AVR).
static void the_colour_scaler_is_exact() {
  const CRGB full = _scaleC(CRGB(255, 128, 0), 255), zero = _scaleC(CRGB(255, 128, 0), 0), half = _scaleC(CRGB(255, 128, 0), 128);
  CHECK(full.r == 255 && full.g == 128 && full.b == 0, "_scaleC(v=255) is the colour itself");
  CHECK(zero.r == 0 && zero.g == 0 && zero.b == 0, "_scaleC(v=0) is black");
  CHECK(half.r == 128 && half.g == 64 && half.b == 0, "_scaleC(v=128) is half (%u,%u,%u)", half.r, half.g, half.b);
}
#endif

// ---- the beat envelope and the accent overlay ---------------------------------------------------
// am=3 ("build") scales the pump by progress through the section: it must rise, and it must not sit
// at the floor.
static void a_build_section_ramps() {
  boot(0x00, 0x01, 200);
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=ffffff,m=255,am=3,at=0"); at(1002, "!P*A:i=solid,c=ffffff,at=16");
  at(1100, "!**C:bpm=120,bpb=4,beat=0");               // the section is beats 0 to 15: 1100 to 9100
  runTo(9000);
  const uint8_t early = peakScale(1600, 3000), late = peakScale(7100, 9000);
  CHECK(late > early + 40, "a build ramps with the section: early peak %u, late peak %u", early, late);
  CHECK(late > 100, "a build is not parked at the floor: late peak %u of 200", late);
}
// am=1: a scored accent on every downbeat and on no other beat, in the accent's own colour.
static void a_downbeat_accent_fires_on_downbeats_only() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=sparkle,c=00ff00,at=0,am=1,ae=flash,ac=ffffff");
  at(1100, "!**C:bpm=120,bpb=4,beat=0");               // downbeats at 1100, 3100, 5100
  runTo(7000);
  const std::vector<uint32_t> on = onsets(1100, 7000, WHITE);
  CHECK(on.size() == 3, "12 beats of am=1 accent 3 times, saw %u", (unsigned)on.size());
  for (size_t k = 0; k < on.size() && k < 3; k++)
    CHECK(on[k] >= 1100 + 2000 * k && on[k] <= 1160 + 2000 * k, "accent %u is on its downbeat, saw t=%u", (unsigned)k, on[k]);
}
// am=2: exactly one accent a beat (not one a frame), in the section's colour when there is no ac=,
// for the ad= it names.
static void an_every_beat_accent_fires_once_a_beat() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=sparkle,c=00ff00,at=0,am=2,ae=solid,ad=200");
  at(1100, "!**C:bpm=120,bpb=4,beat=0");
  runTo(4050);
  const std::vector<uint32_t> on = onsets(1100, 4050, GREEN);
  CHECK(on.size() == 6, "6 beats of am=2 accent 6 times, saw %u", (unsigned)on.size());
  for (size_t k = 0; k < on.size() && k < 6; k++)
    CHECK(on[k] >= 1100 + 500 * k && on[k] <= 1160 + 500 * k, "accent %u is on its beat, saw t=%u", (unsigned)k, on[k]);
  if (!on.empty()) {
    uint32_t end = on[0];
    for (const Frame& f : between(on[0], on[0] + 400)) { if (!f.allAre(GREEN)) break; end = f.t; }
    CHECK(end - on[0] >= 150 && end - on[0] <= 230, "ad=200 is a 200 ms accent, saw %u ms", end - on[0]);
  }
}
// An entry with no ae=, or with an ae= the core refuses (a look that keeps state, a native code),
// never arms the overlay: a v1.1 show plays exactly as it did.
static void no_ae_means_no_overlay() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=sparkle,c=00ff00,at=0,am=2,m=100"); at(1100, "!**C:bpm=120,bpb=4,beat=0");
  at(4000, "!**M:v=show"); at(4001, "!P*A:i=sparkle,c=00ff00,at=0,am=2,ae=scan,ac=ffffff"); at(4100, "!**C:bpm=120,bpb=4,beat=0");
  at(7000, "!**M:v=show"); at(7001, "!P*A:i=sparkle,c=00ff00,at=0,am=2,ae=native:15,ac=ffffff"); at(7100, "!**C:bpm=120,bpb=4,beat=0");
  runTo(10000);
  CHECK(mostLit(1200, 3900) > 0, "the sparkle section plays");
  for (const Frame& f : between(1200, 10000))
    if (f.lit == 48 && !swipeAt(f.t)) { CHECK(false, "t=%u: a full-field frame; no overlay may arm without a valid ae=", f.t); break; }
}
// The accent is an effect swap in the accent's colour at the unit's level, un-pumped; then the base
// look returns and pumps again.
static void an_accent_swaps_the_effect_at_the_units_level() {
  boot(0x00, 0x01, 200);
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=00ff00,m=255,am=2,at=0,ae=comet,ac=ff0000,ad=180");
  at(1100, "!**C:bpm=60,bpb=4,beat=0");                // a beat a second: the overlay is beats' first 180 ms
  runTo(4000);
  unsigned overlay = 0;
  for (const Frame& f : between(2110, 2260)) {
    bool onlyRed = true;
    for (int i = 0; i < NUM_LEDS; i++) if (f.px[i].g || f.px[i].b) onlyRed = false;
    CHECK(onlyRed && f.lit > 0, "t=%u: the overlay draws in the accent colour (red)", f.t);
    CHECK(f.scale == 200, "t=%u: the overlay is at the unit's level, un-pumped: saw %u", f.t, f.scale);
#ifndef PSI_NO_STRIP_LOOKS
    CHECK(f.lit < 48, "t=%u: ae=comet draws a comet, not a solid (%u lit)", f.t, f.lit);
#else
    CHECK(f.lit == 48, "t=%u: on this build ae=comet draws a solid (%u lit)", f.t, f.lit);
#endif
    overlay++;
  }
  CHECK(overlay >= 4, "the overlay is up for its 180 ms (%u frames)", overlay);
  CHECK(shown(2600).allAre(GREEN), "the base look returns when the accent ends");
  uint8_t lo = 255;
  for (const Frame& f : between(2400, 3050)) if (f.allAre(GREEN) && f.scale < lo) lo = f.scale;
  CHECK(lo < 150, "the base look pumps again after the accent (lowest scale %u of 200)", lo);
}
// A re-seed must not inherit the beat edge of the timeline it replaces: Play after a seek back to
// beat 0 accents beat 0 again.
static void a_reseed_accents_its_first_beat() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=sparkle,c=00ff00,at=0,am=2,ae=solid,ac=ffffff");
  at(1100, "!**C:bpm=60,bpb=4,beat=0"); at(1600, "!**C:bpm=60,bpb=4,beat=0");
  runTo(2000);
  const std::vector<uint32_t> on = onsets(1100, 2000, WHITE);
  CHECK(on.size() == 2 && on[0] <= 1160 && on[1] >= 1600 && on[1] <= 1660,
        "beat 0 accents at the seed (1100) and again at the re-seed (1600): saw %u accents", (unsigned)on.size());
}
// ...and neither must a new show: its first beat accents although the last show accented beat 0.
static void a_new_show_accents_its_first_beat() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=sparkle,c=00ff00,at=0,am=1,ae=solid,ac=ffffff"); at(1100, "!**C:bpm=60,bpb=4,beat=0");
  at(2000, "!**M:v=show"); at(2001, "!P*A:i=sparkle,c=00ff00,at=0,am=1,ae=solid,ac=ffffff"); at(2100, "!**C:bpm=60,bpb=4,beat=0");
  runTo(2500);
  const std::vector<uint32_t> on = onsets(1100, 2500, WHITE);
  CHECK(on.size() == 2 && on[1] >= 2100 && on[1] <= 2160, "each show accents its own beat 0: saw %u accents", (unsigned)on.size());
}
// flash alternates lit and dark under 3 Hz, and it rides the pump. The look changes state every
// 170 ms, but the panel only changes at a frame (every 26 ms), so on the panel a state is 6 or 7
// frames: 156 or 182 ms, 170 on average. What is held here is what the eye gets: no state shorter
// than 170 ms less one frame, and never more than 3 dark-to-lit changes in any second.
static void flash_alternates_no_faster_than_the_cap() {
  boot(0x00, 0x01, 200);
  at(1000, "!P*A:i=flash,c=ffffff,s=255");
  at(5000, "!**C:bpm=60,bpb=4,beat=0"); at(5001, "!P*A:m=200,am=2");
  runTo(9000);
  bool sawLit = false, sawDark = false; uint32_t since = 0; bool state = false; bool first = true; uint32_t shortest = 99999;
  std::vector<uint32_t> litAt;
  for (const Frame& f : between(1200, 4900)) {
    const bool lit = f.lit > 0;
    if (lit) sawLit = true; else sawDark = true;
    if (first) { state = lit; since = 0; first = false; continue; }
    if (lit != state) {
      if (since && f.t - since < shortest) shortest = f.t - since;
      if (lit) litAt.push_back(f.t);
      since = f.t; state = lit;
    }
  }
  CHECK(sawLit && sawDark, "flash alternates a lit and a dark frame (lit=%d dark=%d)", sawLit, sawDark);
  CHECK(shortest >= 170 - 26, "no flash state is shorter than 170 ms less one frame, saw %u ms", shortest);
  for (size_t k = 3; k < litAt.size(); k++)
    CHECK(litAt[k] - litAt[k - 3] >= 1000, "4 dark-to-lit changes inside %u ms (at t=%u)", litAt[k] - litAt[k - 3], litAt[k]);
  uint8_t lo = 255, hi = 0;
  for (const Frame& f : between(5300, 9000)) if (f.lit) { if (f.scale < lo) lo = f.scale; if (f.scale > hi) hi = f.scale; }
  CHECK(hi > lo + 40, "flash rides the beat pump: lit frames from %u to %u", lo, hi);
}
// The strobe cool-down cannot be bypassed by re-arming while an overlay is up: an accent inside
// 340 ms of the last ACCEPTED one is refused whole, and one after it is taken.
static void the_accent_cool_down_cannot_be_bypassed() {
  boot();
  at(1000, "!P*A:i=solid,c=00ff00"); at(2000, "!P*P:c=ffffff,d=300"); at(2100, "!P*P:c=ff00ff,d=300");
  at(2400, "!P*P:c=00ffff,d=300");
  runTo(3000);
  CHECK(shown(2150).allAre(WHITE), "the first pulse is up");
  for (const Frame& f : between(2000, 2390))
    if (f.allAre(MAGENTA)) { CHECK(false, "t=%u: a pulse 100 ms after the last one was taken", f.t); break; }
  CHECK(shown(2280).allAre(WHITE), "the refused pulse did not change the one on screen");
  CHECK(shown(2450).allAre(CYAN), "a pulse 400 ms after the last accepted one is taken");
}

// ---- sections 7 and 10: verb V and the routed Q reply ----------------------------------------------
static const char* kL0 = "P*A:i=solid,c=00ff00,at=0";
static const char* kL1 = "P*A:i=solid,c=ff00ff,at=8";
// M:v=show, the two lines (the first twice if asked), V with the given numbers, Q, then Play.
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
// n and k do not depend on a line delivered twice (a mesh retry) or on the order of arrival.
static void a_line_delivered_twice_is_harmless() {
  boot();
  loadTwo((uint16_t)(crc16(kL0) ^ crc16(kL1)), 2, true);
  runTo(3000);
  CHECK(shown(2900).allAre(GREEN) && strstr(Serial1.txLog, ",n=2,ok=1,"), "a repeated line changes neither n nor k (tx: %s)", Serial1.txLog);
}
// A board that did not get its whole show goes back to its native look, which the owner can see
// with no return path, and says ok=2 until the next M:v=show.
static void a_bad_checksum_gives_the_panel_back() {
  boot();
  loadTwo((uint16_t)(crc16(kL0) ^ crc16(kL1) ^ 1), 2);
  at(4000, "!PFQ:r=0");
  runTo(5000);
  CHECK(swipeAt(2900), "a bad load must return to the swipe, Play or no Play");
  CHECK(countOf(Serial1.txLog, ",n=0,ok=2,") == 2, "Q after a bad V reports ok=2, and still does 3 s later (tx: %s)", Serial1.txLog);
}
static void a_short_count_gives_the_panel_back() {
  boot();
  loadTwo((uint16_t)(crc16(kL0) ^ crc16(kL1)), 3);
  runTo(3000);
  CHECK(swipeAt(2900), "a missing line must return to the swipe");
}
// An insert into a full table is an error, not a silent drop: the load is bad even when V's own
// numbers are right for the lines that were sent.
static void an_overflowing_load_is_bad() {
  boot();
  static char buf[40][48], v[40];
  uint16_t k = 0;
  at(1000, "!**M:v=show");
  for (int i = 0; i < 40; i++) {                       // 40 sections: more than either table holds
    snprintf(buf[i], sizeof buf[i], "!P*A:i=solid,c=00ff00,at=%d", i * 4);
    at(1001 + i, buf[i]); k ^= crc16(buf[i] + 1);
  }
  snprintf(v, sizeof v, "!PFV:n=40,k=%04x", k);
  at(1100, v); at(1200, "!**C:bpm=120,bpb=4,beat=0");
  runTo(3000);
  CHECK(swipeAt(2900), "a load larger than the table must be refused");
}
// After a failed V the unit takes no scored line until the next M:v=show, so a late or retried
// line cannot start half a show. The next M:v=show clears it.
static void a_failed_load_drops_scored_lines_until_the_next_show() {
  boot();
  loadTwo((uint16_t)(crc16(kL0) ^ crc16(kL1) ^ 1), 2);   // fails at 1004
  at(2000, "!P*A:i=solid,c=00ffff,at=0"); at(2100, "!**C:bpm=120,bpb=4,beat=0"); at(2500, "!PFQ:r=0");
  static char a[64], v[48];
  snprintf(a, sizeof a, "!%s", kL0); snprintf(v, sizeof v, "!PFV:n=1,k=%04x", crc16(kL0));
  at(4000, "!**M:v=show"); at(4001, a); at(4002, v); at(4003, "!PFQ:r=0"); at(4100, "!**C:bpm=120,bpb=4,beat=0");
  runTo(6000);
  for (const Frame& f : between(2000, 3990))
    if (f.allAre(CYAN)) { CHECK(false, "t=%u: a scored line after a failed V played", f.t); break; }
  CHECK(strstr(Serial1.txLog, ",n=0,ok=2,") != nullptr, "the late line was not stored (tx: %s)", Serial1.txLog);
  CHECK(shown(5900).allAre(GREEN) && strstr(Serial1.txLog, ",n=1,ok=1,"), "the next M:v=show loads and plays (tx: %s)", Serial1.txLog);
}
// A scored A carries its look. Without i= it is dropped, and V then sees a line missing.
static void a_scored_line_without_a_look_is_dropped() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:c=00ff00,at=0"); at(1002, "!PFQ:r=0"); at(1100, "!**C:bpm=120,bpb=4,beat=0");
  runTo(3000);
  CHECK(strstr(Serial1.txLog, ",n=0,ok=0,") != nullptr, "nothing was stored (tx: %s)", Serial1.txLog);
  CHECK(mostLit(1200, 3000) == 0, "the show began with nothing to play: dark");
}
// The reply, byte for byte: routed back the way SHADOW routes its replies, so a dome board never
// puts a bare line on the bus for the WCB to broadcast. Unpadded decimals, a trailing \r.
static void the_query_reply_is_byte_exact() {
  boot();
  at(1000, "!**C:bpm=128,ph=0,bpb=4,beat=0"); at(1100, "!PFQ:r=0");
  at(2000, "!PFQ:r=7"); at(3000, "!PFQ:r=99");
  at(4000, "!**C:bpm=0"); at(4100, "!PFQ:r=0");        // C6: a held clock reports 0
  runTo(5000);
  const char* tx = Serial1.txLog;
  char want[80];
  snprintf(want, sizeof want, ";S0!PFq:ver=1.3,rel=1,cap=%d,n=0,ok=0,bpm=128\r", (int)PSI_SCORE_CAP);
  CHECK(strstr(tx, want) != nullptr, "r=0: straight to the Pi's own WCB (tx: %s)", tx);
  snprintf(want, sizeof want, ";W7,;S0!PFq:ver=1.3,rel=1,cap=%d,n=0,ok=0,bpm=128\r", (int)PSI_SCORE_CAP);
  CHECK(strstr(tx, want) != nullptr, "r=7: by way of WCB 7 (tx: %s)", tx);
  snprintf(want, sizeof want, ";W99,;S0!PFq:ver=1.3,rel=1,cap=%d,n=0,ok=0,bpm=128\r", (int)PSI_SCORE_CAP);
  CHECK(strstr(tx, want) != nullptr, "r=99: the longest route (tx: %s)", tx);
  snprintf(want, sizeof want, ";S0!PFq:ver=1.3,rel=1,cap=%d,n=0,ok=0,bpm=0\r", (int)PSI_SCORE_CAP);
  CHECK(strstr(tx, want) != nullptr, "a held clock reads 0 (tx: %s)", tx);
  CHECK(countOf(tx, "q:ver=") == 4, "four queries, four replies: saw %u", countOf(tx, "q:ver="));
}
// No r=, an r= that is not a number from 0 to 99, a wildcard, or the other PSI: no reply at all.
static void a_query_that_cannot_be_routed_is_not_answered() {
  boot();
  at(1000, "!PFQ"); at(1100, "!**Q:r=0"); at(1200, "!P*Q:r=0"); at(1300, "!PRQ:r=0");
  at(1400, "!PFQ:r=zz"); at(1500, "!PFQ:r=100"); at(1600, "!PFQ:r="); at(1700, "!PFQ:r=-1");
  runTo(3000);
  CHECK(strstr(Serial1.txLog, "q:ver=") == nullptr, "none of these is answered (tx: %s)", Serial1.txLog);
}

// ---- section 12: flash, pulse and scan have ONE timing definition, the core's -------------------
// The oracle in these three is the core's own function, so the panel is checked against the same
// maths the other two boards and Studio's preview use.
// scan: exactly one whole column lit, at fxScanPos(), walking the panel and back.
static void scan_is_the_shared_ping_pong() {
  boot();
  at(1000, "!P*A:i=scan,c=00ff00,s=200");            // applied at t=1000: the look's own time zero
  runTo(4000);
  unsigned frames = 0; bool seen[COLUMNS] = {false};
  for (const Frame& f : between(1030, 4000)) {
    const int want = fxScanPos(f.t - 1000, 200, COLUMNS, false);
    for (int c = 0; c < COLUMNS; c++)
      for (int r = 0; r < LEDS_PER_COLUMN; r++) {
        const int led = psiLed((uint8_t)c, (uint8_t)r);
        if (led < 0) continue;
        const bool lit = f.px[led].g == 255 && f.px[led].r == 0 && f.px[led].b == 0;
        const bool dark = !(f.px[led].r | f.px[led].g | f.px[led].b);
        if (c == want ? !lit : !dark) { CHECK(false, "t=%u: column %d is wrong (the lit column should be %d)", f.t, c, want); goto done; }
      }
    seen[want] = true; frames++;
  }
done:
  CHECK(frames > 100, "the scan latches every frame (%u in 3 s)", frames);
  for (int c = 0; c < COLUMNS; c++) CHECK(seen[c], "the scan never reached column %d", c);
}
// flash: a state is fxFlashHalfMs(speed), 170 ms at s=255 up to 425 ms at s=0, to within a frame.
static void flash_speed_is_the_shared_timing() {
  boot();
  at(1000, "!P*A:i=flash,c=00ff00,s=0");
  runTo(6000);
  uint32_t since = 0; bool state = false, first = true; unsigned states = 0;
  for (const Frame& f : between(1100, 6000)) {
    const bool lit = f.lit > 0;
    if (first) { state = lit; first = false; continue; }
    if (lit == state) continue;
    if (since) {
      const uint32_t d = f.t - since;
      CHECK(d + 26 >= fxFlashHalfMs(0) && d <= fxFlashHalfMs(0) + 26, "at s=0 a state is %u ms, saw %u", (unsigned)fxFlashHalfMs(0), d);
      states++;
    }
    since = f.t; state = lit;
  }
  CHECK(states >= 8, "flash at s=0 changes state about every 425 ms (%u states in 5 s)", states);
}
// pulse: with nothing pumping the look it breathes on fxPulseBright(): 2 s a cycle at s=0, 340 ms
// at s=255 and never faster (S8). Under the beat pump the envelope is the breathe.
// At s=0 the panel is the core's breathe exactly. At s=255 the S9 field gate squares the rise off
// (a fast pulse rises in a step, not a ramp), so there the panel is never ABOVE the breathe.
static void pulse_breathes_on_the_shared_timing() {
  boot(0x00, 0x01, 200);
  at(1000, "!P*A:i=pulse,c=ffffff,s=0"); at(8000, "!P*A:i=pulse,c=ffffff,s=255");
  runTo(13000);
  for (const Frame& f : between(1100, 7900)) {
    const uint8_t want = (uint8_t)(((uint16_t)200 * fxPulseBright(f.t - 1000, 0)) / 255u);
    if (f.scale != want) { CHECK(false, "t=%u: the breathe is %u, the core says %u", f.t, f.scale, want); break; }
  }
  for (const Frame& f : between(8100, 12900)) {
    const uint8_t most = (uint8_t)(((uint16_t)200 * fxPulseBright(f.t - 8000, 255)) / 255u);
    if (f.scale > most) { CHECK(false, "t=%u: the panel is at %u, above the core's breathe (%u)", f.t, f.scale, most); break; }
  }
  const unsigned slow = pumpPeaks(1500, 7500), fast = pumpPeaks(8100, 12900);
  CHECK(slow == 3, "s=0 is a 2 s cycle: 3 peaks in 6 s, saw %u", slow);
  CHECK(fast >= 12 && fast <= 14, "s=255 is a 340 ms cycle: 14 peaks in 4.8 s, never more; saw %u", fast);
}
static void a_pumped_pulse_follows_the_beat() {
  boot(0x00, 0x01, 200);
  at(1000, "!**C:bpm=120,bpb=4,beat=0"); at(1001, "!P*A:i=pulse,c=ffffff,s=255,m=255,am=2");
  runTo(5100);
  const unsigned p = pumpPeaks(1100, 5100);
  CHECK(p >= 7 && p <= 9, "under the pump a pulse peaks once a beat (8 in 4 s), not on its own cycle; saw %u", p);
}

// ---- section 6: no accent on a native section; a meter with no level feed ------------------------
static void no_accent_on_a_native_section() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=native:15,at=0,am=2,ae=solid,ac=ffffff");
  at(2000, "!**C:bpm=60,bpb=4,beat=0");
  runTo(6000);
  CHECK(mostLit(2500, 6000) > 0 && mostLit(2500, 6000) <= 6, "the native section plays (Knight Rider)");
  CHECK(onsets(2000, 6000, WHITE).empty(), "a native section takes no overlay in R1");
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
// ...and a show's L must not leave the board's own VU mode frozen once the show has ended.
static void a_native_vu_mode_walks_after_a_show() {
  boot(0x01);                                          // alwaysOn: the native VU mode keeps running
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=meter"); at(1200, "!P*L:v=128"); at(1500, "!**M:v=idle"); at(1600, "0T92");
  runTo(9000);
  unsigned kinds = 0; unsigned seen[49] = {0};
  for (const Frame& f : between(4000, 9000)) if (!seen[f.lit]++) kinds++;
  CHECK(kinds >= 3, "the native VU meter moves again 2 s after the show's last L (saw %u shapes)", kinds);
}


// ---- C1, C2, S4: corrections and live looks that do not disturb the show ---------------------------
// C1: Studio corrects the clock every 4 s. A correction that lands well into a beat must not fire
// that beat's accent a second time; the next real beat still accents.
static void reanchor_does_not_refire_the_accent() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=000040,at=0,am=2,ae=solid,ac=ffffff,ad=180");
  at(2000, "!**C:bpm=60,bpb=4,beat=0"); at(4500, "!**C:ph=1500,beat=4");   // "beat 4 is 1500 ms away": no change
  runTo(6400);
  CHECK(onsets(2000, 4400, WHITE).size() == 3, "beats 0, 1 and 2 accent (saw %u)", (unsigned)onsets(2000, 4400, WHITE).size());
  for (const Frame& f : between(4300, 4950))
    if (f.allAre(WHITE)) { CHECK(false, "t=%u: an accent fired mid-beat after the re-anchor", f.t); break; }
  CHECK(shown(5100).allAre(WHITE), "the next real beat (5000) still accents");
}
// ...and the same when a scored line arrives mid-beat while the clock runs (a late or re-sent line).
static void a_scored_line_mid_beat_does_not_refire() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=000040,at=0,am=2,ae=solid,ac=ffffff,ad=180");
  at(2000, "!**C:bpm=60,bpb=4,beat=0"); at(4500, "!P*A:i=solid,c=000040,at=64");
  runTo(5400);
  for (const Frame& f : between(4300, 4950))
    if (f.allAre(WHITE)) { CHECK(false, "t=%u: an accent fired mid-beat when a scored line arrived", f.t); break; }
  CHECK(shown(5100).allAre(WHITE), "the next real beat (5000) still accents");
}
// C2: a correction that moves the beat position by less than a beat never moves the section
// cursor backwards (it would replay the end of the last section for a few frames).
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
// ...but a seek does: a C with a tempo, or one that moves a beat or more, goes where it says.
static void a_seek_goes_back() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=ff00ff,at=0"); at(1002, "!P*A:i=solid,c=00ffff,at=8");
  at(2000, "!**C:bpm=120,bpb=4,beat=0"); at(7000, "!**C:bpm=120,bpb=4,beat=2");
  runTo(8000);
  CHECK(shown(6900).allAre(CYAN), "section 2 is playing at beat 10");
  CHECK(shown(7900).allAre(MAGENTA), "a seek back to beat 2 plays section 1 again");
}
// S4: a live look less than 170 ms after the last applied one is held, the lines held in that window
// merge key by key, and the result is applied when the window ends.
static void live_looks_inside_170ms_are_held_and_merged() {
  boot(0x00, 0x01, 200);
  at(1000, "!P*A:i=solid,c=00ff00"); at(1050, "!P*A:c=ff00ff"); at(1090, "!P*A:b=128");
  runTo(1400);
  CHECK(shown(1150).allAre(GREEN) && shown(1150).scale == 200, "inside 170 ms the first look holds");
  CHECK(shown(1300).allAre(MAGENTA) && shown(1300).scale == 100, "then the held lines apply together: colour and b=128");
  uint32_t change = 0;
  for (const Frame& f : between(1000, 1400)) if (f.allAre(MAGENTA)) { change = f.t; break; }
  CHECK(change >= 1170 && change <= 1200, "the held look applies when the window ends (1170), saw t=%u", change);
}
// A held look does not outlive a reset: X while one is waiting stays dark.
static void x_drops_a_held_look() {
  boot();
  at(1000, "!P*A:i=solid,c=00ff00"); at(1050, "!P*A:c=ff00ff"); at(1100, "!P*X");
  runTo(2000);
  CHECK(mostLit(1130, 2000) == 0, "after X nothing is applied late (saw %u lit)", mostLit(1130, 2000));
}

// ---- section 6: capacity and every-N accents; section 11: lines back to back ----------------------
// Every section of a full table plays (16 in the serial-only build, 12 with I2C), and the whole
// load arrives as ONE burst with no gap between lines, over 400 bytes of it.
static void a_full_table_arrives_in_one_burst_and_plays() {
  boot();
  at(1000, "!**M:v=show");
  static char buf[PSI_SCORE_CAP][48]; size_t bytes = 0;
  for (int i = 0; i < PSI_SCORE_CAP; i++) {
    // 16 lines of 28 bytes or 12 of 38: over 400 bytes either way, and inside the mock's 512-byte RX buffer
    snprintf(buf[i], sizeof buf[i], PSI_SCORE_CAP > 12 ? "!P*A:i=solid,c=00%02x00,at=%d" : "!P*A:i=solid,c=00%02x00,s=128,b=255,at=%d",
             0x10 + i, i * 2);
    at(1001, buf[i]); bytes += strlen(buf[i]) + 1;
  }
  at(1100, "!**C:bpm=120,bpb=4,beat=0");
  runTo(1100 + PSI_SCORE_CAP * 1000 + 500);
  CHECK(bytes > 400, "the burst is over 400 bytes (%u)", (unsigned)bytes);
  for (int i = 0; i < PSI_SCORE_CAP; i++) {
    const uint32_t t = 1100 + (uint32_t)(i * 2) * 500 + 600;
    CHECK(shown(t).px[20].g == 0x10 + i, "section %d (beat %d) did not play: green=%02x at t=%u", i, i * 2, shown(t).px[20].g, t);
  }
}
// an=2,ap=1: with am=2, accent the beats where beat % 2 == 1. This is what lets a delivered show
// alternate front and rear by beat.
static void every_second_beat_accents() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=000040,at=0,am=2,an=2,ap=1,ae=solid,ac=ffffff");
  at(2000, "!**C:bpm=60,bpb=4,beat=0");                // beats at 2000, 3000, ...: odd beats at 3000, 5000, 7000
  runTo(8500);
  const std::vector<uint32_t> on = onsets(2000, 8400, WHITE);
  CHECK(on.size() == 3, "an=2,ap=1 accents beats 1, 3 and 5 only, saw %u", (unsigned)on.size());
  CHECK(!on.empty() && on[0] >= 3000 && on[0] < 3100, "the first accent is on beat 1 (t=3000), saw t=%u", on.empty() ? 0 : on[0]);
}
// A scored line of 102 characters, the longest v1.3 line, arrives whole: its LAST key is what the
// check reads (ad=2550, a 2.55 s accent), so a buffer one byte short would cut it. (The length is
// made up with leading zeros so that the look is one the check can see on both builds.)
static void the_longest_line_arrives_whole() {
  static const char line[] =
    "!P*A:i=solid,c=3b82f6,s=255,b=255,at=000000000000000,am=2,m=000,an=16,ap=00,ae=solid,ac=ffffff,ad=2550";
  static_assert(sizeof line - 1 == 102, "the longest v1.3 scored line is 102 characters");
  boot();
  at(1000, "!**M:v=show"); at(1001, line); at(2000, "!**C:bpm=120,bpb=4,beat=0");
  runTo(6000);
  uint32_t last = 0;
  for (const Frame& f : between(2000, 6000)) { if (!f.allAre(WHITE)) break; last = f.t; }
  CHECK(shown(2100).allAre(WHITE), "the accent is up at beat 0");
  CHECK(last >= 2000 + 2550 - 30 && last <= 2000 + 2550 + 30, "ad=2550 arrived whole: the accent lasts 2.55 s, saw %u ms", last - 2000);
  CHECK(shown(5000).allAre(0x3b82f6), "the section's own look returns after it");
}

// ---- S7: native codes are an allow-list -----------------------------------------------------------
// A line naming a code off the list is dropped whole: the look, the colour and everything else on
// it. 2, 3 and 5 are upstream's Flash, Alarm and Scream; 23 and 99 are no mode at all.
static void native_codes_off_the_list_are_dropped() {
  boot();
  at(1000, "!P*A:i=solid,c=00ff00"); at(2000, "!P*A:i=native:2,c=ff00ff"); at(3000, "!P*A:i=native:99");
  at(4000, "!P*A:i=native:3"); at(5000, "!P*A:i=native:5,b=10"); at(6000, "!P*A:i=native:23"); at(7000, "!P*A:i=native:-1");
  runTo(8000);
  for (const Frame& f : between(1100, 8000))
    if (!f.allAre(GREEN) || f.scale != 20) { CHECK(false, "t=%u: the look changed; a line naming native 2, 3, 5, 23, 99 or -1 is dropped whole", f.t); break; }
}
// ...and every code on the list is taken: each draws something of its own, and none is the solid
// look it replaced.
static void native_codes_on_the_list_run() {
  static const int modes[] = {1, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 92};
  const int n = (int)(sizeof modes / sizeof modes[0]);
  char line[20][32];
  boot();
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00");
  for (int k = 0; k < n; k++) { snprintf(line[k], sizeof line[k], "!P*A:i=native:%d", modes[k]); at(2000 + 2000 * k, line[k]); }
  at(2000 + 2000 * n, "!P*A:i=native:0");
  runTo(2000 + 2000 * n + 1000);
  for (int k = 0; k < n; k++) {
    const uint32_t a = 2100 + 2000 * k, b = 3990 + 2000 * k;
    bool green = false;
    for (const Frame& f : between(a, b)) if (f.allAre(GREEN)) green = true;
    CHECK(!green && mostLit(a, b) > 0, "native:%d did not run (still the solid look, or dark)", modes[k]);
  }
  CHECK(shown(2000 + 2000 * n + 900).lit == 0, "native:0 is the board's own off");
}
// A scored line naming a code off the list is not stored, so V sees a line missing.
static void a_scored_native_off_the_list_fails_v() {
  boot();
  static char v[48];
  static const char* good = "P*A:i=native:15,at=0"; static const char* bad = "P*A:i=native:2,at=8";
  snprintf(v, sizeof v, "!PFV:n=2,k=%04x", (unsigned)(crc16(good) ^ crc16(bad)));
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=native:15,at=0"); at(1002, "!P*A:i=native:2,at=8"); at(1003, "!PFQ:r=0");
  at(1004, v); at(1005, "!PFQ:r=0");
  runTo(3000);
  CHECK(strstr(Serial1.txLog, ",n=1,ok=0,") != nullptr, "only the allowed section was stored (tx: %s)", Serial1.txLog);
  CHECK(strstr(Serial1.txLog, ",n=0,ok=2,") != nullptr && swipeAt(2900), "V fails and the board is given back (tx: %s)", Serial1.txLog);
}

// ---- native corner cases the upstream differential found ----------------------------------------
// 0T19|3: upstream's comment says the lightsaber battle ignores a timing. It plays its whole 5.4 s.
static void the_saber_ignores_a_command_timing() {
  boot();
  at(2000, "0T19|3");
  runTo(9000);
  uint32_t last = 0;
  for (const Frame& f : between(2000, 9000)) if (f.lit > 0 && f.lit < 48) last = f.t;
  CHECK(last >= 7300, "the battle must play to its end (5.4 s), last saber frame at t=%u", last);
}

// ---- L2: the host-silence watchdog ----------------------------------------------------------------
static void watchdog_gives_the_panel_back() {
  boot();
  at(1000, "!**M:v=show,wd=2"); at(1100, "!P*A:i=solid,c=00ff00");
  runTo(4000);
  CHECK(shown(2900).allAre(GREEN), "inside wd the look holds");
  CHECK(swipeAt(3900), "2 s of silence must end the show (swipe back)");
}
// Any well-formed line addressed to the board is a keepalive, a wildcard Q that is never answered
// included. A line for another board, or for the other PSI, is not.
static void watchdog_is_fed_by_any_addressed_line() {
  boot();
  at(1000, "!**M:v=show,wd=2"); at(1100, "!P*A:i=solid,c=00ff00");
  for (uint32_t t = 2500; t < 9000; t += 1500) at(t, "!**Q");
  runTo(9000);
  CHECK(shown(8900).allAre(GREEN), "a line every 1.5 s keeps a wd=2 show alive");
}
static void watchdog_is_not_fed_by_another_boards_lines() {
  boot();                                              // a front PSI
  at(1000, "!**M:v=show,wd=2"); at(1100, "!P*A:i=solid,c=00ff00");
  for (uint32_t t = 2000; t < 6000; t += 500) { at(t, "!PRQ"); at(t + 100, "!L*A:i=solid,c=ffffff"); at(t + 200, "!P*garbage"); }
  runTo(6000);
  CHECK(shown(2900).allAre(GREEN), "inside wd the look holds");
  CHECK(swipeAt(3900) && swipeAt(5900), "lines for the rear PSI, for the logics, and malformed ones do not feed it");
}
// X leaves the watchdog armed: a host that blacks the dome out and then dies still gives it back.
static void x_leaves_the_watchdog_armed() {
  boot();
  at(1000, "!**M:v=show,wd=2"); at(1100, "!P*A:i=solid,c=00ff00"); at(2000, "!P*X");
  runTo(5000);
  CHECK(shown(3900).lit == 0, "X is dark while the host is only just silent");
  CHECK(swipeAt(4900), "2 s after the last line the board is given back, although X was the last word");
}

// ---- S9: one limit for every path (the field gate, at the place every frame leaves) --------------
// The hostile streams are test_property.cpp's. These four show the rule itself.
// A relight right after a blackout: the dark is immediate, the light waits until 340 ms after the
// panel last lit, and until then the frame is held under a quarter of the ceiling.
static void a_relight_right_after_x_is_held() {
  boot(0x00, 0x01, 200);
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=off"); at(2000, "!P*A:i=solid,c=ffffff");
  at(2200, "!P*X"); at(2250, "!P*A:i=solid,c=ffffff");
  runTo(3000);
  uint32_t lit = 0;
  for (const Frame& f : between(2000, 2100)) if (f.scale == 200 && f.lit == 48) { lit = f.t; break; }
  CHECK(lit != 0, "the first light, after a long dark, is not held");
  CHECK(shown(2240).lit == 0, "X is dark at once");
  for (const Frame& f : between(2250, lit + 339))
    if (f.lit && f.scale > 50) { CHECK(false, "t=%u: relit at %u only %u ms after the panel last lit (t=%u)", f.t, f.scale, f.t - lit, lit); break; }
  CHECK(shown(lit + 380).allAre(WHITE) && shown(lit + 380).scale == 200, "340 ms after it last lit the look is at its level");
}
// The native look the panel is given back to starts no sooner either: X, then M:v=idle, again and again.
static void the_swipe_after_idle_is_held_too() {
  boot(0x00, 0x01, 200);
  at(1000, "!P*A:i=off");
  for (uint32_t t = 2000; t < 5000; t += 100) { at(t, "!P*X"); at(t + 50, "!**M:v=idle"); }
  runTo(6000);
  std::vector<uint32_t> on; bool was = false;
  for (const Frame& f : between(2000, 5000)) {
    const bool is = f.lit >= 24 && f.scale >= 100;       // half the field at half the ceiling, or more
    if (is && !was) on.push_back(f.t);
    was = is;
  }
  CHECK(on.size() >= 3, "the swipe does come back between blackouts (%u times in 3 s)", (unsigned)on.size());
  for (size_t k = 1; k < on.size(); k++) CHECK(on[k] - on[k - 1] >= 340, "two relights %u ms apart (t=%u)", on[k] - on[k - 1], on[k]);
  CHECK(shown(5900).lit == 48 && shown(5900).scale == 200, "when the stream ends the swipe is back at the owner's 200");
}
// S10: the native look's frames are never dimmed. What waits is the hand-over: the panel stays as
// it is (dark) until the gate's window ends, and then the swipe is there at the owner's brightness
// with its first frame.
static void the_swipe_starts_when_the_window_ends() {
  boot(0x00, 0x01, 200);
  at(1000, "!P*A:i=off"); at(2000, "!P*A:i=solid,c=ffffff"); at(2200, "!P*X"); at(2260, "!**M:v=idle");
  runTo(3000);
  uint32_t lit = 0, back = 0;
  for (const Frame& f : between(2000, 2100)) if (f.scale == 200 && f.lit == 48) { lit = f.t; break; }
  for (const Frame& f : between(2260, 3000)) if (f.lit) { back = f.t; break; }
  CHECK(lit != 0, "the look lights");
  CHECK(back >= lit + 340 && back <= lit + 340 + 27, "the swipe starts within a frame of the window's end (t=%u), saw t=%u", lit + 340, back);
  for (const Frame& f : between(2260, 3000))
    if (f.lit && f.scale != 200) { CHECK(false, "t=%u: a swipe frame at %u; native frames are never dimmed", f.t, f.scale); break; }
}
// Nothing the contract holds is ever brighter than the ceiling, whatever the primitive asked for
// (upstream's bare show() latches a brightness stored at boot, not the owner's current one). Boot
// at 200, then the owner turns it down to 20, then a show runs every native mode it may select.
// (This held before the gate's clamp too; it is here so that it keeps holding.)
static void a_show_never_exceeds_the_ceiling() {
  static const int modes[] = {0, 1, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 92};
  const int n = (int)(sizeof modes / sizeof modes[0]);
  char line[21][32];
  boot(0x00, 0x01, 200);
  at(500, "3P20"); at(1000, "!**M:v=show");
  for (int k = 0; k < n; k++) { snprintf(line[k], sizeof line[k], "!P*A:i=native:%d", modes[k]); at(1500 + 1500 * k, line[k]); }
  runTo(1500 + 1500 * n);
  unsigned frames = 0;
  for (const Frame& f : between(1530, 1500 + 1500 * n)) {
    frames++;
    if (f.scale > 20) { CHECK(false, "t=%u (native:%d): a frame at %u, above the ceiling 20", f.t, modes[(f.t - 1500) / 1500], f.scale); break; }
  }
  CHECK(frames > 300, "the native modes draw (%u frames)", frames);
}
// R1: no overlay on a native:<n> look, scored or live, so an accent never restarts a native sequence.
static void no_pulse_over_a_contract_native_look() {
  boot();
  at(1000, "!P*A:i=native:15"); at(2500, "!P*P:c=ffffff,d=300");
  runTo(4000);
  CHECK(onsets(2500, 4000, WHITE).empty(), "a P over native:15 draws nothing");
  CHECK(mostLit(2500, 4000) > 0 && mostLit(2500, 4000) <= 6, "Knight Rider runs on");
}

// ---- S10: native looks run as stock; the contract holds only their edges ---------------------------
// Leia (6) and Knight Rider (15) move a small lit area fast. Under a show their frames go out
// exactly as the stock firmware draws them: never scaled down, at the show's level.
static void a_native_look_runs_as_stock_under_a_show() {
  boot();
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=native:6"); at(5000, "!P*A:i=native:15");
  runTo(9000);
  unsigned frames = 0, lit = 0;
  for (const Frame& f : between(1500, 9000)) {
    frames++; if (f.lit) lit++;
    if (f.lit && f.scale != 20) { CHECK(false, "t=%u: a native frame at scale %u; the show's level is 20", f.t, f.scale); break; }
  }
  CHECK(frames > 60 && lit > 30, "Leia and Knight Rider draw (%u frames, %u lit)", frames, lit);
}
// The start of a native look is the contract's edge. Started from dark inside the gate's window, it
// comes up dim (the brightness the show gives it is held) and is at its level when the window ends.
static void a_native_look_started_in_the_window_comes_up_dim() {
  boot(0x00, 0x01, 200);
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=off"); at(2000, "!P*A:i=solid,c=ffffff"); at(2200, "!P*X"); at(2260, "!P*A:i=native:17");
  runTo(3200);
  uint32_t lit = 0;
  for (const Frame& f : between(2000, 2100)) if (f.scale == 200 && f.lit == 48) { lit = f.t; break; }
  CHECK(lit != 0, "the look lights");
  for (const Frame& f : between(2260, lit + 339))
    if (f.lit && f.scale > 50) { CHECK(false, "t=%u: native:17 at %u only %u ms after the panel last lit", f.t, f.scale, f.t - lit); break; }
  CHECK(shown(lit + 400).lit == 48 && shown(lit + 400).scale == 200, "then it is at the show's level (saw %u)", shown(lit + 400).scale);
}
// So is the brightness a show gives it: B up under a native look is held like any other relight.
static void b_under_a_native_look_is_held() {
  boot(0x00, 0x01, 200);
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=native:17");
  for (uint32_t t = 2000; t < 5000; t += 100) { at(t, "!P*B:v=0"); at(t + 50, "!P*B:v=255"); }
  runTo(6000);
  std::vector<uint32_t> on; bool was = true;
  for (const Frame& f : between(1900, 5100)) {
    const bool is = f.lit >= 24 && f.scale >= 100;
    if (is && !was) on.push_back(f.t);
    was = is;
  }
  CHECK(on.size() >= 3, "the look does come back up between the dips (%u times in 3 s)", (unsigned)on.size());
  for (size_t k = 1; k < on.size(); k++) CHECK(on[k] - on[k - 1] >= 340, "two relights %u ms apart (t=%u)", on[k] - on[k - 1], on[k]);
  CHECK(shown(5900).scale == 200, "and it ends at the show's level");
}

// ---- core revision CA1.2: what a line may and may not do ---------------------------------------
// A scored line delivered twice (a mesh retry) changes nothing: the section that is playing does
// not restart. A DIFFERENT line for the same beat replaces the entry, and the look follows.
static void a_repeated_scored_line_changes_nothing() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=scan,c=00ff00,s=200,at=0"); at(1100, "!**C:bpm=120,bpb=4,beat=0");
  at(2000, "!P*A:i=scan,c=00ff00,s=200,at=0");        // the same bytes again
  at(4000, "!P*A:i=solid,c=ff00ff,at=0");             // another line for beat 0
  runTo(5000);
  uint32_t start = 0;                                 // the section's own time zero: its first lit frame
  for (const Frame& f : between(1100, 1300)) if (f.lit) { start = f.t; break; }
  CHECK(start != 0, "the scan section starts");
  unsigned bad = 0;
  for (const Frame& f : between(2030, 3900)) {
    const int want = fxScanPos(f.t - start, 200, COLUMNS, false);
    const int led = psiLed((uint8_t)want, 2);
    if (led >= 0 && f.px[led].g != 255) bad++;
  }
  CHECK(bad == 0, "after the repeat the scan carries on from where it was (%u frames off its own timeline)", bad);
  CHECK(shown(4900).allAre(MAGENTA), "a different line for the same beat replaces the section");
}
// S1: a bpm= that is not a plain number makes the whole C ignored, so a corrupted byte can neither
// stop a show (bpm=0 is Pause) nor move it.
static void a_bpm_that_is_not_a_number_is_ignored() {
  boot();
  at(1000, "!**M:v=show"); at(1001, "!P*A:i=solid,c=ff00ff,at=0"); at(1002, "!P*A:i=solid,c=00ffff,at=4");
  at(1100, "!**C:bpm=120,bpb=4,beat=0");              // beat 4 lands at 3100
  at(2000, "!**C:bpm=,beat=0"); at(2010, "!**C:bpm=-0"); at(2020, "!**C:bpm=0x"); at(2030, "!**C:bpm=12a,beat=0");
  runTo(4000);
  CHECK(shown(3000).allAre(MAGENTA) && shown(3200).allAre(CYAN), "the clock ran on: section 2 starts at 3100 as seeded");
}

#ifdef PSI_ENABLE_I2C
// ---- the I2C build: the TWI interrupt only queues ------------------------------------------------
// A contract line from an I2C master is copied in the interrupt and applied by loop(): nothing is
// parsed or drawn in interrupt context.
static void an_i2c_contract_line_is_applied_by_loop() {
  boot();
  runTo(1000);
  const size_t before = g_frames.size();
  Wire.feed("!P*A:i=solid,c=00ff00\r"); receiveEvent(22);
  CHECK(g_frames.size() == before, "the interrupt drew %u frame(s)", (unsigned)(g_frames.size() - before));
  runTo(1100);
  CHECK(shown(1090).allAre(GREEN), "loop() applies the queued line");
  atI2c(2000, "!P*P:c=ffffff,d=100");
  runTo(3000);
  CHECK(onsets(2000, 3000, WHITE).size() == 1, "a queued line is applied once, saw %u", (unsigned)onsets(2000, 3000, WHITE).size());
}
// L6 over I2C: the native command runs in the interrupt, and loop() ends the show before the next frame.
static void an_i2c_native_command_takes_the_panel_back() {
  boot();
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00"); atI2c(2000, "0T1");
  runTo(3000);
  CHECK(shown(1900).allAre(GREEN), "the show holds the panel");
  for (const Frame& f : between(2001, 3000))
    if (f.allAre(GREEN) || f.scale != 20) { CHECK(false, "t=%u: the show still holds the panel after 0T1 over I2C", f.t); break; }
  CHECK(swipeAt(2900), "the swipe is back at 20");
}
#endif

static const Scenario kScenarios[] = {
  {"no_wd_means_no_watchdog", no_wd_means_no_watchdog},
  {"out_of_range_bpm_is_ignored", out_of_range_bpm_is_ignored},
  {"pump_recurs_no_faster_than_340ms", pump_recurs_no_faster_than_340ms},
  {"brightness_is_a_fraction_of_the_owners", brightness_is_a_fraction_of_the_owners},
  {"b_is_only_honoured_in_a_show", b_is_only_honoured_in_a_show},
  {"b_is_cleared_by_a_reset", b_is_cleared_by_a_reset},
  {"a_pulse_b_does_not_stick", a_pulse_b_does_not_stick},
  {"scored_b_is_carried", scored_b_is_carried},
  {"the_ceiling_is_taken_when_the_show_arms", the_ceiling_is_taken_when_the_show_arms},
  {"the_owners_3p_survives_a_show", the_owners_3p_survives_a_show},
  {"the_owners_3p_mid_show_is_kept", the_owners_3p_mid_show_is_kept},
  {"the_pot_turned_mid_show_is_kept", the_pot_turned_mid_show_is_kept},
  {"show_keeps_the_swipe_until_its_first_line", show_keeps_the_swipe_until_its_first_line},
  {"a_retired_d_does_not_end_a_look", a_retired_d_does_not_end_a_look},
  {"a_resent_show_waits_for_its_own_seed", a_resent_show_waits_for_its_own_seed},
  {"a_resent_show_replaces_the_last", a_resent_show_replaces_the_last},
  {"x_clears_the_score", x_clears_the_score},
  {"idle_clears_the_score", idle_clears_the_score},
  {"show_after_x_does_not_replay_the_last_look", show_after_x_does_not_replay_the_last_look},
  {"x_in_a_show_stays_dark_until_idle", x_in_a_show_stays_dark_until_idle},
  {"idle_returns_to_swipe_after_a_native_look", idle_returns_to_swipe_after_a_native_look},
  {"x_is_dark_until_idle_then_the_swipe", x_is_dark_until_idle_then_the_swipe},
  {"a_native_command_takes_the_panel_back", a_native_command_takes_the_panel_back},
  {"a_native_command_ends_an_armed_show", a_native_command_ends_an_armed_show},
  {"pulse_outside_a_show_returns_to_native", pulse_outside_a_show_returns_to_native},
  {"pulse_over_a_native_mode_returns_to_that_mode", pulse_over_a_native_mode_returns_to_that_mode},
  {"an_a_with_no_look_is_solid", an_a_with_no_look_is_solid},
  {"a_hold_freezes_the_show", a_hold_freezes_the_show},
  {"every_look_latches_frames", every_look_latches_frames},
#ifndef PSI_NO_STRIP_LOOKS
  {"strip_looks_draw_their_shapes", strip_looks_draw_their_shapes},
  {"the_colour_scaler_is_exact", the_colour_scaler_is_exact},
#endif
  {"a_build_section_ramps", a_build_section_ramps},
  {"a_downbeat_accent_fires_on_downbeats_only", a_downbeat_accent_fires_on_downbeats_only},
  {"an_every_beat_accent_fires_once_a_beat", an_every_beat_accent_fires_once_a_beat},
  {"no_ae_means_no_overlay", no_ae_means_no_overlay},
  {"an_accent_swaps_the_effect_at_the_units_level", an_accent_swaps_the_effect_at_the_units_level},
  {"a_reseed_accents_its_first_beat", a_reseed_accents_its_first_beat},
  {"a_new_show_accents_its_first_beat", a_new_show_accents_its_first_beat},
  {"flash_alternates_no_faster_than_the_cap", flash_alternates_no_faster_than_the_cap},
  {"the_accent_cool_down_cannot_be_bypassed", the_accent_cool_down_cannot_be_bypassed},
  {"a_verified_load_plays_and_reports_ok", a_verified_load_plays_and_reports_ok},
  {"a_line_delivered_twice_is_harmless", a_line_delivered_twice_is_harmless},
  {"a_bad_checksum_gives_the_panel_back", a_bad_checksum_gives_the_panel_back},
  {"a_short_count_gives_the_panel_back", a_short_count_gives_the_panel_back},
  {"an_overflowing_load_is_bad", an_overflowing_load_is_bad},
  {"a_failed_load_drops_scored_lines_until_the_next_show", a_failed_load_drops_scored_lines_until_the_next_show},
  {"a_scored_line_without_a_look_is_dropped", a_scored_line_without_a_look_is_dropped},
  {"the_query_reply_is_byte_exact", the_query_reply_is_byte_exact},
  {"a_query_that_cannot_be_routed_is_not_answered", a_query_that_cannot_be_routed_is_not_answered},
  {"scan_is_the_shared_ping_pong", scan_is_the_shared_ping_pong},
  {"flash_speed_is_the_shared_timing", flash_speed_is_the_shared_timing},
  {"pulse_breathes_on_the_shared_timing", pulse_breathes_on_the_shared_timing},
  {"a_pumped_pulse_follows_the_beat", a_pumped_pulse_follows_the_beat},
  {"no_accent_on_a_native_section", no_accent_on_a_native_section},
  {"a_meter_with_no_level_feed_walks", a_meter_with_no_level_feed_walks},
  {"a_native_vu_mode_walks_after_a_show", a_native_vu_mode_walks_after_a_show},
  {"reanchor_does_not_refire_the_accent", reanchor_does_not_refire_the_accent},
  {"a_scored_line_mid_beat_does_not_refire", a_scored_line_mid_beat_does_not_refire},
  {"a_nudge_never_flips_the_section_back", a_nudge_never_flips_the_section_back},
  {"a_seek_goes_back", a_seek_goes_back},
  {"live_looks_inside_170ms_are_held_and_merged", live_looks_inside_170ms_are_held_and_merged},
  {"x_drops_a_held_look", x_drops_a_held_look},
  {"a_full_table_arrives_in_one_burst_and_plays", a_full_table_arrives_in_one_burst_and_plays},
  {"every_second_beat_accents", every_second_beat_accents},
  {"the_longest_line_arrives_whole", the_longest_line_arrives_whole},
  {"native_codes_off_the_list_are_dropped", native_codes_off_the_list_are_dropped},
  {"native_codes_on_the_list_run", native_codes_on_the_list_run},
  {"a_scored_native_off_the_list_fails_v", a_scored_native_off_the_list_fails_v},
  {"the_saber_ignores_a_command_timing", the_saber_ignores_a_command_timing},
  {"watchdog_gives_the_panel_back", watchdog_gives_the_panel_back},
  {"watchdog_is_fed_by_any_addressed_line", watchdog_is_fed_by_any_addressed_line},
  {"watchdog_is_not_fed_by_another_boards_lines", watchdog_is_not_fed_by_another_boards_lines},
  {"x_leaves_the_watchdog_armed", x_leaves_the_watchdog_armed},
  {"a_relight_right_after_x_is_held", a_relight_right_after_x_is_held},
  {"the_swipe_after_idle_is_held_too", the_swipe_after_idle_is_held_too},
  {"the_swipe_starts_when_the_window_ends", the_swipe_starts_when_the_window_ends},
  {"a_show_never_exceeds_the_ceiling", a_show_never_exceeds_the_ceiling},
  {"no_pulse_over_a_contract_native_look", no_pulse_over_a_contract_native_look},
  {"a_native_look_runs_as_stock_under_a_show", a_native_look_runs_as_stock_under_a_show},
  {"a_native_look_started_in_the_window_comes_up_dim", a_native_look_started_in_the_window_comes_up_dim},
  {"b_under_a_native_look_is_held", b_under_a_native_look_is_held},
  {"a_repeated_scored_line_changes_nothing", a_repeated_scored_line_changes_nothing},
  {"a_bpm_that_is_not_a_number_is_ignored", a_bpm_that_is_not_a_number_is_ignored},
#ifdef PSI_ENABLE_I2C
  {"an_i2c_contract_line_is_applied_by_loop", an_i2c_contract_line_is_applied_by_loop},
  {"an_i2c_native_command_takes_the_panel_back", an_i2c_native_command_takes_the_panel_back},
#endif
};
