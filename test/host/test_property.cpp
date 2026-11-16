// test/host/test_property.cpp — contract v1.3 S9 as a PROPERTY of the firmware (src/main.cpp,
// verbatim, on the host mocks of the hardware).
//
//   Whatever lines arrive, in whatever order, the panel
//     (a) goes from dark to lit across its whole field at most three times in any second, and no
//         two such onsets are less than 340 ms apart;
//     (b) is never brighter than its owner's ceiling while the contract holds it;
//     (c) is back on the board's own native look, and stays there, after M:v=idle, a watchdog
//         expiry or a failed V (in a stream with no native commands that look is the swipe at the owner's
//         brightness), and the watchdog fires when the test's own count says it must;
//     (d) is dark at once after X, and stays dark until a line that may relight it;
//     (e) L6: runs the mode a native command started, and the contract does not take the panel
//         back by itself;
//     (f) S7: never runs, for a show, a native mode that is off the allow-list;
//     (g) S10: never writes into a native look: no line changes the levels the native VU mode
//         shows while a native look (the owner's, or a native:<n> of the show's) has the panel;
//     (h) S9 in LUMINANCE (Rec.709), the same limit as (a): a change of colour moves the
//         luminance while the brightest channel stays where it was (white to blue and back);
//     (i) S11: changes its look or colour across the whole field at most once in 170 ms, whatever
//         path asks (a live A, a section reached by time or by a seek, an accent, a hand-back).
//
// What is measured is the frame on its way to the LEDs: the full-field level of every frame
// FastLED.show() sends (the mean over the LEDs of each LED's brightest channel, times the master
// scale), which is what the panel shows until the next frame. (a) is counted seven times over:
// with three wide pairs of thresholds for "dark" and "lit" (10 % and 50 % of the ceiling, 19 % and
// 75 %, 4 % and 31 %) and four bands only an eighth of the ceiling wide (3 to 15 %, 40 to 52 %,
// 45 to 60 %, 80 to 95 %), because S9's step is a tenth of the ceiling. The limit must not depend
// on where an observer draws those lines.
//
// TWO PHOTOMETERS, mirrored from the logics fork. The first reads each LED's brightest channel, so
// a red or blue field counts as much as a white one. The second reads colour: the field's
// luminance for (h), as the gate defines it (per LED (54 r + 183 g + 19 b) >> 8, then the mean),
// with the three wide pairs; and the field's mean chromaticity for (i), for five observers, each
// with its own idea of when the field is lit enough to have a colour.
//
// Two kinds of stream, mirrored from the logics fork's test_contract_property.cpp:
//   * HOSTILE PROFILES: one per way a host was found able to flash a display, each a short script
//     repeated with random jitter and, in half the rounds, harmless lines in between;
//   * RANDOM streams: every verb, wildcards, bursts, corrupted parameters, re-anchors, seeks,
//     scored and live lines, native codes on and off the allow-list, at random times; in half of
//     them an owner also sends native commands (a "button").
//
// THE BUTTON. A native command may light a dark panel whenever its owner likes; that is not the
// contract's onset, so the observers start counting afresh at each one. What the contract does
// AFTER it is counted as ever. The modes a button picks here do not strobe the field themselves.
//
// The firmware keeps static state, so every stream runs in its own forked child, which reports its
// numbers through a pipe. What the test knows about the layer's inside is little: g_owned (does
// the contract hold the panel, for (b), (c) and (e)), g_u.inShow (did show mode end without a
// line saying so), and who draws a frame, the contract or a native look (S10, below). Everything
// else it asserts is on the frames.
//
// For hunting: PROPERTY_SEED=<n> and PROPERTY_STREAMS=<n> run other streams; PROPERTY_JOB=<n>
// runs one job in this process and prints the lines and level changes that led to its first break
// (with PROPERTY_LOG=1, those of the whole stream). PROPERTY_EACH=1 names every failing job.
#include "native_mocks/Arduino.h"
// (native_mocks/FastLED.h is not included here: main.cpp names the S9 frame gate and then includes
// <FastLED.h> itself. ContractPSI.h refuses to build if the mock was compiled without the gate.)
#include "native_mocks/EEPROM.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <initializer_list>
#include <string>
#include <deque>
#include <vector>

#include "../../src/main.cpp"  // the firmware, verbatim

// ---- deterministic randomness ----
static uint32_t gRng = 0x9E3779B9u;
static uint32_t rnd() { gRng ^= gRng << 13; gRng ^= gRng >> 17; gRng ^= gRng << 5; return gRng; }
static uint32_t rnd(uint32_t n) { return n ? rnd() % n : 0; }
static const char* pick(std::initializer_list<const char*> l) { return *(l.begin() + rnd((uint32_t)l.size())); }

// ---- (a): one observer, with its own idea of dark and lit ----
// S10: a native look runs as stock, so what its OWN frames do is the stock firmware's look and not
// the contract's doing. An onset seen on a native frame is therefore counted only when the
// contract caused it: after one of its edges (it started or restarted the look, handed the panel
// back to it, or raised the brightness it gives it; `edge`), the NEXT relight on a native frame is
// the contract's, whenever it shows and whether or not the panel was lit at the edge. (A native
// look that clears and redraws when it starts has been made to do that by the contract. A look
// that was dark when the panel came back to it relights at its own time, but the light before it
// was the contract's, and the two are one flash after another to anyone watching.) Every other
// onset on a native frame is the look's own and is set aside (counted in `own`, never in the
// limit). Frames the contract draws are counted as ever, and nothing else is excluded.
struct Onsets {
  int darkPct = 10, litPct = 50; bool lum = false;   // lum: this one reads luminance
  bool lit = false; uint32_t at[128]; long n = 0, total = 0, own = 0;
  bool armed = false;            // a contract edge came: the next native relight is the contract's
  int worst = 0; uint32_t minGap = 0xFFFFFFFFu;
  void see(int level, int ceil, uint32_t now, bool nativeFrame, bool edge) {
    if (!nativeFrame) armed = false;                 // the contract draws: its frames answer for themselves
    else if (edge) armed = true;
    if (!lit && level * 100 >= ceil * litPct) {
      lit = true;
      if (nativeFrame && !armed) { own++; return; }
      armed = false;
      if (n > 0 && now - at[(n - 1) & 127] < minGap) minGap = now - at[(n - 1) & 127];
      at[n++ & 127] = now; total++;
      int w = 0; for (long i = n; i-- > 0 && n - i <= 128 && now - at[i & 127] < 1000; ) w++;
      if (w > worst) worst = w;
    } else if (lit && level * 100 <= ceil * darkPct) lit = false;
  }
};

