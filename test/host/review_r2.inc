// test/host/review_r2.inc — REVIEW FINDINGS on the R2 line (git diff 0e87a91..be6e9d2: core CA2,
// contract v1.4, the `vu` look). Written by an independent review, 2026-11-21. Every scenario here
// is a claim of the spec or of DROIDNET_FORK.md that the firmware does not keep today: each one
// FAILS until its finding is fixed, and none of them is a scenario of the layer's own suite.
//
// Built by run.sh's last stage ("[R2 review]") through test_timeline.cpp with
// -DTIMELINE_SCENARIOS="review_r2.inc", on the default environment's flags (-DPSI_NO_STRIP_LOOKS).
// It includes the layer's own scenario file for its helpers (rowOnsets, worstSecond, closestTwo,
// rowsLitAt, hashFrames, framesOfChild) and runs only the scenarios below.
//
// The row observer is the layer's own (timeline_scenarios.inc, "v1.4 N10 to N12"): a row is lit
// when half its LEDs are on at a scale of half the owner's ceiling or more (100 at the ceiling of
// 200 its scenarios use, so the same observer there), dark when a tenth or fewer are on; no row may
// light more than three times in a second, nor twice inside 340 ms (v1.4 section 9).
#define kScenarios kScenariosOfTheLayer
#include "timeline_scenarios.inc"
#undef kScenarios

// The layer's rowOnsets() with "lit" at half the owner's ceiling instead of a fixed scale of 100.
static void rowOnsetsAt(uint32_t from, uint32_t to, int ceil, std::vector<uint32_t> on[LEDS_PER_COLUMN]) {
  bool lit[LEDS_PER_COLUMN];
  for (int r = 0; r < LEDS_PER_COLUMN; r++) lit[r] = true;     // a row lit before `from` is not an onset
  for (const Frame& f : between(from, to))
    for (int r = 0; r < LEDS_PER_COLUMN; r++) {
      unsigned n = 0, onLeds = 0;
      for (int c = 0; c < COLUMNS; c++) {
        const int led = psiLed((uint8_t)c, (uint8_t)r);
        if (led < 0) continue;
        n++; if (f.px[led].r | f.px[led].g | f.px[led].b) onLeds++;
      }
      const bool dark = f.scale == 0 || onLeds * 10 <= n, bright = f.scale * 2 >= ceil && onLeds * 2 >= n;
      if (!lit[r] && bright) { on[r].push_back(f.t); lit[r] = true; } else if (lit[r] && dark) lit[r] = false;
    }
}
// true when every row keeps the gate in [from, to]; prints each row that does not, with its onsets.
static bool rowsKeep(const char* what, uint32_t from, uint32_t to, int ceil, bool quiet = false) {
  std::vector<uint32_t> on[LEDS_PER_COLUMN];
  rowOnsetsAt(from, to, ceil, on);
  bool ok = true;
  for (int r = 0; r < LEDS_PER_COLUMN; r++) {
    const unsigned most = worstSecond(on[r]);
    const uint32_t gap = on[r].size() > 1 ? closestTwo(on[r]) : 0xFFFFFFFFu;
    if (most <= 3 && gap >= 340) continue;
    ok = false;
    if (quiet) continue;
    printf("  %s: row %d lit %u times, %u in one second at the most, the closest two %u ms apart; lit at", what, r,
           (unsigned)on[r].size(), most, gap);
    for (size_t k = 0; k < on[r].size() && k < 24; k++) printf(" %u", on[r][k]);
    printf("\n");
  }
  return ok;
}

