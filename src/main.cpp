/**********************************************************************************************************
 *  Maxstang's MaxPSI Sketch for the PSI PRO Connected
 *  Written by Neil Hutchison
 *  Main sequence transitions by Krijn Schaap, based on his PSI sketch.  Many thanks Krijn.
 *  Pattern Timing Tuning by Malcolm MacKenzie
 *  Bug fixes and Mini support by Skelmir
 *  
 *
 *  Thanks to Malcolm (Maxstang) for the boards, support, testing and encouragement.
 *  
 *
 *    
 *  BEFORE BUILDING OR UPLOADING THIS SKETCH, be sure that the config.h and matrices.h files are in the skectch folder. 
 *
 *  Version 1.7
 *
 *  Version History :
 *  
 *  Version 1.7 - 30th Dec 2020
 *  
 *  Fix a bug in the main loop restoring the default pattern
 *  Remove compiler warnings
 *  Support for Mini added.
 *  
 *  Version 1.6 - 14th May 2020
 *  
 *  Fixes for the Valid PSI address checks in the T command
 *  Check for a valid pattern number, and ignore if the pattern does not match a known pattern.
 *    Continue running the current pattern with the current timings.
 *  
 *  Version 1.5 - 13th May 2020
 *  
 *  Adding a check in the T command processing to prevent setting the global timing parameters if
 *  the command is not addressed to a PSI (address 0, 4,5)
 *  
 *  Version 1.3 - 21st April 2020
 *  
 *    Fixed a bug with the timing for Imperial March
 *  
 *  Version 1.2 - 16th April 2020
 *  
 *    Correct comment typos
 *    Always on was actually only on for 17 min. Changed to +18 hrs.
 *    Change Star Wars Intro
 *  
 *  Version 1.1 - 13th April 2020
 *  
 *    Fixed a bug in the Rebel pattern where it would blink the first time a timing command was given
 *      Subsequent calls to Rebel with timing supplied worked
 *    Explicitly check in Fade Out and Lightsaber Battle for a timing parameter supplied and ignore it
 *    All CRGB:White changed to CRGB:Grey to reduce power consumption of the panel
 *    Max Brightness allowed upped to 200 from 175
 *    Renamed the sketch to match git repo
 *  
 *  Version 1.0 - 11th April 2020
 *    Added 3Pyy command to set brightness without saving to EEPROM
 *    Limit the Max LED Brightness to 200 to preserve the LED Life.
 *  
 *  Version 0.99_5 - 10th April 2020
 *    Renamed USB_DEBUG to USB_SERIAL
 *  
 *  Version 0.99_4 - 10th April 2020
 *  Fixed the Command line setting for per pattern timeout that was added
 *    Timings over 32 seconds did not work
 *    To set the pattern as "always On" Set the timing parameter to 256 which will
 *    run the pattern for 16 hours - I'll call that good enough for always on!
 *  Added Firmware Average for the POT readings.  This works around the issue of not
 *  having a resitor on the POT.
 *  
 *  Version 0.98 - 8th April 2020
 *  Added the ability for each sequence to run for a given time.
 *    Rather than try to set the time a pattern runs for by setting the loops, you can
 *    specify the total time the pattern should run for.  To disable the total run time
 *    and use a set number of loops, set the run time parameter to 0.
 *  Added the ability to set the command duration in seconds via the command.
 *    This only applies to T commands.  
 *    Send the command using 0Tx|y where |y is optional.  y is in seconds.
 *  
 *  Version 0.97 - 7th April 2020
 *  Added ability to set Disco Ball and VU Meter on indefinitely.
 *      Mode 13 is the new Always on Disco Ball
 *      Mode 12 is the timed Disco Ball 
 *      Mode 92 is VU Meter (always on) to match Logic commanding
 *      Mode 21 is VU Meter timed
 *  Restored the fast switch between USB Serial and Tx/Rx Pin Serial
 *  
 *  Version 0.96 - 5th April 2020
 *  Added address checking for T commands
 *  0 is all
 *  4 is Front PSI
 *  5 is rear PSI as taken from Marc's Teeces command guide.
 *  
 *      Address field is interpreted as follows:
 *      0 - global address, all displays that support the command are set
 *      1 - TFLD (Top Front Logic Dislay)
 *      2 - BFLD (Bottom Front Logic Display)
 *      3 - RLD  (Rear Logic Display)
 *      4 - Front PSI
 *      5 - Rear PSI
 *      6 - Front Holo (not implemented here)
 *      7 - Rear Holo  (not implemented here)
 *      8 - Top Holo   (not implemented here)
 *  
 *  Version 0.9.5 - 5th April 2020
 *  Star Wars scrolling text sequence added
 *  Minor bug fixes
 *  
 *  Version 0.94 - 4th April 2020
 *  Comments cleanup and clarification
 *  More timing tweaks
 *  Work around added for serial difficulties with non Sparkfun Pro Micro
 *  
 *  
 *  Version 0.93 - 1st April 2020 (Happy April Fools Day!)
 *  Code cleanup, and code size reduction
 *  Timing tweaks from Malcolm for various sequences.
 *  Updated JawaLite To support A, D and P (P used to change always on mode)
 *  T1 (Swipe) is now the default sequence, as MarcDuino sends 0T1 on startup.
 *  Added the ability to set the default pattern in the config.h  
 *      Note that MarcDuino will send 0T1, so whatever is in Mode 1 will be the starting pattern.
 *      After that point when a sequence completes, it will restore the "defaultPattern" 
 *      as defined in config.h
 *  EEPROM Support added to store various global settings:
 *      alwaysOn config
 *      Internal or External POT use
 *      Internal brightness setting if using Internal Brightness value (1P1 was sent)
 *  Fixed a bug in the VU Meter Sequence.
 *
 *  Version 0.8 - 31st March 2020
 *  Added Lightsaber Battle animation
 *  Added Pulse for rear logic dsiplay on T9
 *  Updated JawaLite Commanding on Serial to be 0Txx format
 *  Added the ability to change the serial port by defining USB_SERIAL.  
 *    Uncomment #define USB_SERIAL for serial comunications using Tx and Rx (removed again)
 *  Set the default behavior for unrecognised commands to just keep running the swipe pattern.
 *  Configuration data moved to config.h rather than being scattered.
 *
 *  Version 0.7 - 30th March 2020
 *  Non-Delay version of code.
 *  Allows sequences to be interrupted at ay time.
 *  Waiting for sequence completion is no longer required
 *  Set the default brightness in setup from the Brightness POT
 *
 *  Version 0.6 - 29th March 2020
 *  Base versions of most sequences implented
 *  Support for Front/Rear color selection using Jumper implemented
 *  Brightness Pot implemented
 * 
 * 
 *                           ***************************   
 *                           ********* WARNING *********
 *                           ***************************
 *                                       
 *          This PSI CAN DRAW MORE POWER THAN YOUR COMPUTER'S USB PORT CAN SUPPLY!! 
 *     
 *     When using the USB connection on the Pro Micro to power the PSI (during programming 
 *     for instance) be sure to have the brightness POT turned nearly all the way COUNTERCLOCKWISE.  
 *     Having the POT turned up too far when plugged into USB can damage the Pro Micro and/or your 
 *     computer's USB port!!!! If you are using the internal brightness control and are connected 
 *     to USB, KEEP THIS VALUE LOW, not higher than 20. The Pro Micro can also be removed from the 
 *     PSI and programmed separately. 
 *              
 *              
 *               
 *  ///////////////////////// COMMANDS AND COMMAND STRUCTURE /////////////////////////            
 * 
 *  
 *  Supported JAWALite Commands via Serial or i2c:
 *
 *  Serial:
 *
 *  Command T - Trigger a numbered Mode.  Txx where xx is the pattern number below. When using the R2 Touch app, commands
 *              should be in the form @0Tx\r or @0Txx\r. Please see below for address information for the T command. 
 *              
 *              The Optional time parameter can be sent by adding |yy to the T command.  Commands should be in the form
 *              @0Tx|y.  y is a value in seconds.
 *  
 *  Command A - Go to Main mode of operation which is Standard Swipe Pattern.
 *              @0A from R2 Touch
 *  
 *  Command D - Go to Default mode which is the Standard Swipe Pattern.
 *              @0D from R2 Touch
 *  
 *  Command xPy - Sets various board parameters.
 *                If x is 0, Set the alwaysOn behavior of the panel
 *                  The default mode for the panel is to display command sequences for 
 *                  a given time, then revert to the default pattern (swipe).  
 *                  By sending the xPy command, this can be changed.
 *                  y is either 0 or 1 (default or always on mode)
 *                  0P0 - Default mode, where default pattern (swipe) is restored after the sequence plays
 *                  0P1 - The sequence continues to play until a new comand is received.
 *                  
 *                If x is 1, Set the POT mode
 *                  The default is to read the external POT value for setting brightness
 *                  y is either 0 or 1 (Pot or internal setting)
 *                  1P0 - Default mode, uses the external POT to set the LED brightness
 *                  1P1 - Use the internal brightness, which is set using command 2Py below
 *                  
 *                If x is 2, Set the internal brightness value, overriding the POT.
 *                  The default setting is that brightness is 20.
 *                  y is a value between 0 (off) and 255 (max brightness) Values over 200 
 *                  will be limited to 200 to preserve the life of the LEDs. This value
 *                  is saved to the EPROM and will persist after power down. 
 *                  for example:  2Py or 2Pyy or 2Pyyy
 *                  
 *                If x is 3, Set the internal brighness value, overriding the POT, but do not save to EEPROM.
 *                  3P0 will restore the brightness to it's previous value.  If that was POT control, the POT setting
 *                  will be used, it if was internal brightness, then the previous global internal brightness witll be used.
 *                  3Pyyy will set the brightness in the range 1 to 200.  Values over 200 will be limited to 200 to preserve
 *                  the life of the LEDs.
 *               
 *               @xPy from R2 Touch (You don't need the '0' before the x when using the P command. 
 *                                          
 *                                       ***************************   
 *                                       ********   WARNING ********
 *                                       ***************************
 *                                       
 *              This PSI CAN DRAW MORE POWER THAN YOUR computer's USB PORT CAN SUPPLY!! When using the USB connection 
 *              on the Pro Micro to power the PSI (during programming for instance) be sure to have the brightness 
 *              POT turned nearly all the way COUNTERCLOCKWISE.  Having the POT turned up too far when plugged into 
 *              USB can damage the Pro Micro and/or your computer's USB port!!!! If you are using the internal brightness
 *              control and are connected to USB, KEEP THIS VALUE LOW, not higher than 20. The Pro Micro can also be removed
 *              from the PSI and programmed separately. 
 * 
 *  i2c:
 *
 *  When sending i2c command the Panel Address is defined on the config.h tab to be 22.  The command type and value are needed.  
 *  To trigger a pattern, send an address (0 for all, 4 for front, 5 for rear) then the character 'T' and the Mode value corresponding 
 *  to the pattern list below to trigger the corresponding sequence. Sequences must be terminated with a carriage return (\r).  
 *  
 *  Using i2c with the R2 Touch app, commands must be sent in hex. For example, &220T6\r would be spelled &22,x33,x54,x36,x0D\r
 *  
 *  Commands:
 *  
 *  Address modifiers for "T" commands.  The digit preceeding the T is the address:
 *  
 *  0 is all
 *  4 is Front PSI
 *  5 is Rear PSI as taken from Marc's Teeces command guide.
 *  
 *      Address field is interpreted as follows:
 *      0 - global address, all displays that support the command are set
 *      1 - TFLD (Top Front Logic Dislay)
 *      2 - BFLD (Bottom Front Logic Display)
 *      3 - RLD  (Rear Logic Display)
 *      4 - Front PSI
 *      5 - Rear PSI
 *      6 - Front Holo (not implemented here)
 *      7 - Rear Holo  (not implemented here)
 *      8 - Top Holo   (not implemented here)
 *
 *  Command T Modes
 *  Sensitivity to flashing lights can be as slow as 3x/second.  
 *    e.g. Flash, Alarm, Scream
 *  You must be cautious.
 * 
 *    Mode 0  - Turn Panel off (This will also turn stop the Teeces if they share the serial connection and the "0" address is used)
 *    Mode 1  - Default (Swipe) The default mode can be changed on the config.h tab
 *    Mode 2  - Flash (fast flash) (4 seconds) Use caution around those sensitive to flashing lights.  
 *    Mode 3  - Alarm (slow flash) (4 seconds)
 *    Mode 4  - Short Circuit (10 seconds)
 *    Mode 5  - Scream (4 seconds)
 *    Mode 6  - Leia Message (34 seconds)
 *    Mode 7  - I Heart U (10 seconds)
 *    Mode 8  - Quarter Panel Sweep (7 seconds)
 *    Mode 9  - Flashing Red Heart (Front PSI), Pulse Monitor (Rear PSI)
 *    Mode 10 - Star Wars - Title Scroll (15 seconds)
 *    Mode 11 - Imperial March (47 seconds)
 *    Mode 12 - Disco Ball (4 seconds)
 *    Mode 13 - Disco Ball - Runs Indefinitely
 *    Mode 14 - Rebel Symbol (5 seconds)
 *    Mode 15 - Knight Rider (20 seconds)
 *    Mode 16 - Test Sequence (White on Indefinitely)
 *    Mode 17 - Red on Indefinitely  
 *    Mode 18 - Green on Indefinitely
 *    Mode 19 - LightSaber Battle
 *    Mode 20 - Star Wars Intro (scrolling yellow "text" getting smaller and dimmer)
 *    Mode 21 - VU Meter (4 seconds)
 *    Mode 92 - VU Meter - Runs Indefinitely (Spectrum on Teeces)
 *    
 * Most users shouldn't need to change anything below this line. Please see the config.h tab    
 * for user adjustable settings.  
*/