// ---- what is measured ----
static uint32_t gNow = 0;
static int gCeil = 20;                    // the owner's brightness in this stream
static bool gButtons = false;             // this stream has an owner at the controller
static Onsets gObs[10];                   // (a): 0-2 wide, 3-6 narrow. (h): 7-9, on luminance
// ---- (i): one observer of the field's colour ----
// Below its dark level the field has no colour for it; the colour it sees when the field is lit
// is compared with the one it saw last, and a different one is a change of look. On a native
// look's frames (S10) only the change a contract edge makes is counted: the first lit frame after
// the edge. What the look then does with its colours is its own.
struct Colour {
  int darkPct = 10, litPct = 50; bool lit = false, known = false, any = false, armed = false;
  int chroma[3] = { 0, 0, 0 }; uint32_t at = 0;
  int lastCode = -2;             // the native mode whose frame it last saw lit; -1: a frame the contract drew
  // returns the gap to the last counted change when this frame is one, else 0xFFFFFFFF
  uint32_t see(int level, int ceil, long sr, long sg, long sb, uint32_t now, bool nativeFrame, bool edge, bool sameLook, int code) {
    if (!nativeFrame) armed = false; else if (edge) armed = true;
    if (!lit && level * 100 >= ceil * litPct) lit = true;
    else if (lit && level * 100 <= ceil * darkPct) lit = false;
    const long top = sr > sg ? (sr > sb ? sr : sb) : (sg > sb ? sg : sb);
    if (!lit || top == 0) return 0xFFFFFFFFu;      // too dim to have a colour, for this one
    const int c[3] = { (int)(sr * 255 / top), (int)(sg * 255 / top), (int)(sb * 255 / top) };
    int far = 0;
    for (int i = 0; i < 3; i++) { const int d = c[i] > chroma[i] ? c[i] - chroma[i] : chroma[i] - c[i]; if (d > far) far = d; }
    uint32_t gap = 0xFFFFFFFFu;
    // Back on the native look it last saw lit (only black in between: X, then the look again):
    // the same look, moved on by itself. S11 calls that no change, and so does the look gate.
    const bool counts = (!nativeFrame || armed) && !sameLook && !(nativeFrame && code == lastCode);
    lastCode = nativeFrame ? code : -1;
    armed = false;                                  // the first lit frame after the edge was this one
    if (known && far > 96 && counts) { if (any) gap = now - at; at = now; any = true; }
    for (int i = 0; i < 3; i++) chroma[i] = c[i];
    known = true;
    return gap;
  }
};
static Colour gCol[5];
static uint32_t gColourGap = 0xFFFFFFFFu; static long gColourChanges = 0; static bool gWasMeter = false;
static int gLevel = 0, gScale = 0, gLit = 0; static long gRgbKey = -1;
static long gOverCeiling = 0, gNotNative = 0, gNotDark = 0, gNotButton = 0, gWdLate = 0, gOffList = 0, gReached = 0;
static long gDarkChecked = 0, gNativeChecked = 0, gButtonChecked = 0, gContractFrames = 0, gLines = 0, gPresses = 0;
static bool gExpectNative = false, gExpectDark = false; static uint32_t gNativeFrom = 0, gDarkFrom = 0;
static int gExpectButton = -1; static uint32_t gButtonFrom = 0; static int gButtonPasses = 0;
static bool gPressed = false;            // a native command is in the RX buffer, not read yet
static int gPassesSincePress = 1000;     // loop passes since the last native command went out
static uint32_t gFrameMs = 0;            // when the last frame was latched
static bool gTrace = false, gBroken = false;
// S10 bookkeeping: has the contract done something to the panel since the last frame?
static bool gEdge = false;                // a line (re)started a native look or wrote into it, or show mode ended by itself
static long gSig = -1;                    // who drew the last frame, and what: a change is an edge
static int gHanded = 0;                   // the brightness a show last gave a native:<n> look
static long gOwnOnsets = 0;
static std::vector<std::string> gLog;
static void logf(const char* what) { if (gTrace) { char b[224]; snprintf(b, sizeof b, "  %7u  %s", gNow, what); gLog.push_back(b); } }
static void broke(const char* why) {
  if (!gTrace || gBroken) return;
  gBroken = true;
  printf("TRACE: %s at %u ms. What led up to it:\n", why, gNow);
  for (size_t i = gLog.size() > 60 ? gLog.size() - 60 : 0; i < gLog.size(); i++) puts(gLog[i].c_str());
}