// ---- FINDING 1 (the highest): a scored vu with an accent blinks the bottom rows on every beat ----
// v1.4 section 4: "an overlay may run over each of them as a base look". One scored line, a vu
// section with a flash accent on every beat, and a clock: the accent lights the whole panel on the
// beat, its overlay ends 180 ms later with VUMeter()'s black frame, and on the next pass the pump's
// level (still high, 200 ms into the beat) is taken by the level hold, whose window the accent never
// touched, and the bottom rows light again: twice a beat, 208 ms apart, five times in a second. The
// rows are green (half a red row's level), so at the bench brightness (20) and at 200 the relight
// is under the field gate's step and nothing else holds it. The same line with i=meter (R1's look)
// keeps the gate. (test_property's region observer, run by the same stage, sees the bottom band do
// it at the owner's 20: review_r2_profiles.inc.)
static void scoredVuAccent(uint8_t owner, const char* look, int bpm) {
  boot(0x00, 0x01, owner);
  char a[96], c[48];
  snprintf(a, sizeof a, "!P*A:i=%s,c=ffffff,s=255,am=2,m=255,at=0,ae=flash,ac=ffffff", look);
  snprintf(c, sizeof c, "!**C:bpm=%d,bpb=4,beat=0", bpm);
  at(1000, "!**M:v=show"); at(1100, a); at(1500, c);
  runTo(9600);
}
static void review_r2_a_scored_vu_accent_keeps_the_rows_at_the_owners_20() {
  scoredVuAccent(20, "vu", 120);
  CHECK(rowsKeep("scored vu, ae=flash every beat, 120 BPM, owner 20", 1600, 9600, 20),
        "a scored vu with an accent lights a row twice inside 340 ms or more than three times a second");
}
static void review_r2_a_scored_vu_accent_keeps_the_rows_at_200() {
  scoredVuAccent(200, "vu", 120);
  CHECK(rowsKeep("scored vu, ae=flash every beat, 120 BPM, owner 200", 1600, 9600, 200),
        "a scored vu with an accent lights a row twice inside 340 ms or more than three times a second");
}
// The control, which holds today: the same line with R1's meter (the walk under the accent).
static void review_r2_control_a_scored_meter_accent_keeps_the_rows() {
  scoredVuAccent(20, "meter", 120);
  CHECK(rowsKeep("scored meter, ae=flash every beat, owner 20", 1600, 9600, 20), "the R1 control broke");
}

// ---- FINDING 2: a pause or a resume hands the rows between the pump and the walk with no window ----
// DROIDNET_FORK.md: "a pause (C:bpm=0) stops the clock, so vu is the meter and walks"; BENCH_R2.md
// row 9 sends exactly that. The walk (VUMeter's random steps, every 170 ms at the fastest) and the
// pump (through the level hold's 340 ms window) each keep the gate alone; the hand-over between
// them keeps no time. One C:bpm=0, 80 ms after a beat whose rise the pump showed: the walk's next
// step relights the top row 286 ms after the pump lit it. One pause and one resume (a seed on beat
// 0): the walk lights the bottom row, the pump's first frame darkens it, the pump's rise relights it
// 208 ms later. (A:am=0 / A:m=0 and back hand over the same way.) Over 30 streams of pauses and
// resumes 150 to 1,500 ms apart, `review_r2_pauses_and_resumes_keep_the_rows` counts the streams
// in which a row lit twice inside 340 ms (6 of 30 today; the layer's region observer sees none,
// the rows are under a quarter of the field).
static void review_r2_a_pause_keeps_the_rows() {
  boot(0x00, 0x01, 200);
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=vu,c=ffffff,s=255,am=2,m=255"); at(1500, "!**C:bpm=120,bpb=4,beat=0");
  at(6605, "!**C:bpm=0");                                // 105 ms after the beat at 6500
  runTo(8600);
  CHECK(rowsKeep("a pumped vu, one C:bpm=0", 1600, 8600, 200), "a pause relit a row inside 340 ms");
}
static void review_r2_a_resume_keeps_the_rows() {
  boot(0x00, 0x01, 200);
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=vu,c=ffffff,s=255,am=2,m=255"); at(1500, "!**C:bpm=120,bpb=4,beat=0");
  at(2000, "!**C:bpm=0"); at(2346, "!**C:bpm=120,bpb=4,beat=0");
  runTo(4400);
  CHECK(rowsKeep("a pumped vu, a pause and a resume", 1600, 4400, 200), "a resume relit a row inside 340 ms");
}
static void pauseStream(int seed) {
  boot(0x00, 0x01, 200);
  uint32_t r = 0x9E3779B9u ^ (uint32_t)(seed * 2654435761u);
  auto rnd = [&r](uint32_t n) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r % n; };
  static const int kBpm[3] = { 120, 150, 176 };
  const int bpm = kBpm[rnd(3)];
  static char lines[2][48];
  snprintf(lines[0], sizeof lines[0], "!**C:bpm=%d,bpb=4,beat=0", bpm);
  snprintf(lines[1], sizeof lines[1], "!**C:bpm=0");
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=vu,c=ffffff,s=255,am=2,m=255"); at(1500, lines[0]);
  for (uint32_t t = 2000; t < 11000; ) {
    at(t, lines[1]); t += 150 + rnd(1351);
    at(t, lines[0]); t += 150 + rnd(1351);
  }
  runTo(12600);
}
static void review_r2_pauses_and_resumes_keep_the_rows() {
  int bad = 0;
  for (int s = 0; s < 30; s++) {
    fflush(stdout);
    const pid_t pid = fork();
    if (pid == 0) { pauseStream(s); _exit(rowsKeep("pause/resume stream", 1600, 12600, 200, true) ? 0 : 1); }
    int st = 0; waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { bad++; printf("  stream %d: a row lit twice inside 340 ms or four times in a second\n", s); }
  }
  CHECK(bad == 0, "%d of 30 pause/resume streams relit a row inside 340 ms", bad);
}