// Include the various libraries that we need.
#include "preamble.h"
#include <FastLED.h>
#include <EEPROM.h>
#include "matrices.h"
#include "config.h"
#if USE_I2C
// SERIAL-ONLY BY DEFAULT — see the I2C INTAKE block in include/config.h. Uncommenting
// PSI_ENABLE_I2C there restores upstream's I2C/JawaLite intake (and its ISR nesting hazard).
#ifdef PSI_ENABLE_I2C
#include "Wire.h"
#endif
#endif

// Setup the LED Matrix
CRGB leds[NUM_LEDS];

// Brightness control
bool internalBrightness = false;
bool useTempInternalBrightness = false;
uint8_t globalBrightnessValue = 20;     // Set to a default of 20.  This is overridden in the P command or read from EEPROM.
uint8_t tempGlobalBrightnessValue = 20; // Used in the 3Pyyy command to temporarily use internal brightness for script use.
uint8_t previousglobalPOTaverage = 0;
uint8_t tempglobalPOTaverage = 10;
uint8_t globalPOTaverage = 10;          // Used to store the POT average for brightness setting with the POT.

// Command loop processing times
unsigned long previousMillis = 0;
unsigned long interval = 25;

//counters and state stuff
unsigned long doNext;
unsigned long globalTimeout;