// Is the panel a native look's (the board's own, or a native:<n> a show selected)?
static bool nativeHasPanel() { return !g_owned || (g_u.base == CB_NATIVE && !g_u.pulseActive); }
static void hook(const CRGB* l, int n, uint8_t scale) {
  long sum = 0, sr = 0, sg = 0, sb = 0, sy = 0; int lit = 0;
  for (int i = 0; i < n; i++) {
    int m = l[i].r; if (l[i].g > m) m = l[i].g; if (l[i].b > m) m = l[i].b;
    sum += m; if (m) lit++;
    sr += l[i].r; sg += l[i].g; sb += l[i].b; sy += (54 * l[i].r + 183 * l[i].g + 19 * l[i].b) >> 8;
  }
  const int level = (int)((sum * scale) / ((long)n * 255));
  const int luminance = (int)((sy * scale) / ((long)n * 255));
  const long rgbKey = (sr / 256) * 1000000 + (sg / 256) * 1000 + sb / 256;
  if (gTrace && (level != gLevel || scale != gScale || rgbKey != gRgbKey)) {
    char b[200]; snprintf(b, sizeof b, "frame level %3d lum %3d scale %3u lit %2d rgb %02lx%02lx%02lx%s   [gate: %s ref %u, lum %s ref %u, onset %u, look %u, ceil %u]",
                          level, luminance, scale, lit, sr / n, sg / n, sb / n, g_owned ? " (contract)" : "",
                          g_u.fieldLit ? "lit" : "not lit", g_u.fieldRef, g_u.fieldLitY ? "lit" : "not lit", g_u.fieldRefY,
                          g_u.fieldOnsetMs, g_u.lookMs, g_u.ceil);
    logf(b);
  }
  gRgbKey = rgbKey;
  gLevel = level; gScale = scale; gLit = lit; gFrameMs = gNow;
  // A native frame: the board's own look, or a native:<n> a show selected (not an accent overlay
  // still up from the section before it: that is the contract's own drawing). Its edges: the
  // drawer or the look changed since the last frame, a line restarted the look, or a show raised
  // the brightness it gives the look.
  const bool nativeFrame = nativeHasPanel();
  // (Not the section cursor: a section that applies the look the unit already has does not restart it, S11.)
  const long sig = ((long)nativeFrame << 25) | ((long)g_owned << 24) | ((long)g_u.base << 16) | ((long)(g_u.nativeCode & 0xFF) << 8);
  const int handed = (g_owned && g_u.base == CB_NATIVE) ? tempGlobalBrightnessValue : 0;
  bool edgeLook = sig != gSig;                      // the drawer or the look changed: an edge for (a) and for (i)
  bool edge = edgeLook || gEdge || handed > gHanded;   // ...a line that may restart a native look, or more
                                                    // brightness for one: edges for (a) (the next relight)
  if (gPassesSincePress <= 2) edge = edgeLook = false;   // a native command: the owner's light, not an edge
  gEdge = false; gSig = sig; gHanded = handed;
  // S10: Leia (6) and Knight Rider (15) move a lit area under a quarter of the field. That is not
  // a field onset, whoever started the look and whenever: nothing on their frames is counted.
  const int nativeCode = g_owned ? g_u.nativeCode : lastPSIeventCode;
  const bool smallArea = nativeFrame && (nativeCode == 6 || nativeCode == 15);
  if (smallArea) edge = edgeLook = false;
  for (Onsets& o : gObs) {
    const int before = o.worst; const uint32_t gap = o.minGap; const long own = o.own;
    if (smallArea) o.armed = false;
    o.see(o.lum ? luminance : level, gCeil, gNow, nativeFrame, edge);
    if (gTrace && o.own > own && &o == &gObs[0]) logf("   (that light is the native look's own: set aside)");
    if (o.worst > 3 && before <= 3) broke("a fourth onset in one second");
    if (o.minGap < 340 && gap >= 340) broke(o.lum ? "two luminance onsets under 340 ms apart" : "two onsets under 340 ms apart");
  }
  // The contract's meter look is one look in three colours (green rows, then yellow, then red):
  // its mean colour moves with its bars. From one meter frame to the next that is the look's own
  // texture, not a change of look; into the meter and out of it is counted as ever.
  const bool meter = !nativeFrame && g_u.base == CB_LOOK && !g_u.pulseActive && g_u.effect == CE_METER;
  const bool sameLook = meter && gWasMeter;
  gWasMeter = meter;
  for (Colour& k : gCol) {
    if (smallArea) k.armed = false;
    const uint32_t gap = k.see(level, gCeil, sr, sg, sb, gNow, nativeFrame, edgeLook, sameLook, nativeCode);
    if (gap == 0xFFFFFFFFu) continue;
    if (&k == &gCol[0]) gColourChanges++;
    if (gTrace) { char b[96]; snprintf(b, sizeof b, "   (a change of colour for observer %d, %u ms after its last)", (int)(&k - gCol), gap); logf(b); }
    if (gap < gColourGap) { gColourGap = gap; if (gap < STROBE_MIN_STATE_MS) broke("two changes of colour under 170 ms apart"); }
  }
  if (g_owned) { gContractFrames++; if (scale > gCeil) { gOverCeiling++; broke("a contract frame above the ceiling"); } }
  // (f): S7. The show never runs a native mode that is off the allow-list.
  if (g_owned && g_u.base == CB_NATIVE && !nativeAllowedPsi(g_u.nativeCode)) { gOffList++; broke("a native mode off the allow-list"); }
}

static void expectNative() { if (!gExpectNative) gNativeFrom = gNow; gExpectNative = true; gExpectDark = false; }
static void expectDark()   { gDarkFrom = gNow; gExpectDark = true; gExpectNative = false; gExpectButton = -1; }
static void expectUnknown(){ gExpectNative = gExpectDark = false; gExpectButton = -1; }

