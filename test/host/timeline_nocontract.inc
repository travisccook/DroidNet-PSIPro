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
// Review finding 3. The TWI interrupt can land inside a show(), and its mode command runs the mode
// there and then, in the middle of the bytecode step that is drawing. That step used to store its
// place in the OLD program over the new one's, and the old mode went on under the new mode's
// number. What the panel shows then is compared with upstream, pair by pair, in diff_upstream.sh;
// here the VM itself is watched: while the new mode is the board's mode, the VM's place is only
// ever a place the new mode's program has when the same command arrives between two passes.
// A child plays `was` from 1000 ms, then takes "0T<now>" from I2C: inside the first show() at or
// after `from` (inShow), or between two passes at ms `from`. It reports when the command came and
// every place the VM was at after a pass from then on, while `now` was the mode.
static void modeOverMode(int was, int now, uint32_t from, bool inShow, int fd) {
  char a[16], b[16]; snprintf(a, sizeof a, "0T%d", was); snprintf(b, sizeof b, "0T%d", now);
  boot();
  at(1000, a);
  if (inShow) atI2cInShow(from, b); else atI2c(from, b);
  std::vector<const uint8_t*> seen;
  uint32_t t0 = inShow ? 0 : from;
  for (uint32_t t = 1; t <= from + 6000; t++) {
    runTo(t);
    if (inShow && !t0) t0 = g_isrTookAt;
    if (!t0 || t < t0 || lastPSIeventCode != now) continue;
    bool have = false;
    for (const uint8_t* p : seen) if (p == g_vmPC) have = true;
    if (!have) seen.push_back(g_vmPC);
  }
  std::string out = std::to_string(t0);
  for (const uint8_t* p : seen) { char h[24]; snprintf(h, sizeof h, " %lx", (unsigned long)(uintptr_t)p); out += h; }
  out += "\n";
  (void)!write(fd, out.c_str(), out.size());
}
static std::string modeOverModeChild(int was, int now, uint32_t from, bool inShow) {
  int p[2]; std::string out;
  if (pipe(p) != 0) return out;
  fflush(stdout);
  const pid_t pid = fork();     // (a fork, so both children see the programs at the same addresses)
  if (pid == 0) { close(p[0]); modeOverMode(was, now, from, inShow, p[1]); fflush(stdout); _exit(0); }
  close(p[1]);
  char buf[512]; ssize_t n;
  while ((n = read(p[0], buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
  close(p[0]); int st; waitpid(pid, &st, 0);
  return out;
}
static void an_i2c_mode_command_inside_a_show_leaves_the_vm_to_the_new_mode() {
  // every mode a command can name: the bytecode modes, and the ones still drawn by upstream's code
  static const int kModes[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 92};
  unsigned pairs = 0, held = 0, places = 0;
  for (const uint32_t from : {1235u, 1700u, 2300u, 3100u}) for (int was : kModes) for (int now : kModes) {
    const std::string in = modeOverModeChild(was, now, from, true);
    unsigned long t0 = 0; int used = 0;
    CHECK(sscanf(in.c_str(), "%lu%n", &t0, &used) == 1 && t0 >= from && t0 < from + 1200, "0T%d: a show from %u ms on took the line (%s)", was, from, in.c_str());
    const std::string between = modeOverModeChild(was, now, (uint32_t)t0, false);
    const size_t sp = between.find(' ');
    const std::string allowed = sp == std::string::npos ? std::string("\n") : between.substr(sp);   // " a b c\n"
    pairs++; bool ok = true;
    const char* q = in.c_str() + used; unsigned long pc; int k;
    while (sscanf(q, " %lx%n", &pc, &k) == 1) {
      q += k; places++;
      char h[24]; snprintf(h, sizeof h, " %lx", pc);
      const size_t at = allowed.find(h);
      if (at == std::string::npos || (allowed[at + strlen(h)] != ' ' && allowed[at + strlen(h)] != '\n')) {
        if (ok) CHECK(false, "0T%d, then I2C 0T%d inside the show at %lu ms: the VM is at a place (%lx) the program of mode %d never has "
                             "when the command comes between two passes", was, now, t0, pc, now);
        ok = false;
      }
    }
    if (ok) held++;
  }
  CHECK(pairs == 4 * 441 && held == pairs && places > pairs, "%u of %u mode pairs leave the VM to the new mode (%u places checked)", held, pairs, places);
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
  {"an_i2c_mode_command_inside_a_show_leaves_the_vm_to_the_new_mode", an_i2c_mode_command_inside_a_show_leaves_the_vm_to_the_new_mode},
  {"natives_run_after_contract_lines", natives_run_after_contract_lines},
  {"the_announce_has_no_contract_tag", the_announce_has_no_contract_tag},
};