// Global animation stuff is defined here.
int updateLed = 0;
int ledPatternState;
bool firstTime;
bool patternRunning = false;
uint8_t globalPatternLoops;

// Timing values received from command are stored here.
bool timingReceived = false;
unsigned long commandTiming = 0;

// Used for the VU display to store global state ...
int level[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

//Serial Stuff
int lastPSIeventCode = defaultPattern;
bool firstTimeCode   = true;

// handle to the Serial object
Stream* serialPort;

// Swipe Default pattern stuff
enum state {
  Primary,
  PrimaryToSecondary,
  Secondary,
  SecondaryToPrimary
};

// Global animation state for the Swipe Default sequence is defined here
state ledState = Primary;
uint8_t visibleSecondaryColumns = 0;
unsigned long nextEvent = 0;
unsigned long swipeDelay = 0;
unsigned long lastLedUpdate = 0;
CRGB overlayColors[COLUMNS];

// Animation VM: PROGMEM bytecode + the one interpreter that replaces the
// hand-written mode bodies. Included after the global state block (needs
// firstTime/patternRunning/leds/ledMatrix/...) and before ContractPSI.h.
#include "psi_vm.h"

// Driveable-Animation Contract fork layer. Included HERE (after the global state
// block above, and after functions.h prototyped the render primitives) so its
// inline dispatchers see leds[]/level[]/tempGlobalBrightnessValue/firstTime/
// serialPort + the primitive prototypes, and BEFORE loop()/serialEvent() so those
// can call parseContract()/contractLoopTick()/contractPulseTick(). Additive only.
#include "contract/ContractPSI.h"


// Setup
void setup() {

  // Setup LED defaults
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  pinMode(JUMP_FRONT_REAR, INPUT_PULLUP);

  //Sets the default brightness, this reads from the POT and sets the value
  FastLED.setBrightness(brightness());

#if USE_I2C
  //  Setup I2C
#ifdef PSI_ENABLE_I2C
  Wire.begin(I2CAdress);                   // Start I2C Bus as Master I2C Address
  Wire.onReceive(receiveEvent);            // register event so when we receive something we jump to receiveEvent();
#endif
#endif

  // Setup the Serial for debug and command input
  // initialize suart used to communicate with the JEDI Display at 2400 or 9600 bauds
  // This is set in config.h
  uint16_t baudrate;

  #ifdef _9600BAUDSJEDI_
    baudrate=9600;
  #else
    baudrate=2400;
  #endif

  // Setup for Official Pro Micro.  The offical PRO can switch like this.
  #ifdef USB_SERIAL
    // If we want to debug on the USB, then we use Serial
    Serial.begin(baudrate);
    serialPort=&Serial;
  #else
    Serial1.begin(baudrate);
    serialPort=&Serial1;
  #endif

  // READ the default settings from the EEPROM
  byte value;

  // Set the Always on Behavior.
  value = EEPROM.read(alwaysOnAddress);
  if (value == 0) {
    alwaysOn = false;
    DEBUG_PRINT_LN("Panel Behaviour set to default, run sequence then default");
  }
  if (value == 1) {
    alwaysOn = true;
    DEBUG_PRINT_LN("Panel Behaviour set to always on.  Send new command to change sequence.");
  }

  value = EEPROM.read(externalPOTAddress);
  if (value == 0) {
    internalBrightness = false;
    DEBUG_PRINT_LN("Using External POT for brightness control");
  }
  if (value == 1) {
    internalBrightness = true;
    DEBUG_PRINT_LN("Using Internal value for brightness control");
  }

  value = EEPROM.read(internalBrightnessAddress);
  globalBrightnessValue = value;
  DEBUG_PRINT("Global LED Brightness set to :"); DEBUG_PRINT_LN(globalBrightnessValue);
  
  DEBUG_PRINT_LN("Ready");
}


// Main loop
void loop()
{
  // Get current time.
  unsigned long currentMillis = millis();
  uint8_t delta;

  if (currentMillis - previousMillis > interval)
  {
    //DEBUG_PRINT_LN("Main Loop Tick");
    previousMillis = currentMillis;

    // Contract owns the frame when armed (beat-clock + score + envelope + render);
    // otherwise the native pattern path runs exactly as before. Additive.
    if (!contractLoopTick())
    {
      if (patternRunning)
      {
        runPattern(lastPSIeventCode);
      }
      else
      {
        lastPSIeventCode = defaultPattern;
        runPattern(lastPSIeventCode);
      }
    }

    // Grab the POT Average value.
    tempglobalPOTaverage = averagePOT();
    delta = (tempglobalPOTaverage >= previousglobalPOTaverage) ? tempglobalPOTaverage - previousglobalPOTaverage : previousglobalPOTaverage - tempglobalPOTaverage;
    
    // Allow you to debounce the POT :D
    if (delta > POT_VARIANCE_LEVEL){
      previousglobalPOTaverage = tempglobalPOTaverage;
      globalPOTaverage = tempglobalPOTaverage;
    }
  }

  // Service any '!' line the I2C ISR deferred to us (see ContractPSI.h). Runs in MAIN
  // context, every pass and outside the 25 ms gate, so the first contract command can still
  // arm the layer. No-op when nothing is pending.
  contractServicePending();

  // Checked every loop pass (not only inside the 25 ms gate) for crisp, sub-tick
  // pulse-accent expiry. No-op unless a contract P overlay is active.
  contractPulseTick();

}

//////////////////////////
// LED Helper Functions //
//////////////////////////

void allON(CRGB color, bool showLED, unsigned long runtime=0)
{
  if (firstTime) {
    DEBUG_PRINT_LN("Turn on all LEDs");
    firstTime = false;
    patternRunning = true;
    if ((runtime != 0) && (!timingReceived)) set_global_timeout(runtime);
    if (timingReceived) set_global_timeout(commandTiming);
  }
  
  fill_solid(leds, NUM_LEDS, color);
  if (showLED) FastLED.show(brightness());

  if ((runtime != 0) || (timingReceived)){
    // Check for the global timeout to have expired.
    globalTimerDonedoRestoreDefault();
  }
}

void allOFF(bool showLED, unsigned long runtime=0)
{
  if (firstTime) {
    DEBUG_PRINT_LN("All Off");
    firstTime = false;
    patternRunning = true;
    DEBUG_PRINT_LN(runtime);
    if ((runtime != 0) && (!timingReceived)) set_global_timeout(runtime);
    if (timingReceived) set_global_timeout(commandTiming);
  }
  
  //DEBUG_PRINT_LN("LED All Off");
  FastLED.clear();
  if (showLED) FastLED.show();

  if ((runtime != 0) || (timingReceived)) {
    // Check for the global timeout to have expired.
    globalTimerDonedoRestoreDefault();
  }
}

void fill_column(uint8_t column, CRGB color, uint8_t scale_brightness=0) {
  for (int i = 0; i < LEDS_PER_COLUMN; i++) {
    int8_t ledIndex = ledMatrix[column][i];
    if (ledIndex != -1) {
      leds[ledIndex] = color;
      if (scale_brightness != 0) leds[ledIndex] %= scale_brightness;
    }
  }
}

// Fills half a column.  0 for top half, 1 for bottom half.
void fill_half_column(uint8_t column, uint8_t half, CRGB color) {
  uint8_t start;
  uint8_t end_col;
  if (!half) {
    start = 0;
    end_col = LEDS_PER_COLUMN / 2;
  }
  else
  {
    start = LEDS_PER_COLUMN / 2;
    end_col = LEDS_PER_COLUMN;
  }

  for (int i = start; i < end_col; i++) {
    int8_t ledIndex = ledMatrix[column][i];
    if (ledIndex != -1) leds[ledIndex] = color;
  }
}

void fill_row(uint8_t row, CRGB color, uint8_t scale_brightness=0) {
  for (int i = 0; i < COLUMNS; i++) {
    int8_t ledIndex = ledMatrix[i][row];
    if (ledIndex != -1) {
      leds[ledIndex] = color;
      if (scale_brightness != 0) leds[ledIndex] %= scale_brightness;
    }
  }
}

// Display a matrix using the byte array.  Colours are defined so that
// if the matrix has a 1, we use fgcolor, and 0 is bgcolor.
// Optionally colors 2,3,4,5,6,7,8 (allowing a total of nine colours to be used in a pattern.
void displayMatrixColor(const byte* matrix, CRGB fgcolor, CRGB bgcolor, bool displayMe, unsigned long runtime,
                        CRGB color2, CRGB color3, CRGB color4, CRGB color5,
                        CRGB color6, CRGB color7, CRGB color8)
{
  // global LED ID counter ...
  int ledNum = 0;
  byte ledOn;

  if (firstTime) {
    firstTime = false;
    patternRunning = true;
    // AllOff will set the timing, so we don't want to re-set it if timing was received for single display patterns!
    // Special case where we don't want to use the global timeout!
    if (timingReceived) {
      set_global_timeout(commandTiming);
      runtime = 0;
    }    
    if (runtime != 0) set_global_timeout(runtime);
  }

  // Palette indexed by the matrix cell value (0..8): 0 = bgcolor, 1 = fgcolor,
  // 2..8 = the optional extra colours. This collapses the six identical per-row
  // colour switches this function used to carry into a single indexed store.
  const CRGB palette[9] = { bgcolor, fgcolor, color2, color3, color4,
                            color5, color6, color7, color8 };

  // The panel is wired boustrophedon: six rows of 6/8/10/10/8/6 LEDs, alternate
  // rows reversed. (start, count, step) walks the PROGMEM matrix in output-LED
  // order -- the exact same visitation the six hand-unrolled loops did (row 1
  // forward from 0, row 2 reverse from 13, row 3 forward from 14, row 4 reverse
  // from 33, row 5 forward from 34, row 6 reverse from 47). pgm_read_BYTE, not
  // dword: the matrix is a byte array, and the old dword reads over-read three
  // bytes past each cell (only the low byte was ever used).
  static const int8_t rowStart[6] = { 0, 13, 14, 33, 34, 47 };
  static const int8_t rowCount[6] = { 6,  8, 10, 10,  8,  6 };
  static const int8_t rowStep[6]  = { 1, -1,  1, -1,  1, -1 };
  for (uint8_t r = 0; r < 6; r++) {
    int8_t idx = rowStart[r];
    for (int8_t k = 0; k < rowCount[r]; k++) {
      ledOn = pgm_read_byte(&matrix[idx]);
      if (ledOn < 9) leds[ledNum] = palette[ledOn];  // cells are 0..8; others leave the LED as-is, as before
      ledNum++;
      idx += rowStep[r];
    }
  }

  if (displayMe) FastLED.show(brightness());

  if (runtime != 0){
    globalTimerDonedoRestoreDefault();
  }
  else if (timingReceived) globalTimerDonedoRestoreDefault();
}

//////////////////////////////
// END LED Helper Functions //
//////////////////////////////

// i_heart_u/red_heart/march (modes 7/9-front/11) were here — replaced by
// VMP_IHEARTU/VMP_REDHEART/VMP_MARCH bytecode programs (include/psi_vm.h).

void swipe() {

  // We set this to false as we're not running a pattern
  // Yes, I know the swipe is a pattern, but it's the default pattern
  // Just work with me here people!
  patternRunning = false;

  if (millis() >= nextEvent) {
    switch (ledState) {
      case Primary: {
          ledState = PrimaryToSecondary;
          swipeDelay = random(SWIPE_DELAY_MINIMUM, SWIPE_DELAY_MAXIMUM);
          DEBUG_PRINT_LN("Switching to secondary color");

          int totalChance = CHANCE_SECONDARY_FULL + CHANCE_SECONDARY_PARTIAL + CHANCE_SECONDARY_PARTIAL_OFF;
          int selection = random(totalChance);
          if (selection < CHANCE_SECONDARY_FULL) {
            DEBUG_PRINT_LN("Selected full color");
            for (int i = 0; i < COLUMNS; i++) {
              overlayColors[i] = secondary_color();
            }
          } else if (selection < CHANCE_SECONDARY_FULL + CHANCE_SECONDARY_PARTIAL) {
            DEBUG_PRINT_LN("Selected partial secondary, with rest primary");
            int totalColumnsLit = random(SECONDARY_PARTIAL_LINES_MIN, SECONDARY_PARTIAL_LINES_MAX);
            for (int i = 0; i < COLUMNS; i++) {
              overlayColors[i] = i > COLUMNS - totalColumnsLit - 1 ? secondary_color() : primary_color();
            }
          } else {
            DEBUG_PRINT_LN("Selected partial secondary, rest off");
            for (int i = 0; i < COLUMNS; i++) {
              overlayColors[i] = i > COLUMNS - SECONDARY_PARTIAL_OFF_LINES - 1 ? secondary_color() : secondary_off_color();
            }
          }
          // Intentional fall through
          FALL_THROUGH()
        }
      case PrimaryToSecondary:
        visibleSecondaryColumns++;
        if (visibleSecondaryColumns >= COLUMNS) {
          DEBUG_PRINT_LN("On secondary color");
          ledState = Secondary;
          nextEvent = millis() + random(SECONDARY_COLOR_DURATION_MINIMUM, SECONDARY_COLOR_DURATION_MAXIMUM);
        } else {
          nextEvent = millis() + swipeDelay;
        }
        break;
      case Secondary: {
          ledState = SecondaryToPrimary;
          swipeDelay = random(SWIPE_DELAY_MINIMUM, SWIPE_DELAY_MAXIMUM);
          DEBUG_PRINT_LN("Switching to primary color");
          // Intentional fall through
          FALL_THROUGH()
        }
      case SecondaryToPrimary:
        visibleSecondaryColumns--;
        if (visibleSecondaryColumns == 0) {
          DEBUG_PRINT_LN("On primary color");
          ledState = Primary;
          nextEvent = millis() + random(PRIMARY_COLOR_DURATION_MINIMUM, PRIMARY_COLOR_DURATION_MAXIMUM);
        } else {
          nextEvent = millis() + swipeDelay;
        }
        break;
    }

    updateLed = 1;
  }

  if (updateLed || ((millis() - lastLedUpdate) > 100)) {
    lastLedUpdate = millis();

    CRGB primaryColor = primary_color();
    uint8_t switchPoint = COLUMNS - visibleSecondaryColumns;

    for (int i = 0; i < COLUMNS; i++) {
      CRGB columnColor = primaryColor;
      if (i >= switchPoint) {
        columnColor = overlayColors[i - switchPoint];
      }
      fill_column(i, columnColor);
    }

    FastLED.show(brightness());
  }
}

// Delay, loops, color
void VUMeter(unsigned long time_delay, uint8_t loops, unsigned long runtime)
{
  // We use the VU_chart matrix to define the colors used
  // Then we'll simply display as many or as few of the pixels as we want
  // and use a random number to determine rise and fall.

  if (firstTime) {
    DEBUG_PRINT_LN("VU Meter");
    firstTime = false;
    patternRunning = true;
    if (loops != 0) globalPatternLoops = loops;
    else globalPatternLoops = 2;
    if ((runtime != 0) && (!timingReceived)) set_global_timeout(runtime);
    if (timingReceived) set_global_timeout(commandTiming);
    // Clear the display the first time through
    allOFF(true);

    // Set a default start level for each column.
    // Contract verb L drives level[] from Studio energy, so skip the random seed
    // while the contract is armed (keeps the meter tracking L, not noise).
    if (!g_contractArmed) {
      for (int i=0; i< COLUMNS; i++)
      {
        level[i] = random(0, 6);
      }
    }
  }

  updateLed = 0;

  if (checkDelay()) {
    // read the display, but don't show it.
    // we'll blank out some pixels in a sec
    displayMatrixColor(VUChart, 0x008000, 0x000000, false, 0, 0xffd700, 0xff8c00, 0xff0000);

    // Now go through each column, and turn off the unused pixels
    for (int c = 0; c < COLUMNS; c++)
    {
      for (int i = 0; i < LEDS_PER_COLUMN; i++) {
        int8_t ledIndex = ledMatrix[c][i];
        if (ledIndex != -1) {
          if (level[c] > i) leds[ledIndex] = CRGB::Black;
        }
      }
    }
    
    updateLed = 1;

    // calc the next position of the bars.
    // Suppressed while the contract is armed: verb L owns level[] (Studio energy).
    if (!g_contractArmed)
    for (int y = 0; y < COLUMNS; y++)
    {
      byte upDown = random(0, 2);
      byte changeSize = random(1, 3);
      // go up
      if (upDown == 1)
      {
        ((level[y] + changeSize) <= 6) ? level[y] += changeSize : level[y] = 6;
      }
      // go down
      else
      {
        ((level[y] - changeSize) >= 0) ? level[y] -= changeSize : level[y] = 0;
      }
    }

    globalPatternLoops--;
  }


  if (updateLed) {
    FastLED.show(brightness());
    set_delay(time_delay);
  }

  if ((runtime == 0) && (!timingReceived)){
    if (loops) {
      // Check to see if we have run the loops needed for this pattern
      loopsDonedoRestoreDefault();
    }
  } else {
    // Check for the global timeout to have expired.
    globalTimerDonedoRestoreDefault();
  } 
}

// lightsaberBattle/StarWarsIntro (modes 19/20) were here — replaced by
// VMP_SABER/VMP_SWINTRO bytecode programs (include/psi_vm.h).

///////////////////
// OTHER HELPERS //
///////////////////

//This is the non-blocking delay function
// When called it sets some global variables to allow checking of timer exipration
// To check if the timer has expired, call checkDelay()
void set_delay(unsigned long timeout)
{
  doNext = millis() + timeout;
  //DEBUG_PRINT("Set delay to "); DEBUG_PRINT_LN(doNext);
}

// Call this to see if the timer for set_delay() has expired
bool checkDelay()
{
  bool timerExpired = false;
  if (millis() >= doNext) timerExpired = true;
  return timerExpired;
}

//set the global pattern timeout
void set_global_timeout(unsigned long timeout)
{
// use 256 to set as "always on"
// 256 sec == ~4 mins. To make the pattern run longer, square the value
// resulting in ~18 hours
  if (timeout == 256) timeout *= timeout;
  globalTimeout = millis() + (timeout * 1000);
  DEBUG_PRINT("Current time "); DEBUG_PRINT_LN(millis());
  DEBUG_PRINT("Timeout received "); DEBUG_PRINT_LN(timeout);
  DEBUG_PRINT("End time Timeout "); DEBUG_PRINT_LN(globalTimeout);
}

// Check if the global timeout has expired.
// This will return true if the timer has expired.
// If "alwaysOn" is set, the function will never return true.
bool globalTimeoutExpired()
{
  bool timerExpired = false;
  if ((millis() >= globalTimeout) && (!alwaysOn)){ 
    timerExpired = true;
    DEBUG_PRINT("Global Timer Expired at  "); DEBUG_PRINT_LN(millis());
  }
  return timerExpired;
}

void loopsDonedoRestoreDefault()
{
  // Check to see if we have run the loops needed for this pattern
  if ((globalPatternLoops == 0) && (!alwaysOn))
  {
    // Set back to the default pattern
    lastPSIeventCode = defaultPattern;
    patternRunning = false;
  }
}

void globalTimerDonedoRestoreDefault()
{
  if (globalTimeoutExpired()) {
    // Set the loops to 0 to catch any cases like that.
    globalPatternLoops = 0;
    // Global timeout expired, go back to default mode.
    lastPSIeventCode = defaultPattern;
    patternRunning = false;
  }
}

// The following takes the Pattern code, and executes the relevant function
// This allows i2c and serial inputs to use the same function to start patterns
// so we avoid the need to duplicate this code.
void runPattern(int pattern) {

  // Used to restore state if an invalid pattern code is received.
  int currentPattern = lastPSIeventCode;
  
  if (lastPSIeventCode != pattern)
  {
    lastPSIeventCode = pattern;
    firstTime = true;
  }
  else
  {
    firstTime = false;
  }

  switch (pattern) {
    case 0:              //  0 = Turns Panel Off
      allOFF(true);
      break;
    case 1:              //  1 = Default Swipe Pattern
      swipe();
      break;
    case 2:              //  2 = Flash Panel (4s) — was flash(0xffffff, 60, 24, 4)
      vmPlay(VMP_FLASH);
      break;
    case 3:              //  3 = Alarm (4s) — was flash(0xffffff, 125, 15, 4)
      vmPlay(VMP_ALARM);
      break;
    case 4:              //  4 = Short circuit — was FadeOut(257, 3)
      vmPlay(VMP_FADEOUT);
      break;
    case 5:              //  5 = Scream — same as Alarm
      vmPlay(VMP_ALARM);
      break;
    case 6:              //  6 = Leia message (34s) — was Cylon_Row(0xcccccc, 74, 3, 57, 34)
      vmPlay(VMP_LEIA);
      break;
    case 7:              //  7 = I heart U — was i_heart_u(500, 3, 0)
      vmPlay(VMP_IHEARTU);
      break;
    case 8:              //  8 = Radar sweep — was radar(0xff0000, 250, 6, 0)
      vmPlay(VMP_RADAR);
      break;
    case 9:              //   = Flashing red heart (front) / Pulse trace (rear)
      vmPlay(digitalRead(JUMP_FRONT_REAR) ? VMP_REDHEART : VMP_PULSE);
      break;
    case 10:              //  10 = Star Wars Animation — was Cylon_Row(0xC8AA00, 500, 4, 5, 0)
      vmPlay(VMP_SWSCAN);
      break;
    case 11:              //  11 = Imperial March (47s) — was march(0xffffff, 552, 42, 47)
      vmPlay(VMP_MARCH);
      break;
    case 12:          // 13 - Disco Ball - 4 seconds — was DiscoBall(150, 30, 3, CRGB::Grey, 4)
      vmPlay(VMP_DISCO4);
      break;
    case 13:          // 13 - Disco Ball — was DiscoBall(150, 0, 3, CRGB::Grey, 0)
      // Time Delay, loops, sparkles, colour.  If loops is 0, this is on indefinately.
      vmPlay(VMP_DISCOINF);
      break;
    case 14:          // 14 - Rebel Symbol — was displayMatrixColor(rebel, 0xff0000, 0x909497, true, 5)
      vmPlay(VMP_REBEL);
      break;
    case 15:        // 15 - Knight Rider — was Cylon_Col(0xff0000, 250, 1, 5, 0)
      vmPlay(VMP_KNIGHT);
      break;
    case 16:        // All LED's On White Indefinitely
      allON(CRGB::Grey, true);
      break;
    case 17:              //  17 - Turns Panel On Red Indefinitely
      allON(CRGB::Red, true);
      break;
    case 18:              //  18 - Turns Panel On Green Indefinitely
      allON(CRGB::Green, true);
      break;
    case 19:              //  19 - Complex animation test, Lightsaber Battle — was lightsaberBattle(250)
      vmPlay(VMP_SABER);
      break;
    case 20:             // 20 - Star Wars Intro Text (10 seconds) — was StarWarsIntro(500, 4, 0xC8AA00, 10)
      vmPlay(VMP_SWINTRO);
      break;
    case 21:          // 12 - VU Meter (4 seconds).
      // Set loops to 0 to remain on indefinately.
      VUMeter(250, 20, 4);
      break;
    case 92:          // 12 - VU Meter (On Indefinately).
      // Set loops to 0 to remain on indefinately.
      VUMeter(250, 0, 0);
      break;
    case 22:          // 22 - Processing sweep (fork addition — not an upstream mode; the
                       // marginal-cost demo, see vmc_process's comment in include/psi_vm.h)
      vmPlay(VMP_PROCESS);
      break;
    default:
      // Reset back to the state before calling this function
      DEBUG_PRINT("Pattern "); DEBUG_PRINT(pattern); DEBUG_PRINT_LN(" not valid.  Ignoring");
      lastPSIeventCode = currentPattern;
      firstTime = false;
      break;
  }
}

#if USE_I2C
// function that executes whenever data is received from an I2C master
// this function is registered as an event, see setup()
// TWI INTERRUPT CONTEXT. Compiled out entirely unless PSI_ENABLE_I2C is defined, which is
// what removes the TWI vector — and with it the last interrupt that can reach a render.
#ifdef PSI_ENABLE_I2C
void receiveEvent(int eventCode) {

  while (Wire.available()) {

    // New I2C handling
    // Needs to be tested, but uses the same parser as Serial!
    bool command_available;
    char ch = (char)Wire.read();

    DEBUG_PRINT("I2C Character received "); DEBUG_PRINT_LN(ch);
    
    command_available=buildCommand(ch, cmdString);  // build command line

    if (command_available)
    {
      // ISR CONTEXT. Do not parse here, and above all do not render here: FastLED's show()
      // re-enables interrupts mid-render (an unconditional sei) while the I2C slave is already
      // re-armed, so this ISR can re-enter itself and walk the stack down into .bss. Copy the
      // line out and let loop() do the work. See the I2C DEFERRAL note in ContractPSI.h.
      if (cmdString[0]=='!') contractQueueFromISR(cmdString);  // additive contract branch
      else                   parseCommand(cmdString);   // unchanged JawaLite path
    }
  }
}
#endif  // PSI_ENABLE_I2C
#endif

/*
   SerialEvent occurs whenever a new data comes in the
  hardware serial RX.  This routine is run between each
  time loop() runs, so using delay inside loop can delay
  response.  Multiple bytes of data may be available.
*/
void serialEventRun(void)
{
  if (serialPort->available()) serialEvent();
}

void serialEvent() {

   DEBUG_PRINT_LN("Serial In");
   bool command_available;

  while (serialPort->available()) {  
    char ch = (char)serialPort->read();  // get the new byte

    // New improved command handling
    command_available=buildCommand(ch, cmdString);  // build command line
    if (command_available)
    {
      if (cmdString[0]=='!') parseContract(cmdString);  // additive contract branch
      else                   parseCommand(cmdString);   // unchanged JawaLite path
    }
  }
  sei();
}


////////////////////////////////////////////////////////
// Command language - JawaLite emulation
///////////////////////////////////////////////////////


////////////////////////////////
// command line builder, makes a valid command line from the input
byte buildCommand(char ch, char* output_str)
{
  static uint8_t pos=0;
  switch(ch)
 {
    case '\r':                          // end character recognized
      if(pos > CMD_MAX_LENGTH-1) pos = CMD_MAX_LENGTH-1;  // clamp: never write the '\0' past the buffer
      output_str[pos]='\0';   // append the end of string character
      pos=0;        // reset buffer pointer
      return true;      // return and signal command ready
      break;
    default:        // regular character
      // Bounds-check BEFORE writing (fixes the pre-existing off-by-one OOB write).
      // Reserve the last index for the '\0'; drop overflow bytes so a >=64-char line
      // never corrupts the SRAM after cmdString nor wraps into the next command.
      if(pos < CMD_MAX_LENGTH-1) { output_str[pos]=ch; pos++; }
      break;
  }
  return false;
}

///////////////////////////////////
// command parser and switcher, 
// breaks command line in pieces, 
// rejects invalid ones, 
// switches to the right command
void parseCommand(char* inputStr)
{
  byte hasArgument=false;
  byte hasTiming=false;
  int argument;
  int address;
  int timing;
  byte pos=0;
  byte endArg=0;
  byte length=strlen(inputStr);
  if(length<2) goto beep;   // not enough characters

  DEBUG_PRINT(" Here's the input string: ");
  DEBUG_PRINT_LN(inputStr);
  
  // get the adress, one or two digits
  char addrStr[3];
  if(!isdigit(inputStr[pos])) goto beep;  // invalid, first char not a digit
    addrStr[pos]=inputStr[pos];
    pos++;                            // pos=1
  if(isdigit(inputStr[pos]))          // add second digit address if it's there
  {  
    addrStr[pos]=inputStr[pos];
    pos++;                            // pos=2
  }
  addrStr[pos]='\0';                  // add null terminator
  
  address= atoi(addrStr);        // extract the address

  //DEBUG_PRINT(" I think this is the address! ");
  //DEBUG_PRINT_LN(address);
  
  // check for more
  if(length<=pos) goto beep;            // invalid, no command after address
  
  // special case of M commands, which take a string argument
  // Not currently implemented!!!!!
  //if(inputStr[pos]=='M')
  //{
  //  pos++;
  //  if(!length>pos) goto beep;     // no message argument
  //  doMcommand(address, inputStr+pos);   // pass rest of string as argument
  //  return;                     // exit
  //}
  
  // other commands, get the numerical argument after the command character

  pos++;                             // need to increment in order to peek ahead of command char
  if(length<=pos) {hasArgument=false; hasTiming=false;}// end of string reached, no arguments
  else
  {
    for(byte i=pos; i<length; i++)
    {
      if (inputStr[i] == '|')
      {
        //we have a timing parameter for the T command.
        hasTiming = true;
        endArg = i;
        break;
      }
      if(!isdigit(inputStr[i])) goto beep; // invalid, end of string contains non-numerial arguments
    } 
    argument=atoi(inputStr+pos);    // that's the numerical argument after the command character
    hasArgument=true;
    
    if (hasTiming){
      timing=atoi(inputStr+endArg+1);
    }
    else {
      timing = 0;
    }
    /*
    DEBUG_PRINT(" I think this is the address! ");
    DEBUG_PRINT_LN(address);
    DEBUG_PRINT(" I think this is the Command! ");
    DEBUG_PRINT_LN(inputStr[pos-1]);
    DEBUG_PRINT(" I think this is the Command Value! ");
    DEBUG_PRINT_LN(argument);
    if (hasTiming){
      DEBUG_PRINT(" I think this is the Timing Value! ");
      DEBUG_PRINT_LN(timing);
    }
    */
  }
  
  // switch on command character
  switch(inputStr[pos-1])               // 2nd or third char, should be the command char
  {
    case 'T':
      if(!hasArgument) goto beep;       // invalid, no argument after command
      doTcommand(address, argument, timing);      
      break;
    case 'D':                           // D command is weird, does not need an argument, ignore if it has one
    case 'A':                           // A command does the same as D command, so just fall though.
      doDcommand(address);
      break;
    case 'P':    
      if(!hasArgument) goto beep;       // invalid, no argument after command
      doPcommand(address, argument);
      break;
    //case 'R':    
    //  if(!hasArgument) goto beep;       // invalid, no argument after command
    //  doRcommand(address, argument);
    //  break;
    //case 'S':    
    //  if(!hasArgument) goto beep;       // invalid, no argument after command
    //  doScommand(address, argument);
    //  break;
    default:
      goto beep;                        // unknown command
      break;
  }
  
  return;                               // normal exit
  
  beep:                                 // error exit
    // Dont know what this does ... idnoring it for now!
    //serialPort->write(0x7);             // beep the terminal, if connected
    return;
}

////////////////////
// Command Executors

// various commands for states and effects
void doTcommand(int address, int argument, int timing)
{
  /*
  DEBUG_PRINT_LN();
  DEBUG_PRINT("Command: T ");
  DEBUG_PRINT("Address: ");
  DEBUG_PRINT(address);
  DEBUG_PRINT(" Argument: ");
  DEBUG_PRINT_LN(argument);
  if (timing){
    DEBUG_PRINT(" Timing: ");
    DEBUG_PRINT_LN(timing); 
  }
  */

  // If the command is not directed at a PSI, then we should just return and do nothing.
  // This prevents overriding any timing parameters that may be in use, for an invalid
  // command.
  if (!((address == 0) || 
      ((digitalRead(JUMP_FRONT_REAR)) && (address == 4)) || 
      ((!digitalRead(JUMP_FRONT_REAR)) && (address == 5))))
  {
    DEBUG_PRINT("Address "); DEBUG_PRINT(address); DEBUG_PRINT_LN(" not a valid PSI.  Ignoring");
    return;
  }

  if (timing != 0){
    DEBUG_PRINT_LN("Timing Value received in command");
    timingReceived = true;
    commandTiming = timing;
  }
  else {
    DEBUG_PRINT_LN("Disable Global Timing");
    timingReceived = false;
    commandTiming = 0;
  }
  
  // If we are the front PSI, respond to 0 or 4
  if ((digitalRead(JUMP_FRONT_REAR)) && (address == 4))
  {
    runPattern(argument);
  }
  // If we are the rear PSI, respond to 0 or 5
  else if ((!digitalRead(JUMP_FRONT_REAR)) && (address == 5))
  {
    runPattern(argument);
  }
  else if (address == 0) runPattern(argument);
  else
  {
    DEBUG_PRINT("Address "); DEBUG_PRINT(address); DEBUG_PRINT_LN(" not recognised");
  }
  
}

void doDcommand(int address)
{
  // Ignore the argument
  UNUSED(address)
  /*
  DEBUG_PRINT_LN();
  DEBUG_PRINT("Command: D ");
  DEBUG_PRINT("Address: ");
  DEBUG_PRINT_LN(address); 
  */

  runPattern(defaultPattern);
}

// Parameter handling for PSI settings
void doPcommand(int address, int argument)
{
  /*
  DEBUG_PRINT_LN();
  DEBUG_PRINT("Command: P ");
  DEBUG_PRINT("Address: ");
  DEBUG_PRINT(address);
  DEBUG_PRINT(" Argument: ");
  DEBUG_PRINT_LN(argument);  
  */
  switch(address)
  {
    case 0:
      // Set the always on Mode
      if (argument == 0) {
        alwaysOn = false;
        EEPROM.write(alwaysOnAddress, 0);
        DEBUG_PRINT_LN("Disable always on mode ");
      }
      if (argument == 1) {
        alwaysOn = true;
        EEPROM.write(alwaysOnAddress, 1);
        DEBUG_PRINT_LN("Enable always on mode ");
      }
      break;
    case 1:
      // Use either the external POT or internal brightness value
      if (argument == 0){
        // Set the brightness using the POT
        internalBrightness = false;
        EEPROM.write(externalPOTAddress, 0);
        DEBUG_PRINT_LN("Use External POT ");
      }
      if (argument == 1){
        // Set the brightness using the internal value.
        internalBrightness = true;
        EEPROM.write(externalPOTAddress, 1);
        DEBUG_PRINT_LN("Use internal Brightness setting ");
      }
      break;
    case 2:
      //// Brightness Control ////
      //
      // This PSI CAN DRAW MORE POWER THAN YOUR USB PORT CAN SUPPLY!!
      // When using the USB connection on the Pro Micro to power the PSI (during programming
      // for instance) be sure to have the brightness POT turned nearly all the way COUNTERCLOCKWISE.  
      // Having the POT turned up too far when plugged into USB can damage the Pro Micro
      // and/or your computer's USB port!!!!
      // If you are connected to USB, KEEP THIS VALUE LOW, not higher than 20.
      // Be aware that if you change the PSI setting to use the internal brightness value, set this back
      // to 20 prior to plugging the PSI into your USB port!
      // The Pro Micro can also be removed from the PSI and programmed separately. 
      
      if (argument > 200) globalBrightnessValue = 200;
      else globalBrightnessValue = argument;
      EEPROM.write(internalBrightnessAddress, globalBrightnessValue);
      
      DEBUG_PRINT("Setting brightness to: ");
      DEBUG_PRINT_LN(globalBrightnessValue);
      break;
    case 3:
      //// Brightness Control ////
      //
      // This PSI CAN DRAW MORE POWER THAN YOUR USB PORT CAN SUPPLY!!
      // When using the USB connection on the Pro Micro to power the PSI (during programming
      // for instance) be sure to have the brightness POT turned nearly all the way COUNTERCLOCKWISE.  
      // Having the POT turned up too far when plugged into USB can damage the Pro Micro
      // and/or your computer's USB port!!!!
      // If you are connected to USB, KEEP THIS VALUE LOW, not higher than 20.
      // Be aware that if you change the PSI setting to use the internal brightness value, set this back
      // to 20 prior to plugging the PSI into your USB port!
      // The Pro Micro can also be removed from the PSI and programmed separately. 

      if (argument == 0){
        useTempInternalBrightness = false;
        DEBUG_PRINT("Restoring previous brightness values.");
      }
      else {
        useTempInternalBrightness = true;
        if (argument > 200) tempGlobalBrightnessValue = 200;
        else tempGlobalBrightnessValue = argument;
      }
      
      break;
    default:
      break;
  }  
}

// Brightness control
// This is where we'll read from the pot, etc
uint8_t brightness() {
  //LED brightness is capped at 200 (out of 255) to reduce heat and extend life of LEDs. 
  if (useTempInternalBrightness) return tempGlobalBrightnessValue;
  else if (internalBrightness) return globalBrightnessValue;
  else return globalPOTaverage;
}

// Firmware Routine to average the value received from the POT so that the external resistor isn't needed
// Early HW Control boards did not have a resistor across the POT.
// WARNING - DO NOT PUT DEBUG OUTPUT IN THIS FUNCTION, YOU WILL CRASH THE BOARDS!
uint8_t averagePOT() {
  
  // Calculate the Rolling Sum
  POTSum -= POTReadings[POTIndex];
  POTReadings[POTIndex] = map(analogRead(POT_BRIGHT_PIN), 0, 1024, 0, 200);
  POTSum += POTReadings[POTIndex];

  // Adjust the index so we maintain a circular buffer.
  POTIndex++;
  POTIndex = POTIndex % POT_AVG_SIZE;

  return POTSum / POT_AVG_SIZE;
}