// ---- sending, with the bookkeeping the properties need ----
// Lines wait in a queue until the end of the pass. Each is then fed and read by the firmware on
// its own (a burst is still one pass: nothing is drawn between its lines), so that the bookkeeping
// for a line runs after the firmware has taken that line and before it takes the next.
static std::deque<std::string> gQueue;
// L2, by the test's own count: armed by M:v=show,wd=<s>; fed by any well-formed line addressed to
// this board. gWdSure: nothing else (a V) may have ended show mode since it armed.
static bool gWdArmed = false, gWdSure = false; static uint32_t gWdMs = 0, gLastAddressed = 0;
static void tx(const std::string& line) { gQueue.push_back(line); }
static void deliver(const std::string& line) {
  ParsedContract p;
  const char* l = line.c_str();
  if (Serial1.rxHead == Serial1.rxTail) Serial1.rxHead = Serial1.rxTail = 0;
  // (g): S10. The levels a meter shows are the native VU mode's own while a native look has the
  // panel: no line may write them (verb L feeds the contract's meter look, and only that).
  int levelsBefore = 0; for (int c = 0; c < COLUMNS; c++) levelsBefore = levelsBefore * 7 + level[c];
  const bool nativeHad = nativeHasPanel();
  Serial1.feed("!"); Serial1.feed(l); Serial1.feed("\r");
  serialEventRun();
  int levelsAfter = 0; for (int c = 0; c < COLUMNS; c++) levelsAfter = levelsAfter * 7 + level[c];
  if (nativeHad && nativeHasPanel() && levelsAfter != levelsBefore) { gReached++; gEdge = true; broke("a line wrote a native meter's levels"); }
  gLines++;
  logf(("line  " + line).c_str());
  if (!contractParse(l, p) || (p.cls != 'P' && p.cls != '*') || (p.unit != 'F' && p.unit != '*')) return;
  gLastAddressed = gNow;
  // A line that (re)starts a native:<n> look is an edge even when nothing else about the panel's
  // drawer changes (the same code again). No other line is one by itself: what it does to a
  // native look shows as a change of drawer, of look or of the brightness handed to it.
  // (A line naming a code that is off the allow-list is dropped whole: no edge.)
  if (p.verb == CV_ANIMATE && !p.params.hasAt && p.params.hasEffect && p.params.effect == CE_NATIVE &&
      nativeAllowedPsi(p.params.nativeCode)) gEdge = true;
  if (p.verb == CV_CLOCK) { expectUnknown(); return; }                 // a score may start to play
  if (p.verb == CV_VERIFY) {
    gWdSure = false;
    // Section 7: a V that failed is an M:v=idle the unit ran on itself. (A good one changes nothing.)
    if (p.params.hasN && p.params.hasK && g_u.loadOk == 2) { expectNative(); gWdArmed = false; }
    return;
  }
  if (p.verb == CV_MODE && p.params.mode == 'i')      { expectNative(); gWdArmed = false; }
  else if (p.verb == CV_MODE && p.params.mode == 's') { expectNative();  // armed: the native look until the show begins
    gWdArmed = gWdSure = p.params.hasWd && p.params.wd != 0; gWdMs = (uint32_t)p.params.wd * 1000u; }
  else if (p.verb == CV_STOP)                         expectDark();
  else if (p.verb == CV_ANIMATE || p.verb == CV_PULSE) expectUnknown(); // may take, or relight, the panel
  // Q, L, B, V and an M that is neither show nor idle: nothing the panel may light up for
}
// A button: the owner sends native mode `code` to this PSI.
static void press(int code) {
  char b[16]; snprintf(b, sizeof b, "0T%d", code);
  Serial1.feed(b); Serial1.feed("\r");
  logf((std::string("BUTTON ") + b).c_str());
  gPressed = true; gPassesSincePress = 0;
  gWdArmed = false;                                  // a native command ends the show, its watchdog with it
  gExpectDark = false; gExpectNative = false;
  gExpectButton = code; gButtonFrom = gNow; gButtonPasses = 0;
  gPresses++;
}
static void step(uint32_t ms = 1) {
  const bool was = g_u.inShow;
  gNow += ms; g_mock_millis = gNow;
  if (Serial1.rxHead == Serial1.rxTail) Serial1.rxHead = Serial1.rxTail = 0;
  loop();
  // Show mode ended inside the pass, so with no line saying so (lines are read after it): the
  // watchdog. The panel goes back to native. (Not a native command: that ends the show when it is
  // read, or, on the I2C build, at the start of the pass after.)
  gPassesSincePress++;
  // (An edge, unless the native look's first frame came in this same pass: then it has been seen as one.)
  if (was && !g_u.inShow && gPassesSincePress > 2) { expectNative(); if (gFrameMs != gNow) gEdge = true; }
  // The firmware reads the native command now, and what it draws from here on is the owner's
  // light, not the contract's onset: the observers start counting afresh.
  if (gPressed) {
    gPressed = false; gEdge = false;
    for (Onsets& o : gObs) { o.n = 0; o.armed = false; }
    for (Colour& k : gCol) k.known = k.any = k.armed = false;   // ...nor the contract's change of look
  }
  serialEventRun();
  while (!gQueue.empty()) { deliver(gQueue.front()); gQueue.pop_front(); }
  // L2: the watchdog must have fired by now (it is looked at once a frame; a slow pass is 40 ms).
  if (gWdArmed && gNow - gLastAddressed >= gWdMs + 80) {
    if (gWdSure && g_u.inShow) { gWdLate++; broke("the watchdog is late"); }
    gWdArmed = false;
  }
  // (c): the give-back may wait for the onset window (340 ms) and a frame
  if (gExpectNative && gNow - gNativeFrom > 400) {
    gNativeChecked++;
    // The swipe: the default pattern, drawing, at the owner's brightness. (Not "all lit": while a
    // colour change crosses the panel more than half of its columns are dark.)
    const bool swipe = lastPSIeventCode == defaultPattern && gNow - gFrameMs < 500 && gScale == gCeil;
    if (g_owned || (!gButtons && !swipe)) { gNotNative++; broke("not on the native look after idle"); }
  }
  // (d): one frame for the board to draw it, then dark
  if (gExpectDark && gNow - gDarkFrom > 30) { gDarkChecked++; if (gLevel * 10 > gCeil) { gNotDark++; broke("lit after X"); } }
  // (e): the native command runs, and the contract stays off the panel. Counted from the second
  // loop pass after the command went out: the firmware reads it at the end of the first, and the
  // I2C build ends the show at the start of the next (the yield is deferred out of the interrupt).
  if (gExpectButton >= 0 && ++gButtonPasses > 2 && gNow - gButtonFrom > 30) {
    gButtonChecked++;
    if (g_owned || (gNow - gButtonFrom < 1000 && lastPSIeventCode != gExpectButton)) { gNotButton++; broke("not on the mode a native command started"); }
  }
}
static void begin(int ownerBright, bool alwaysOn) {
  gCeil = ownerBright;
  static const int kBand[10][2] = { { 10, 50 }, { 19, 75 }, { 4, 31 }, { 3, 15 }, { 40, 52 }, { 45, 60 }, { 80, 95 },
                                    { 10, 50 }, { 19, 75 }, { 4, 31 } };
  for (int i = 0; i < 10; i++) { gObs[i].darkPct = kBand[i][0]; gObs[i].litPct = kBand[i][1]; gObs[i].lum = i >= 7; }
  static const int kColour[5][2] = { { 10, 50 }, { 19, 75 }, { 4, 31 }, { 3, 15 }, { 45, 60 } };
  for (int i = 0; i < 5; i++) { gCol[i].darkPct = kColour[i][0]; gCol[i].litPct = kColour[i][1]; }
  g_avr_next = 1; g_mock_analog = 512; g_mock_pins[JUMP_FRONT_REAR] = 1;      // a front PSI
  EEPROM.mem[alwaysOnAddress] = alwaysOn ? 0x01 : 0x00; EEPROM.mem[externalPOTAddress] = 0x01;
  EEPROM.mem[internalBrightnessAddress] = (uint8_t)ownerBright;
  g_frame_hook = hook;
  setup();
  for (int i = 0; i < 400; i++) step();            // 400 ms of uptime before anything arrives
}