// ---- FINDING 3: the window "from the frame that shows a rise" misses a rise shown dimmer ----
// ContractPSI.h:477 moves the level's window to a sent frame only when the frame's FIELD level is
// above the last sent one's. A frame that shows the bars higher at a lower brightness (a b= or B
// fall, taken at once in the same pass) has a lower field level, so the window stays at the pass
// that took the rise. Colour changes on the vu (which the meter does not draw: they only hold the
// frame, the look gate's 170 ms) put that pass 150 ms before the frame: the next beat's rise, 340 ms
// after the take, lit row 3 again 208 ms after the frame that lit it. Seven lines, found by a random
// search of colour, b= and B lines at a pumped vu and reduced; 160 BPM, m=128, the owner's 200.
static void review_r2_a_rise_shown_dimmer_still_starts_the_window() {
  boot(0x00, 0x01, 200);
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=vu,c=ffffff,s=255,am=2,m=128"); at(1500, "!**C:bpm=160,bpb=4,beat=0");
  at(8583, "!P*B:b=220"); at(8675, "!P*A:c=a0e9ac,b=255"); at(8873, "!P*A:c=69a9c6"); at(9046, "!P*A:c=7dd721");
  at(9139, "!P*A:c=bad11c,b=220"); at(9335, "!P*A:c=954349,b=160"); at(9526, "!P*B:b=128");
  runTo(11000);
  CHECK(rowsKeep("a pumped vu, colour and brightness lines", 1600, 11000, 200), "a rise shown dimmer slipped the window");
}

