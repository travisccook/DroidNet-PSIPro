// Release version 1.2

///////////////////////////////////////////////////
//////////////// PSI CONFIGURATION ////////////////
///////////////////////////////////////////////////


///////////////////////////////////////////////////
///////////////// Timer Settings /////////////////
/////////////////////////////////////////////////

// The numbered pattern Modes have various preprogrammed lengths
// to match those of the Teeces Logic patterns. Some of the additional Modes 
// have indefinite lengths.  If you want ALL pattern Modes called using the
// Mode (T) command to remain on, then set 'alwaysOn' below to true. 
// The default is false, each selected pattern Mode will remain on for 
// its set time, and then return to the default pattern (swipe) Mode. 
// This can also ????
// The Current Teeces interface runs at a mind numbingly slow 2400 only!

bool alwaysOn = true;


// If your JEDI Device can send at 9600 baud, uncomment this line.
// The Current Teeces interface runs at a mindnumbingly slow 2400 only!

#define _9600BAUDSJEDI_


///////////////////////////////////////////////////
////////////// SET DEFAULT PATTERN ///////////////
/////////////////////////////////////////////////

// Any display Mode can be the default Mode the PSI returns to
// after completing a command initiated Mode.  The standard default Mode
// is Swipe.  Use this to set the default Mode number.

uint8_t defaultPattern = 1; //Mode 1 is Swipe


///////////////////////////////////////////////////
////////// CONTRACT FLASH ESCAPE HATCH ///////////
/////////////////////////////////////////////////

// REQUIRED, NOT OPTIONAL — and it was not always so. This was written as an opt-in
// "only if the linker overflows" hatch, back when nothing had ever been cross-compiled
// and the absolute flash cost was unknown. It has now been linked for real, and the
// honest numbers are:
//
//   stock upstream PSI Pro, no contract ....... 25,106 B of 28,672 B (87.6%)
//
//   as this fork was ORIGINALLY published (before any of the flash work):
//     this hatch OFF .......................... 38,790 B  (135.3% — WILL NOT LINK)
//     this hatch ON ........................... 32,394 B  (113.0% — WILL NOT LINK)
//
//   the code as it stands TODAY:
//     shipped (hatch ON + the codegen work) ... 26,946 B  (94.0%) — fits, 1,726 B spare
//     the same code with this hatch OFF ....... 33,782 B  (117.8% — WILL NOT LINK)
//     the same code, codegen flags removed .... 28,896 B  (100.8% — WILL NOT LINK)
//
// Upstream already used 87.6% of this chip. Only ~3.5 KB was ever free and the contract
// layer needs ~13.7 KB, so the hatch alone cannot save the build — it reclaims 6,400 B
// (not the "~3-5 KB" originally guessed here), and that still leaves it 3,722 B over.
// It fits only in combination with the flash work in platformio.ini's build_flags and the
// PSI_NOINLINE leaf-outlining in src/contract/ContractPSI.h. Turn any one of those three
// off and the image overflows again.
//
// WHAT IT COSTS YOU: the heaviest novelty native modes are dropped — Mode 7 (i_heart_u),
// Mode 9 (red_heart, front half), Mode 11 (Imperial March), Mode 19 (lightsaberBattle),
// Mode 20 (StarWarsIntro). Under -ffunction-sections/--gc-sections they become
// unreferenced and are stripped. Everything else — every other native mode, the whole
// JawaLite grammar, the I2C intake — is untouched. DiscoBall (12/13) is deliberately KEPT,
// because the contract's 'sparkle' effect maps onto it.
//
// If you do not want to lose those five modes, do not build this fork: flash Neil
// Hutchison's upstream PSI Pro instead. There is no configuration of this fork that keeps
// them AND fits in 28,672 B.

#define CONTRACT_SLIM


///////////////////////////////////////////////////
////////////// SWIPE MODE SETTINGS ///////////////
/////////////////////////////////////////////////

// Colors are divded into Primary (Default is Blue for the 
// front PSI and Green for the Rear) and Secondary (Default
// is Red for the front PSI and Yellow for the rear).