// ---- a random line: mostly plausible, sometimes corrupted ----
// (Every random value is drawn into a variable first: the order in which a compiler evaluates
// function arguments is its own, and the streams must be the same streams everywhere.)
static std::string randomLine() {
  char b[160];
  const char* cls = pick({ "P", "P", "P", "P", "*", "*", "L", "H" });
  const char* unit = pick({ "F", "F", "R", "*", "*", "T" });
  const char* fx = pick({ "solid", "solid", "off", "flash", "pulse", "rainbow", "scan", "sparkle", "meter", "comet", "chase",
                          "wipe", "gradient", "colorcycle", "twinkle", "native:0", "native:4", "native:15", "native:2",
                          "native:25", "bogus", "native:7", "native:9", "native:17", "native:21", "native:22" });
  const char* col = pick({ "ffffff", "ffffff", "ff0000", "000000", "0000ff", "101010", "zzzzzz" });
  const uint32_t kind = rnd(17);
  const uint32_t r1 = rnd(256), r2 = rnd(2) ? 255 : rnd(300), r3 = rnd(2) ? 255 : rnd(256), r4 = rnd(6), r5 = rnd(5), r6 = rnd(4);
  const uint32_t r7 = rnd(400), r8 = rnd(40), r9 = rnd(65536), r10 = rnd(2) ? rnd(256) : (rnd(2) ? 0u : 255u), r11 = rnd(600), r12 = rnd(8);
  switch (kind) {
    case 0: case 1: case 2:
      snprintf(b, sizeof b, "%s%sA:i=%s,c=%s,s=%u,b=%u,m=%u,am=%u", cls, unit, fx, col, r1, r2, r3, r4); break;
    case 3:  snprintf(b, sizeof b, "%s%sA:c=%s,b=%u", cls, unit, col, r3); break;                            // a patch
    case 4: {
      const char* ae = pick({ "flash", "solid", "off", "pulse" });
      const char* far = pick({ "2147483647", "-2147483648", "1000000000", "99999999999" });
      if (r12 == 0) snprintf(b, sizeof b, "%s%sA:i=%s,c=%s,at=%s,am=3,m=200,an=16,ap=15,ae=flash", cls, unit, fx, col, far);
      else snprintf(b, sizeof b, "%s%sA:i=%s,c=%s,at=%d,am=%u,m=%u,an=%u,ap=%u,ae=%s", cls, unit, fx, col, (int)r8 - 4, r5, r1, r4, r6, ae);
      break;
    }
    case 5: case 6: {
      const char* pfx = pick({ "solid", "flash", "off", "pulse", "wipe" });
      snprintf(b, sizeof b, "%s%sP:i=%s,c=%s,d=%u,b=%u", cls, unit, pfx, col, r11, r3); break;
    }
    case 7:  snprintf(b, sizeof b, "%s%sC:bpm=%u,ph=%u,bpb=%u,beat=%d", cls, unit, 20 + r2, r7, 1 + r5, (int)r8 - 4); break;
    case 8: {
      const char* ph = pick({ "0", "4294967295", "2147483648" });
      const char* far = pick({ "2147483647", "-2147483648", "1000000000", "-99999999999" });
      if (r12 == 0) snprintf(b, sizeof b, "%s%sC:ph=%s,beat=%s", cls, unit, ph, far);
      else snprintf(b, sizeof b, "%s%sC:ph=%u,beat=%d", cls, unit, r7, (int)r8);
      break;
    }
    case 9:  snprintf(b, sizeof b, "%s%sC:bpm=%s", cls, unit, pick({ "0", "300", "120", "abc", "", "-120", "65656" })); break;
    case 10: snprintf(b, sizeof b, "%s%sB:b=%u", cls, unit, r10); break;
    case 11: snprintf(b, sizeof b, "%s%sL:v=%u", cls, unit, r10); break;
    case 12: snprintf(b, sizeof b, "%s%sX", cls, unit); break;
    case 13: {
      const char* mode = pick({ "show", "show", "idle", "stop" });
      const char* wd = pick({ "", "", ",wd=1", ",wd=2" });
      snprintf(b, sizeof b, "%s%sM:v=%s%s", cls, unit, mode, wd); break;
    }
    case 14: snprintf(b, sizeof b, "%s%sV:n=%u,k=%04x", cls, unit, r6, r9); break;
    case 15: snprintf(b, sizeof b, "%s%sQ:r=%s", cls, unit, pick({ "0", "2", "", "zz", "100" })); break;
    default: {                                     // garbage: random printable bytes after a plausible head
      int n = snprintf(b, sizeof b, "%s%s%c:", cls, unit, "APCBLXMQVz"[r4 + r6]);
      for (int i = 0, len = (int)r8; i < len && n < 120; i++) b[n++] = (char)(33 + rnd(94));
      b[n] = '\0';
    }
  }
  return b;
}