// ---- FINDING 4: an L right after a switch from a pumped vu to the meter is dropped ----
// v1.4 H3: every v1.3 line means on a v1.4 board what it meant on a v1.3 board. ContractPSI.h:766
// drops an L when g_vuPumped is set, and g_vuPumped is the LAST tick's: an A:i=meter and an L:v=255
// in one burst (one pass), after a pumped vu, loses the L, and the meter walks instead of showing
// full bars. (After a pumped solid the same burst shows them: the control.)
static void meterThenL(const char* before) {
  boot(0x00, 0x01, 200);
  at(1000, "!**M:v=show"); at(1100, before); at(1500, "!**C:bpm=120,bpb=4,beat=0");
  at(3010, "!P*A:i=meter,c=ffffff,s=255"); at(3010, "!P*L:v=255");
  runTo(3400);
}
static void review_r2_an_l_just_after_a_pumped_vu_feeds_the_meter() {
  meterThenL("!P*A:i=vu,c=ffffff,s=255,am=2,m=255");
  CHECK(rowsLitAt(3100) == 6 && rowsLitAt(3300) == 6, "L:v=255 after A:i=meter: the bars are full; saw %u and %u rows",
        rowsLitAt(3100), rowsLitAt(3300));
}
static void review_r2_control_an_l_just_after_a_pumped_solid_feeds_the_meter() {
  meterThenL("!P*A:i=solid,c=ffffff,s=255,am=2,m=255");
  CHECK(rowsLitAt(3100) == 6 && rowsLitAt(3300) == 6, "the control broke: saw %u and %u rows", rowsLitAt(3100), rowsLitAt(3300));
}

// ---- FINDING 5: an L during an accent over a pumped vu is shown (v1.4 N12) ----
// N12: "An L that arrives while the pump runs is not shown." The pump runs while the clock runs and
// am= and m= are not 0; the layer clears g_vuPumped while an overlay is up, so an L that lands
// during a scored accent is taken, and the bars after the accent show it. The layer's own
// l_during_a_pump_changes_nothing has no accent.
static void pumpedWithAccentAndL(int k) {
  boot(0x00, 0x01, 200);
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=vu,c=ffffff,s=255,am=2,m=255,at=0,ae=flash,ac=ffffff");
  at(1500, "!**C:bpm=120,bpb=4,beat=0");
  if (k) for (uint32_t t = 2000; t < 8000; t += 37) at(t, (t / 37) & 1 ? "!P*L:v=255" : "!P*L:v=0");
  runTo(9000);
}
static void review_r2_an_l_during_an_accent_over_a_pumped_vu_is_not_shown() {
  uint32_t h0 = 0, h1 = 0; unsigned f0 = 0, f1 = 0;
  CHECK(framesOfChild(pumpedWithAccentAndL, 0, 1000, 9000, h0, f0) && framesOfChild(pumpedWithAccentAndL, 1, 1000, 9000, h1, f1),
        "a firmware did not finish");
  printf("  without L: %u frames, hash %08x; with L every 37 ms: %u frames, hash %08x\n", f0, h0, f1, h1);
  CHECK(f0 > 100 && h0 == h1 && f0 == f1, "an L during the accent over a pumped vu changed what the panel shows");
}

static const Scenario kScenarios[] = {
  {"review_r2_a_scored_vu_accent_keeps_the_rows_at_the_owners_20", review_r2_a_scored_vu_accent_keeps_the_rows_at_the_owners_20},
  {"review_r2_a_scored_vu_accent_keeps_the_rows_at_200", review_r2_a_scored_vu_accent_keeps_the_rows_at_200},
  {"review_r2_control_a_scored_meter_accent_keeps_the_rows", review_r2_control_a_scored_meter_accent_keeps_the_rows},
  {"review_r2_a_pause_keeps_the_rows", review_r2_a_pause_keeps_the_rows},
  {"review_r2_a_resume_keeps_the_rows", review_r2_a_resume_keeps_the_rows},
  {"review_r2_pauses_and_resumes_keep_the_rows", review_r2_pauses_and_resumes_keep_the_rows},
  {"review_r2_a_rise_shown_dimmer_still_starts_the_window", review_r2_a_rise_shown_dimmer_still_starts_the_window},
  {"review_r2_an_l_just_after_a_pumped_vu_feeds_the_meter", review_r2_an_l_just_after_a_pumped_vu_feeds_the_meter},
  {"review_r2_control_an_l_just_after_a_pumped_solid_feeds_the_meter", review_r2_control_an_l_just_after_a_pumped_solid_feeds_the_meter},
  {"review_r2_an_l_during_an_accent_over_a_pumped_vu_is_not_shown", review_r2_an_l_during_an_accent_over_a_pumped_vu_is_not_shown},
};
