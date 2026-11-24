// test/host/review_r2_profiles.inc — REVIEW FINDINGS on the R2 line, as hostile profiles for
// test_property.cpp's observers (the whole field, halves, quarters and bands of the panel, by two
// pairs of thresholds; asserted for profiles). Built by run.sh's last stage with
// -DPROPERTY_PROFILES="review_r2_profiles.inc" and PROPERTY_STREAMS=0: these profiles and no
// random streams. Each profile runs 8 rounds, the first four at the owner's brightness 20 (the
// bench's), the last four at 200. See review_r2.inc for the row observer's view of the same.
static const Profile kProfiles[] = {
  // FINDING 1. One scored vu section with a flash accent, and a clock. The accent lights the whole
  // panel on the beat; when its overlay ends the pump's level is taken at once (the accent never
  // touched the level's window), and the bottom band (rows 4 and 5) lights again 208 ms after the
  // accent lit it, four times in a second, at the owner's 20 (rounds 0 to 3). The green rows are
  // under the field gate's step there. FAILS today.
  { "REVIEW finding 1: a scored vu with a flash accent on every beat, 120 BPM",
    []() { pShow(); tx("PFA:i=vu,c=ffffff,s=255,am=2,m=255,at=0,ae=flash,ac=ffffff"); tx("**C:bpm=120,ph=0,bpb=4,beat=0"); },
    [](uint32_t) {}, 1000 },
  { "REVIEW finding 1: a scored vu with a flash accent on the downbeats, 120 BPM",
    []() { pShow(); tx("PFA:i=vu,c=ffffff,s=255,am=1,m=255,at=0,ae=flash,ac=ffffff"); tx("**C:bpm=120,ph=0,bpb=4,beat=0"); },
    [](uint32_t) {}, 1000 },
  { "REVIEW finding 1: a scored vu with a flash accent on every beat, 90 BPM",
    []() { pShow(); tx("PFA:i=vu,c=ffffff,s=255,am=2,m=255,at=0,ae=flash,ac=ffffff"); tx("**C:bpm=90,ph=0,bpb=4,beat=0"); },
    [](uint32_t) {}, 1000 },
  // The R1 controls, which hold today: the same lines with i=meter (the walk; and fed by L).
  { "REVIEW control: a scored meter with a flash accent on every beat, 120 BPM",
    []() { pShow(); tx("PFA:i=meter,c=ffffff,s=255,am=2,m=255,at=0,ae=flash,ac=ffffff"); tx("**C:bpm=120,ph=0,bpb=4,beat=0"); },
    [](uint32_t) {}, 1000 },
  { "REVIEW control: a scored meter fed by L with a flash accent on every beat, 120 BPM",
    []() { pShow(); tx("PFA:i=meter,c=ffffff,s=255,am=2,m=255,at=0,ae=flash,ac=ffffff"); tx("**C:bpm=120,ph=0,bpb=4,beat=0"); },
    [](uint32_t t) { if (t == 0) tx("P*L:v=90"); }, 100 },
  // FINDING 2 by these observers: pauses and resumes. They hold here (what relights is a row, under
  // a quarter of the field: review_r2.inc's row observer sees it); kept so the two views stay side by side.
  { "REVIEW finding 2 (holds by region): pause and resume a pumped vu",
    []() { pShow(); tx("PFA:i=vu,c=ffffff,s=255,am=2,m=255"); tx("**C:bpm=120,ph=0,bpb=4,beat=0"); },
    [](uint32_t t) { if (t == 0) tx("**C:bpm=0"); if (t == 300) tx("**C:bpm=120,ph=0,bpb=4,beat=0"); }, 710 },
};