// ---- the hostile profiles: the ways a host was found able to flash a display ----
struct Profile { const char* name; void (*setup)(); void (*at)(uint32_t t); uint32_t period; };
static void pShow() { tx("**M:v=show"); }
// The owner keeps native mode `code` running (the board's alwaysOn setting, then the command).
static void ownerRuns(int code) { alwaysOn = true; gButtons = true; press(code); }
static const Profile kProfiles[] = {
  { "X between live A lines", pShow,
    [](uint32_t t) { if (t == 0) tx("PFA:i=solid,c=ffffff"); if (t == 50) tx("PFX"); if (t == 100) tx("PFA:i=solid,c=ffffff"); }, 170 },
  { "B 0/255 alternation", []() { pShow(); tx("PFA:i=solid,c=ffffff"); },
    [](uint32_t t) { if (t == 0) tx("PFB:b=0"); if (t == 50) tx("PFB:b=255"); }, 100 },
  { "P:b= alternation", []() { pShow(); tx("PFA:i=solid,c=ffffff"); },
    [](uint32_t t) { if (t == 0) tx("PFP:i=solid,c=ffffff,b=0"); if (t == 50) tx("PFP:i=solid,c=ffffff,b=255"); }, 100 },
  { "meter following L 0/255", []() { pShow(); tx("PFA:i=meter,c=ffffff"); },
    [](uint32_t t) { if (t == 0) tx("P*L:v=0"); if (t == 50) tx("P*L:v=255"); }, 100 },
  { "C seeks across two scored sections", []() { pShow(); tx("PFA:i=solid,c=ffffff,at=0"); tx("PFA:i=off,at=100"); },
    [](uint32_t t) { if (t == 0) tx("P*C:bpm=120,beat=100"); if (t == 50) tx("P*C:bpm=120,beat=0"); }, 100 },
  { "C:bpm= re-seed restarting the pump", []() { pShow(); tx("PFA:i=solid,c=ffffff,am=2,m=255"); },
    [](uint32_t t) { if (t == 0) tx("P*C:bpm=300"); }, 150 },
  { "A cancelling an overlay, with P", []() { pShow(); tx("PFA:i=off"); },
    [](uint32_t t) { if (t == 0) tx("PFP:i=solid,c=ffffff"); if (t == 90) tx("PFA:i=off"); if (t == 260) tx("PFA:i=solid,c=ffffff");
                     if (t == 340) tx("PFP:i=off"); if (t == 430) tx("PFA:i=solid,c=ffffff"); if (t == 600) tx("PFA:i=off"); }, 680 },
  { "pump across section seams at 300 BPM",
    []() { pShow(); for (int b = 0; b < 32; b++) { char l[96]; snprintf(l, sizeof l, "PFA:i=solid,c=ffffff,at=%d,am=2,m=255,an=2,ap=%d", b, b % 2); tx(l); }
           tx("**C:bpm=300,ph=0,bpb=4,beat=0"); },
    [](uint32_t t) { if (t == 0) tx("**C:bpm=300,ph=0,bpb=4,beat=0"); }, 6200 },
  { "X and M:v=idle alternation (dark, then the native look)", []() { tx("PFA:i=solid,c=ffffff"); },
    [](uint32_t t) { if (t == 0) tx("PFX"); if (t == 50) tx("PFM:v=idle"); }, 100 },
  { "live A restarting a pulse", pShow,
    [](uint32_t t) { if (t == 0) tx("PFA:i=pulse,c=ffffff,s=255"); }, 170 },
  { "live A between white and native lights-out", pShow,
    [](uint32_t t) { if (t == 0) tx("PFA:i=solid,c=ffffff"); if (t == 50) tx("PFX"); if (t == 60) tx("PFA:i=native:0"); if (t == 120) tx("PFA:i=solid,c=ffffff"); }, 240 },
  { "a scored flash section delivered again and again", []() { pShow(); tx("PFA:i=flash,c=ffffff,s=255,at=0"); tx("P*C:bpm=120"); },
    [](uint32_t t) { if (t == 0) tx("PFA:i=flash,c=ffffff,s=255,at=0"); }, 200 },
  // PSI: a native mode that strobes by design, selected by the show. 2 is upstream's Flash (white
  // at 8.3 Hz), 4 its Short Circuit flicker.
  { "a strobing native mode selected by the show", pShow,
    [](uint32_t t) { if (t == 0) tx("PFA:i=native:2"); if (t == 3000) tx("PFA:i=native:4"); }, 6000 },
  // PSI, S10: the EDGES of a native look are the contract's. A native look started from dark again
  // and again; a show's brightness toggled under a native look; the beat pump under one.
  { "a native look started from dark, again and again", pShow,
    [](uint32_t t) { if (t == 0) tx("PFX"); if (t == 60) tx("PFA:i=native:17"); if (t == 180) tx("PFX"); if (t == 230) tx("PFA:i=native:22"); }, 300 },
  // Modes 7 and 9 clear the panel when they start and draw at once; 22 builds up from dark. Started
  // while the panel is lit, that relight is the contract's doing.
  { "a native look that clears as it starts, started while lit", pShow,
    [](uint32_t t) { if (t == 0) tx("PFA:i=off"); if (t == 400) tx("PFA:i=solid,c=ffffff"); if (t == 600) tx("PFA:i=native:7");
                     if (t == 800) tx("PFA:i=native:9"); if (t == 1000) tx("PFA:i=native:22"); }, 1400 },
  // L writes the levels a meter shows. The board's own VU mode (a native command), and a native:21
  // a show selected, are native looks: L must not reach into them.
  { "L 0/255 at a native VU meter a show selected", []() { pShow(); tx("PFA:i=native:92"); },
    [](uint32_t t) { if (t == 0) tx("P*L:v=0"); if (t == 50) tx("P*L:v=255"); }, 100 },
  // The meter redraws every 250 ms: L empties it, a pulse lights the panel, L fills the meter.
  { "L at the board's own VU meter, around a pulse", []() { ownerRuns(92); },
    [](uint32_t t) { if (t == 0) tx("P*L:v=0"); if (t == 300) tx("PFP:c=ffffff,d=60"); if (t == 310) tx("P*L:v=255"); }, 1010 },
  // PSI, L3 and S10: a pulse over a native look the owner started that goes dark and relights by
  // itself (7 and 9: once in 1,040 ms; 22: once in 1,612 ms). The pulse lights the panel; the
  // look's own relight after it must keep its distance from that.
  { "P over a native look that blinks by itself", []() { ownerRuns(7); },
    [](uint32_t t) { if (t == 0) tx("PFP:c=ffffff,d=60"); }, 350 },
  { "P over a native look that builds up from dark", []() { ownerRuns(22); },
    [](uint32_t t) { if (t == 0) tx("PFP:c=ffffff,d=60"); if (t == 400) tx("PFP:c=ffffff,d=200"); }, 790 },
  // An accent overlay that is still flashing when its section gives way to a native:<n> one: the
  // overlay is the contract's own drawing for as long as it is up, whatever the base look is.
  { "an accent still flashing when a native section starts",
    []() { pShow(); for (int b = 0; b < 24; b += 6) { char l[96];     // one beat dark with the accent, five of native:0
             snprintf(l, sizeof l, "PFA:i=off,s=255,at=%d,am=2,ae=flash,ac=ffffff,ad=2500", b); tx(l);
             snprintf(l, sizeof l, "PFA:i=native:0,s=255,at=%d", b + 1); tx(l); }
           tx("**C:bpm=120,ph=0,bpb=4,beat=0"); },
    [](uint32_t t) { if (t == 0) tx("**C:bpm=120,ph=0,bpb=4,beat=0"); }, 11900 },
  // ---- what the brightest channel cannot see, and what a quarter-of-the-ceiling step let through ----
  { "C seeks between a white and a blue section (the brightest channel never moves)",
    []() { pShow(); tx("PFA:i=solid,c=ffffff,at=0"); tx("PFA:i=solid,c=0000ff,at=100"); },
    [](uint32_t t) { if (t == 0) tx("P*C:bpm=120,beat=100"); if (t == 50) tx("P*C:bpm=120,beat=0"); }, 100 },
  { "C seeks between a red and a blue section",
    []() { pShow(); tx("PFA:i=solid,c=ff0000,at=0"); tx("PFA:i=solid,c=0000ff,at=100"); },
    [](uint32_t t) { if (t == 0) tx("P*C:bpm=120,beat=100"); if (t == 50) tx("P*C:bpm=120,beat=0"); }, 100 },
  { "C seeks between a native section and a red one", []() { pShow(); tx("PFA:i=native:18,at=0"); tx("PFA:i=solid,c=ff0000,at=100"); },
    [](uint32_t t) { if (t == 0) tx("P*C:bpm=120,beat=100"); if (t == 50) tx("P*C:bpm=120,beat=0"); }, 100 },
  { "a white accent between two colours of a live look", []() { pShow(); tx("PFA:i=solid,c=ff0000"); },
    [](uint32_t t) { if (t == 0) tx("PFP:c=ffffff,d=170"); if (t == 20) tx("PFA:i=solid,c=0000ff"); if (t == 340) tx("PFP:c=ffffff,d=300");
                     if (t == 360) tx("PFA:i=solid,c=ff0000"); }, 680 },
  { "white by a colour change, X, white again by an accent (two luminance onsets, one brightness onset)",
    []() { pShow(); tx("PFA:i=solid,c=0000ff"); },
    [](uint32_t t) { if (t == 0) tx("PFA:i=solid,c=ffffff"); if (t == 10) tx("PFX"); if (t == 20) tx("PFP:c=ffffff,d=170");
                     if (t == 200) tx("PFA:i=solid,c=0000ff"); }, 400 },
  { "live A between red and blue, as fast as it can be sent", pShow,
    [](uint32_t t) { if (t == 0) tx("PFA:i=solid,c=ff0000"); if (t == 30) tx("PFA:i=solid,c=0000ff"); }, 60 },
  // I heart U flashes by itself (once in 1,040 ms): X and a relight just after its own ON edge.
  { "native:7 (it flashes by itself), X and a relight just after its own ON edge", []() { pShow(); tx("PFA:i=native:7"); },
    [](uint32_t t) { if (t == 510) tx("PFX"); if (t == 520) tx("PFA:i=solid,c=ffffff"); if (t == 690) tx("PFA:i=native:7"); }, 1205 },
  { "B between 0 and 63 (under a quarter of the ceiling)", []() { pShow(); tx("PFA:i=solid,c=ffffff"); },
    [](uint32_t t) { if (t == 0) tx("PFB:b=0"); if (t == 50) tx("PFB:b=63"); }, 100 },
  { "B between 192 and 255 (the top quarter)", []() { pShow(); tx("PFA:i=solid,c=ffffff"); },
    [](uint32_t t) { if (t == 0) tx("PFB:b=192"); if (t == 50) tx("PFB:b=255"); }, 100 },
  { "the top fifth of a meter following L", []() { pShow(); tx("PFA:i=meter,c=ffffff"); tx("P*L:v=255"); },
    [](uint32_t t) { if (t == 0) tx("P*L:v=204"); if (t == 50) tx("P*L:v=255"); }, 100 },
  // Looks whose own animation moves the colour, at their fastest: the hue goes round, and with it
  // the luminance (blue is a twentieth of green), while the brightest channel stays where it is.
  { "rainbow at its fastest", []() { pShow(); tx("PFA:i=rainbow,s=255"); }, [](uint32_t) {}, 1000 },
  { "colorcycle at its fastest", []() { pShow(); tx("PFA:i=colorcycle,s=255"); }, [](uint32_t) {}, 1000 },
  { "gradient at its fastest", []() { pShow(); tx("PFA:i=gradient,s=255"); }, [](uint32_t) {}, 1000 },
  { "B 0/255 under a native look", []() { pShow(); tx("PFA:i=native:17"); },
    [](uint32_t t) { if (t == 0) tx("PFB:b=0"); if (t == 50) tx("PFB:b=255"); }, 100 },
  { "the pump under a native look at 300 BPM", []() { pShow(); tx("PFA:i=native:16,m=255,am=2"); tx("**C:bpm=300,ph=0,bpb=4,beat=0"); },
    [](uint32_t t) { if (t == 0) tx("**C:bpm=300,ph=0,bpb=4,beat=0"); }, 150 },
  // PSI: a pulse over the board's own native look (L3), and the native look coming back, again and again.
  { "P over the native look", []() {},
    [](uint32_t t) { if (t == 0) tx("PFP:c=ffffff,d=60"); if (t == 100) tx("PFA:i=off"); if (t == 150) tx("PFM:v=idle"); }, 200 },
};
static const int kProfileCount = (int)(sizeof kProfiles / sizeof kProfiles[0]);