// The number of milliseconds the PSI pauses on the primary color
// before switching to the secondary color.
// A random value between MINIMUM and MAXIMUM will be used.

#define PRIMARY_COLOR_DURATION_MINIMUM 2000  // Default 2000
#define PRIMARY_COLOR_DURATION_MAXIMUM 10000 // Default 10000

// Number of milliseconds that the secondary color will be
// visible before switching back to the primary color.
// A random value between MINIMUM and MAXIMUM will be used.

#define SECONDARY_COLOR_DURATION_MINIMUM 5000  // Default 4000
#define SECONDARY_COLOR_DURATION_MAXIMUM 12000 // Default 12000

// Speed range of the swipe animation. Longer delay means
// slower animation speed. 

#define SWIPE_DELAY_MINIMUM 20    // Default 20
#define SWIPE_DELAY_MAXIMUM 50    // Default 50

// Define the chance proportion between the various options for
// the secondary color. Increasing a value compared to the others increases
// the likelihood of that option occuring. If the chance for an option is 
// set to 0, it will not be selected.

#define CHANCE_SECONDARY_FULL 6
#define CHANCE_SECONDARY_PARTIAL 4
#define CHANCE_SECONDARY_PARTIAL_OFF 6

// How many columns to display the secondary color.

#define SECONDARY_PARTIAL_LINES_MIN 3 //The remainder will be the primary color.
#define SECONDARY_PARTIAL_LINES_MAX 6 //The remainder will be the primary color.
#define SECONDARY_PARTIAL_OFF_LINES 5 //The remainder will be off.

// Use the jumpers on the PSI CPU board to set Front colors (jumper off)
// or Rear colors (jumper on).

// Use the following settings to adjust the colors for font and rear.

// Set colors for the front PSI.
                                            // Default colors
CRGB frontPrimaryColor = CRGB(0, 0, 255);   // Blue (0, 0, 255)
CRGB frontSecondaryColor = CRGB(255, 0, 0); // Red  (255, 0, 0)
CRGB frontSecondaryOffColor = CRGB::Black;  // Off Black

// Colors for the rear PSI
                                              // Default colors
CRGB rearPrimaryColor = CRGB(0, 255, 0);      // Green  (0, 255, 0)
CRGB rearSecondaryColor = CRGB(200, 170, 0);  // Yellow (200, 170, 0)
CRGB rearSecondaryOffColor = CRGB::Black;     // Off Black

#if defined(ARDUINO_AVR_PRO)
#define JUMP_FRONT_REAR 12
#else
#define JUMP_FRONT_REAR 14
#endif

// Set the colors based on the pin being jumpered to ground.

CRGB primary_color() {
  if (digitalRead(JUMP_FRONT_REAR)) {
    return frontPrimaryColor;
  } else {
    return rearPrimaryColor;
  }
}

CRGB secondary_color() {
  if (digitalRead(JUMP_FRONT_REAR)) {
    return frontSecondaryColor;
  } else {
    return rearSecondaryColor;
  }
}

CRGB secondary_off_color() {
  if (digitalRead(JUMP_FRONT_REAR)) {
    return frontSecondaryOffColor;
  } else {
    return rearSecondaryOffColor;
  }
}

///////////////////////////////////////////////////
//////////////// Serial SETTINGS ///////////////// 
/////////////////////////////////////////////////

// If USB_SERIAL is defined, the Serial port on the USB of the 
// Pro Micro will be used for communication, and debug output
// Uncomment this if you want to debug, add new patterns etc,
// and are working via USB.  Note the brigtness warning on the main
// sketch tab! The normal mode is that any serial control device (MarcDuino,
// STEALTH etc) will be connected to the PSI via the header pins on the
// PSI PCB by default.  These pins are referred to as Serial1.
// Uncommenting the line beow switches to using the USB port
// and the Serial on the USB of the Pro Micro instead.

// If you are using an Arduino with only one serial connection such
// as the Pro Mini, then you will want to uncomment this line to ensure
// the sketch uses Serial and not Serial1 for communication.  

//#define USB_SERIAL
#if defined(ARDUINO_AVR_PRO) && !defined(USB_SERIAL)
#define USB_SERIAL
#endif

