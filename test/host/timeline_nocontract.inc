// test/host/timeline_nocontract.inc — scenarios for the build WITHOUT the contract layer
// (-DPSI_ENABLE_I2C -DPSI_NO_CONTRACT: the PSIPro-i2c environment), run through test_timeline.cpp
// with -DTIMELINE_SCENARIOS. That build is upstream's firmware with this fork's fixes: a '!' line
// is nothing to it, native commands run from the serial port and from I2C, and the WDP announce
// carries no contract tag. (Its native modes are the goldens and the upstream differential, which
// run.sh runs on this build too.)
#define GREEN 0x00ff00
#define RED 0xff0000

// A show, a look, X and a query: nothing. The swipe runs on, lit, never green, and no reply leaves.
static void a_contract_line_is_nothing_to_this_build() {
  boot();
  at(1000, "!**M:v=show"); at(1100, "!P*A:i=solid,c=00ff00"); at(1500, "!P*P:c=00ff00,d=300"); at(2000, "!P*X");
  at(2100, "!PFQ:r=0"); at(2500, "!P*A:i=native:18");
  runTo(4000);
  unsigned frames = 0;
  for (const Frame& f : between(1000, 4000)) {
    frames++;
    if (f.allAre(GREEN) || f.lit < 24 || f.scale != 20) {   // (the front swipe is red and blue)
      CHECK(false, "t=%u: %u lit at scale %u (px0 %02x%02x%02x); the swipe runs on at the owner's brightness", f.t, f.lit, f.scale,
            f.px[0].r, f.px[0].g, f.px[0].b);
      break;
    }
  }
  CHECK(frames > 100, "the swipe draws (%u frames)", frames);
  CHECK(strstr(Serial1.txLog, "q:ver") == nullptr, "a Q is not answered (tx: %s)", Serial1.txLog);
}
// The native commands, from both intakes.
static void a_native_command_over_serial_runs() {
  boot();
  at(1000, "0T17");
  runTo(1500);
  CHECK(shown(1400).allAre(RED), "0T17 from the serial port: the panel is red");
}
static void a_native_command_over_i2c_runs() {
  boot();
  atI2c(1000, "0T17");
  runTo(1500);
  CHECK(shown(1400).allAre(RED), "0T17 from an I2C master: the panel is red");
}
// A native command after a '!' line that names the same mode still runs, and the owner's 3P works.
static void natives_run_after_contract_lines() {
  boot();
  at(1000, "!**M:v=show"); at(1100, "!P*X"); at(1200, "0T15"); at(2500, "3P80");
  runTo(4000);
  unsigned most = 0;
  for (const Frame& f : between(1500, 2400)) if (f.lit > most) most = f.lit;
  CHECK(most > 0 && most <= 6, "Knight Rider runs (at most %u lit)", most);
  bool at80 = false;
  for (const Frame& f : between(2600, 4000)) if (f.lit && f.scale == 80) at80 = true;
  CHECK(at80, "3P80 sets the brightness");
}
// The announce: type and fw, no caps, so nothing takes this board for one that can run a show.
static void the_announce_has_no_contract_tag() {
  boot();
  runTo(4000);
  CHECK(strstr(Serial1.txLog, "@WDP1 {\"type\":\"PSI Front\",\"fw\":\"abcdef0\"}\n") != nullptr, "the line (tx: %s)", Serial1.txLog);
  CHECK(strstr(Serial1.txLog, "caps") == nullptr && strstr(Serial1.txLog, "contract") == nullptr, "no caps (tx: %s)", Serial1.txLog);
}
static const Scenario kScenarios[] = {
  {"a_contract_line_is_nothing_to_this_build", a_contract_line_is_nothing_to_this_build},
  {"a_native_command_over_serial_runs", a_native_command_over_serial_runs},
  {"a_native_command_over_i2c_runs", a_native_command_over_i2c_runs},
  {"natives_run_after_contract_lines", natives_run_after_contract_lines},
  {"the_announce_has_no_contract_tag", the_announce_has_no_contract_tag},
};