struct Result {
  int worst = 0; uint32_t minGap = 0xFFFFFFFFu, narrowGap = 0xFFFFFFFFu, lumGap = 0xFFFFFFFFu, colourGap = 0xFFFFFFFFu;
  long overCeiling = 0, notNative = 0, notDark = 0, wdLate = 0, notButton = 0, offList = 0, reached = 0, colourChanges = 0, lumOnsets = 0;
  long onsets = 0, own = 0, darkChecked = 0, nativeChecked = 0, contractFrames = 0, lines = 0, presses = 0, buttonChecked = 0;
  void take() {
    for (int i = 0; i < 10; i++) {
      const Onsets& o = gObs[i];
      if (o.worst > worst) worst = o.worst;
      uint32_t& g = i < 3 ? minGap : (i < 7 ? narrowGap : lumGap);
      if (o.minGap < g) g = o.minGap;
    }
    colourGap = gColourGap; colourChanges = gColourChanges; lumOnsets = gObs[7].total;
    overCeiling = gOverCeiling; notNative = gNotNative; notDark = gNotDark; wdLate = gWdLate; notButton = gNotButton; offList = gOffList; reached = gReached;
    onsets = gObs[0].total; own = gObs[0].own; darkChecked = gDarkChecked; nativeChecked = gNativeChecked; contractFrames = gContractFrames;
    lines = gLines; presses = gPresses; buttonChecked = gButtonChecked;
  }
  void add(const Result& o) {
    if (o.worst > worst) worst = o.worst;
    if (o.minGap < minGap) minGap = o.minGap;
    if (o.narrowGap < narrowGap) narrowGap = o.narrowGap;
    if (o.lumGap < lumGap) lumGap = o.lumGap;
    if (o.colourGap < colourGap) colourGap = o.colourGap;
    colourChanges += o.colourChanges; lumOnsets += o.lumOnsets;
    overCeiling += o.overCeiling; notNative += o.notNative; notDark += o.notDark; wdLate += o.wdLate; notButton += o.notButton;
    offList += o.offList; reached += o.reached;
    onsets += o.onsets; own += o.own; darkChecked += o.darkChecked; nativeChecked += o.nativeChecked; contractFrames += o.contractFrames;
    lines += o.lines; presses += o.presses; buttonChecked += o.buttonChecked;
  }
  bool holds() const {
    return worst <= 3 && minGap >= 2 * STROBE_MIN_STATE_MS && narrowGap >= 2 * STROBE_MIN_STATE_MS &&
           lumGap >= 2 * STROBE_MIN_STATE_MS && colourGap >= STROBE_MIN_STATE_MS && overCeiling == 0 && notNative == 0 && notDark == 0 && wdLate == 0 &&
           notButton == 0 && offList == 0 && reached == 0;
  }
  void print(const char* verdict, const char* name) const {
    printf("%s S9 %s: %d onsets in one second", verdict, name, worst);
    if (minGap != 0xFFFFFFFFu) printf(" (closest two %u ms apart)", minGap);
    if (narrowGap < 2 * STROBE_MIN_STATE_MS) printf(", narrow-band onsets %u ms apart", narrowGap);
    if (lumGap < 2 * STROBE_MIN_STATE_MS)    printf(", luminance onsets %u ms apart", lumGap);
    if (colourGap < STROBE_MIN_STATE_MS)     printf(", colour changes %u ms apart", colourGap);
    if (notDark)     printf(", %ld ms lit after X", notDark);
    if (notNative)   printf(", %ld ms not native after idle", notNative);
    if (overCeiling) printf(", %ld frames over the ceiling", overCeiling);
    if (wdLate)      printf(", the watchdog was late %ld times", wdLate);
    if (notButton)   printf(", %ld ms not on the mode a native command started", notButton);
    if (offList)     printf(", %ld frames of a native mode off the allow-list", offList);
    if (reached)     printf(", %ld lines wrote a native meter's levels", reached);
    printf("\n");
  }
};

