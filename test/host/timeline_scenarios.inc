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
  boot();
  at(1000, "!P*A:i=solid,c=00ff00"); at(2000, "!P*P:c=ffffff,d=200,b=64");
  runTo(3000);
  CHECK(shown(2900).allAre(GREEN) && shown(2900).scale == 20, "after P:b=64 the look is back at 20, saw %u",
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

// ---- the query ack (v1.2 form until verb V and the routed reply land) -----------------------------
// Studio parses these exact bytes: unpadded decimal for i= and bpm=, two lower-case hex digits a
// colour byte, a trailing \r, and nothing for a query not addressed to this unit by name.
static void the_query_ack_is_byte_exact() {
  boot();
  at(1000, "!P*A:i=solid,c=0080ff"); at(1001, "!**C:bpm=128,ph=0,bpb=4,beat=0"); at(1100, "!PFQ");
  at(2000, "!P*A:i=rainbow,c=0A0B0C"); at(2001, "!**C:bpm=30,ph=0,bpb=4,beat=0"); at(2100, "!PFQ");
  at(3000, "!**C:bpm=7,ph=0,bpb=4,beat=0"); at(3100, "!PFQ");      // S1: out of range, the clock keeps 30
  at(4000, "!**C:bpm=0"); at(4100, "!PFQ");                         // C6: a held clock reports 0
  at(5000, "!P*Q"); at(5100, "!PRQ"); at(5200, "!**Q");             // not this unit by name: no ack
  runTo(6000);
  const char* tx = Serial1.txLog;
  CHECK(strstr(tx, "!PFq:ver=1.2,phase=2,i=2,c=0080ff,bpm=128\r") != nullptr, "solid, mid colour, 3-digit bpm (tx: %s)", tx);
  CHECK(countOf(tx, "!PFq:ver=1.2,phase=2,i=5,c=0a0b0c,bpm=30\r") == 2, "zero-padded hex; bpm=7 is ignored (tx: %s)", tx);
  CHECK(strstr(tx, "!PFq:ver=1.2,phase=2,i=5,c=0a0b0c,bpm=0\r") != nullptr, "a held clock reads 0 (tx: %s)", tx);
  CHECK(countOf(tx, "q:ver=") == 4, "four queries by name, four acks: saw %u", countOf(tx, "q:ver="));
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
// pulse: with nothing pumping the look it breathes on fxPulseBright(): 340 ms a cycle at s=255,
// never faster (S8), 2 s a cycle at s=0. Under the beat pump the envelope is the breathe.
static void pulse_breathes_on_the_shared_timing() {
  boot(0x00, 0x01, 200);
  at(1000, "!P*A:i=pulse,c=ffffff,s=255"); at(6000, "!P*A:i=pulse,c=ffffff,s=0");
  runTo(13000);
  for (const Frame& f : between(1100, 5900)) {
    const uint8_t want = (uint8_t)(((uint16_t)200 * fxPulseBright(f.t - 1000, 255)) / 255u);
    if (f.scale != want) { CHECK(false, "t=%u: the breathe is %u, the core says %u", f.t, f.scale, want); break; }
  }
  const unsigned fast = pumpPeaks(1100, 5900), slow = pumpPeaks(6500, 12500);
  CHECK(fast >= 12 && fast <= 14, "s=255 is a 340 ms cycle: 14 peaks in 4.8 s, never more; saw %u", fast);
  CHECK(slow == 3, "s=0 is a 2 s cycle: 3 peaks in 6 s, saw %u", slow);
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
  {"the_query_ack_is_byte_exact", the_query_ack_is_byte_exact},
  {"scan_is_the_shared_ping_pong", scan_is_the_shared_ping_pong},
  {"flash_speed_is_the_shared_timing", flash_speed_is_the_shared_timing},
  {"pulse_breathes_on_the_shared_timing", pulse_breathes_on_the_shared_timing},
  {"a_pumped_pulse_follows_the_beat", a_pumped_pulse_follows_the_beat},
  {"no_accent_on_a_native_section", no_accent_on_a_native_section},
  {"a_meter_with_no_level_feed_walks", a_meter_with_no_level_feed_walks},
  {"a_native_vu_mode_walks_after_a_show", a_native_vu_mode_walks_after_a_show},
#ifdef PSI_ENABLE_I2C
  {"an_i2c_contract_line_is_applied_by_loop", an_i2c_contract_line_is_applied_by_loop},
  {"an_i2c_native_command_takes_the_panel_back", an_i2c_native_command_takes_the_panel_back},
#endif
};
