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
////////// ANIMATIONS ARE DATA, NOT CODE /////////
/////////////////////////////////////////////////

// EPITAPH for CONTRACT_SLIM, the define that used to live on this line. It was an opt-in
// "drop five heavy native novelty modes if the linker overflows" hatch — i_heart_u (7),
// red_heart front (9), Imperial March (11), lightsaberBattle (19), StarWarsIntro (20) —
// written back when the only way to shrink a mode was to delete its hand-written C++ body.
//
// It is gone because that premise is gone. Every native mode's animation, including those
// five, is now a flat PROGMEM byte string played by a small shared interpreter
// (include/psi_vm.h — "Animations are data, the interpreter is the only animation code.").
// A new animation costs tens of bytes of opcode data, not a new function; new bitmap art
// costs an additional 48 B/frame. All 22 upstream modes ship, in both the serial-only and
// I2C builds, with room to spare — see platformio.ini for the current measured sizes.




///////////////////////////////////////////////////
////////////////// I2C INTAKE ////////////////////
/////////////////////////////////////////////////

// THIS FORK IS SERIAL-ONLY BY DEFAULT. That is a deliberate DIVERGENCE from Neil
// Hutchison's upstream PSI Pro, which listens on BOTH the serial header and I2C
// (JawaLite address 22), and it is the one place this fork is less capable than the
// firmware it forked. Uncomment the line below to get the upstream behaviour back.
//
//     #define PSI_ENABLE_I2C     <-- uncomment for an I2C + serial board
//
// WHY. The Wire onReceive callback (receiveEvent, main.cpp) runs in TWI INTERRUPT
// CONTEXT, and it parses a command and renders LEDs from in there. FastLED's show()
// ends with an unconditional `sei` — it disables interrupts to bit-bang the WS2812
// data, then re-enables them — and Arduino's twi.c re-arms the I2C slave BEFORE
// invoking the callback. So a render inside that ISR turns interrupts back on with the
// slave live, and more I2C traffic during the render window RE-ENTERS the same ISR on
// top of the frame already on the stack. An AVR stack overflow does not trap: it walks
// down into .bss and silently corrupts globals. That is a genuinely nasty bug to chase
// on a bench, and it is worse on this board than on its siblings because the ATmega32U4
// has only ~1 KB of stack to give.
//
// The contract layer's own path is already deferred out of that ISR (see the I2C
// DEFERRAL note in src/contract/ContractPSI.h), so this switch is NOT about our code —
// it is about upstream's native path (receiveEvent -> parseCommand -> runPattern ->
// allOFF -> FastLED.show), which we will not rewrite in a fork that carries Neil's name.
// Compiling I2C out removes the TWI vector from the image entirely, and with it the last
// interrupt that can reach a render at all.
//
// WHAT IT BUYS (measured, not estimated; as of feature/animation-vm — re-measure with
// `pio run && pio run -e PSIPro-i2c` and test/host/stack_report.py rather than trusting
// this comment as it ages):
//                                serial-only      + I2C
//     flash                  26,666 B (93.0%)   28,108 B (98.0%)
//     SRAM                    1,309 B (51.1%)    1,590 B (62.1%)
//     stack budget                    1,251 B            970 B
//     worst-case stack           371 B (30%)       441 B (45%)
//     can an ISR reach a render?          NO      yes (native path)
//
// WHAT IT DOES NOT COST: all 22 upstream modes ship in BOTH configurations — there is
// no longer a "drop modes to make room" trade here (see the ANIMATIONS ARE DATA note
// above). The only difference between the two columns is the I2C intake itself.
//
// WHEN YOU NEED TO TURN IT BACK ON: if anything on your droid addresses this board over
// I2C (a MarcDuino, a Teeces/STEALTH setup, any JawaLite master on the bus at address
// 22), you want PSI_ENABLE_I2C. With it off, the board simply will not hear them — it
// does not fail loudly, it just never answers. Both configurations are built and
// type-checked by test/host/run.sh, so neither can rot.

//#define PSI_ENABLE_I2C


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