// One job is one stream in a fresh firmware. Jobs 0..(8 * profiles - 1) are the profile rounds,
// the rest are random streams.
static const int kRounds = 8;
static void runJob(int job, uint32_t seed) {
  gRng = (seed ^ ((uint32_t)job * 2654435761u)) | 1u;
  if (job < kProfileCount * kRounds) {
    const Profile& p = kProfiles[job / kRounds]; const int round = job % kRounds;
    const bool noise = (round & 1) != 0;
    begin(round < 4 ? 20 : 200, false);
    p.setup();
    for (int i = 0; i < 500; i++) step();          // the set-up settles (and its live look's hold ends)
    const uint32_t jitter = rnd(4) * 10;
    for (uint32_t t = 0; t < 8000; t++) {
      p.at((t + jitter) % p.period);
      if (noise && rnd(400) == 0) { const std::string who = pick({ "R", "F" }); tx("P" + who + pick({ "Q:r=0", "L:v=128", "V:n=0,k=0000", "B:b=255" })); }
      step();
    }
    return;
  }
  gButtons = rnd(2) == 0;                          // half the streams have an owner at the controller
  const int owner = rnd(3) == 0 ? (int)(20 + rnd(181)) : 20;
  begin(owner, gButtons);                          // with an owner: alwaysOn, so a native mode keeps running
  const uint32_t busy = 20 + rnd(300);             // one line every `busy` ms on average
  const bool fine = rnd(4) == 0;                   // a quarter pass through the loop every 1 to 4 ms, not every 1
  for (uint32_t t = 0; t < 12000; ) {
    if (rnd(busy) == 0) { const bool burst = rnd(8) == 0; for (uint32_t n = 1 + (burst ? rnd(12) : 0); n > 0; n--) tx(randomLine()); }
    if (gButtons && rnd(3500) == 0) {              // about three native commands a stream
      static const int kMode[] = { 1, 1, 15, 15, 0, 16, 17, 18, 21 };
      press(kMode[rnd(9)]);
    }
    const uint32_t slow = rnd(200) == 0 ? 10 + rnd(4) * 10 : (fine ? 1 + rnd(4) : 1);   // now and then a slow pass
    step(slow); t += slow;
  }
}

int main() {
  const uint32_t seed = getenv("PROPERTY_SEED") ? (uint32_t)strtoul(getenv("PROPERTY_SEED"), nullptr, 0) : 0x9E3779B9u;
  const int streams = getenv("PROPERTY_STREAMS") ? atoi(getenv("PROPERTY_STREAMS")) : 1500;
  const int jobs = kProfileCount * kRounds + streams;
  if (const char* one = getenv("PROPERTY_JOB")) {  // hunt: one job, in this process, traced
    gTrace = true;
    runJob(atoi(one), seed);
    if (getenv("PROPERTY_LOG")) for (const std::string& l : gLog) puts(l.c_str());   // the whole stream
    Result r; r.take(); r.print(r.holds() ? "ok   " : "FAIL ", "job");
    return r.holds() ? 0 : 1;
  }
  std::vector<Result> profile(kProfileCount); Result random, all; int fails = 0; int firstBad = -1;
  for (int job = 0; job < jobs; job++) {
    int fd[2];
    if (pipe(fd) != 0) { perror("pipe"); return 2; }
    fflush(stdout);
    const pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 2; }
    if (pid == 0) {
      close(fd[0]);
      runJob(job, seed);
      Result r; r.take();
      const ssize_t w = write(fd[1], &r, sizeof r);
      _exit(w == (ssize_t)sizeof r ? 0 : 3);
    }
    close(fd[1]);
    Result r; const ssize_t got = read(fd[0], &r, sizeof r);
    close(fd[0]);
    int status = 0; waitpid(pid, &status, 0);
    if (got != (ssize_t)sizeof r || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      printf("FAIL  S9 job %d: the firmware did not finish the stream (crash or sanitizer report)\n", job);
      fails++; if (firstBad < 0) firstBad = job;
      continue;
    }
    if (!r.holds() && firstBad < 0) firstBad = job;
    if (!r.holds() && getenv("PROPERTY_EACH")) { char name[24]; snprintf(name, sizeof name, "job %d", job); r.print("FAIL ", name); }
    if (job < kProfileCount * kRounds) profile[job / kRounds].add(r); else random.add(r);
    all.add(r);
  }
  for (int i = 0; i < kProfileCount; i++) if (!profile[i].holds()) { profile[i].print("FAIL ", kProfiles[i].name); fails++; }
  if (!random.holds()) { random.print("FAIL ", "random streams"); fails++; }
  if (fails) {
    printf("%d S9 stream group(s) FAILED. First failing job: PROPERTY_JOB=%d PROPERTY_SEED=%u\n", fails, firstBad, seed);
    return 1;
  }
  printf("S9 property OK: %d profiles x %d rounds and %d random streams; %ld lines, %ld frames while the contract held the panel,\n"
         "   %ld onsets, the closest two %u ms apart (%ld more were a native look's own and set aside); %ld ms checked dark\n"
         "   after X, %ld ms checked native after idle, %ld native commands and %ld ms checked on their mode. Seed %u.\n",
         kProfileCount, kRounds, streams, all.lines, all.contractFrames, all.onsets, all.minGap, all.own, all.darkChecked,
         all.nativeChecked, all.presses, all.buttonChecked, seed);
  printf("   (narrow-band onsets: the closest two %u ms apart; luminance onsets: %ld, the closest two %u ms apart;\n"
         "   colour changes: %ld, the closest two %u ms apart)\n", all.narrowGap, all.lumOnsets, all.lumGap, all.colourChanges, all.colourGap);
  return 0;
}