///////////////////////////////////////////////////
//////////// Assign IC2 Address Below ////////////
/////////////////////////////////////////////////

#if !defined(ARDUINO_AVR_PRO)
#define USE_I2C 1
byte I2CAdress = 22;
#else
#define USE_I2C 0
#endif

///////////////////////////////////////////////////
/////////////////////////////////////////////////

// This is the pin for the Brighness POT

#if defined(ARDUINO_AVR_PRO)
#define POT_BRIGHT_PIN A1
#else
#define POT_BRIGHT_PIN 19
#endif

///////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
// CHANGES BEYOND THESE LINES SHOULD NOT BE NECESSARY ///
////////////////////////////////////////////////////////
///////////////////////////////////////////////////////

// This is Neil's personal setup ... probably don't play with this!
//#define NEIL_PERSONAL_DEBUG
#ifdef NEIL_PERSONAL_DEBUG
  #define DEBUG       // Prints Debug Strings to help debugging
  #define USB_SERIAL  // Sets the Serial to use the USB port for sending and receiving commands instead of the TxRx on the board.
#endif
// End Neil's personal setup.

//Setup Debug stuff

// Uncomment this if you want Debug output.
// By default debug output is not enabled in
// the release version
//#define DEBUG

//Setup Debug stuff for Real Arduino Pro Micros
#ifdef DEBUG
    #define DEBUG_PRINT_LN(msg)  serialPort->println(msg)
    #define DEBUG_PRINT(msg)  serialPort->print(msg)
#else
  #define DEBUG_PRINT_LN(msg)
  #define DEBUG_PRINT(msg)
#endif // DEBUG

#define UNUSED(a) (void)(a);
#define FALL_THROUGH() __attribute__((fallthrough));

#define LED_PIN 4
#define NUM_LEDS 48
#define LEDS_PER_COLUMN 6
#define COLUMNS 10
 
// Addressible LED Array
// -1 = no LED in that grid space

  int8_t ledMatrix[COLUMNS][LEDS_PER_COLUMN] = {
    { -1, -1, 23, 24, -1, -1, },
    { -1,  6, 22, 25, 41, -1, },
    {  5,  7, 21, 26, 40, 42, },
    {  4,  8, 20, 27, 39, 43, },
    {  3,  9, 19, 28, 38, 44, },
    {  2, 10, 18, 29, 37, 45, },
    {  1, 11, 17, 30, 36, 46, },
    {  0, 12, 16, 31, 35, 47, },
    { -1, 13, 15, 32, 34, -1, },
    { -1, -1, 14, 33, -1, -1, } 
  };


// Command processing stuff
// maximum number of characters in a command (95 chars since we need the null termination)
//
// DroidNet fork: raised 64 -> 96 (+32 B SRAM). buildCommand() DROPS every byte past
// CMD_MAX_LENGTH-1 and terminates there — an over-long line is silently TRUNCATED and then
// parsed as if it were complete. A v1.2 scored contract line carrying an accent, e.g.
//   !P*A:i=colorcycle,c=3b82f6,at=1234,am=2,m=200,ae=flash,ac=ffffff
// is 64 characters, one past the old 63-char ceiling: the trailing accent key would be
// chopped and the section would silently render with no accent. 96 covers the longest line
// the Studio emitter can produce (pinned by the buffer guard in test/host/run.sh).
// NOTE: preamble.h defines this macro too, and is included FIRST (main.cpp:286) — the two
// MUST be kept identical, or the definition that reaches buildCommand() is not this one.
#define CMD_MAX_LENGTH 96

// memory for command string processing
char cmdString[CMD_MAX_LENGTH];

// POT Averager
#define POT_AVG_SIZE 30
// Change this if you get flicker.  A larger number will reduce POT noise.
#define POT_VARIANCE_LEVEL 2
uint16_t POTReadings[POT_AVG_SIZE];
uint8_t POTIndex = 0;
unsigned long POTSum = 0;
uint8_t POTCount = 0;

// EEPROM SETTINGS
int alwaysOnAddress = 0;
int externalPOTAddress = 1;
int internalBrightnessAddress = 2;
