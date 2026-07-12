# DroidNet_PSIPro

DroidNet contract fork of the **PSI Pro** firmware (Maxstang **MaxPSI** / MaxStang PSI PRO),
customized to implement the **Driveable-Animation Contract** so Cantina Studio can drive the 48-LED
PSI matrix with arbitrary RGB, beat-locked accents, a Studio-seeded beat-clock, and autonomous
board-side section playback — **additively**, with every native JawaLite `<addr>T/A/D/P` command and
I²C intake still working.

- **Role:** sub-project C of the Cantina dome light-show program (the hardest fork and pacing item).
- **Seeded from:** `travisccook/C2B5` @ `2fbd4023` (subdir `PSIPro`), 2026-07-12 — git-tracked files only.
- **MCU/toolchain:** ATmega32U4 (Sparkfun Pro Micro), PlatformIO/AVR. **Flash headroom is the binding
  constraint — bench-compile early;** an opt-in `CONTRACT_SLIM` build drops novelty modes if the linker
  overflows (default build keeps them for backward compat).
- **Fork spec (authoritative):** `DroidNetSignalBooster/docs/superpowers/specs/2026-07-12-psi-pro-fork-design.md`
  (+ the contract v1.1 amendment and transport spec).
- **First code task:** fix the pre-existing `buildCommand` off-by-one OOB (`src/main.cpp:2295-2296`) before
  feeding longer contract lines.
- **GitHub remote:** TBD (created later).
- **Status:** baseline import — no contract code added yet.
