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

static const Scenario kScenarios[] = {
  {"no_wd_means_no_watchdog", no_wd_means_no_watchdog},
};
