// test/host/timeline_scenarios.inc — contract scenarios for test_timeline.cpp. Each task that changes
// what the panel does adds its scenarios here and to kScenarios. Front PSI, owner brightness 20
// (EEPROM 00,01,14) unless a scenario says otherwise.

// A show armed without wd= has no watchdog: it holds with no host (a show stored in a WCB macro).
static void no_wd_means_no_watchdog() {
  boot();
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00");
  runTo(40000);
  CHECK(shown(39900).allAre(0x00ff00), "a show armed without wd= must hold with no host");
}

// ---- S1, S2: the pump cannot strobe -----------------------------------------------------------
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

static const Scenario kScenarios[] = {
  {"no_wd_means_no_watchdog", no_wd_means_no_watchdog},
  {"out_of_range_bpm_is_ignored", out_of_range_bpm_is_ignored},
  {"pump_recurs_no_faster_than_340ms", pump_recurs_no_faster_than_340ms},
};
