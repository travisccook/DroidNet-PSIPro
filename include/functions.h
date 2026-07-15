#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <FastLED.h>

// Function prototypes
uint8_t brightness();
uint8_t averagePOT();
void set_global_timeout(unsigned long timeout);
bool globalTimeoutExpired();
void globalTimerDonedoRestoreDefault();
bool checkDelay();
void set_delay(unsigned long timeout);
void loopsDonedoRestoreDefault();
void runPattern(int pattern);

// Render primitives called by the contract fork layer (src/contract/ContractPSI.h),
// which is included before their definitions in main.cpp. Signatures match main.cpp.
void allON(CRGB color, bool showLED, unsigned long runtime);
void allOFF(bool showLED, unsigned long runtime);
void VUMeter(unsigned long time_delay, uint8_t loops, unsigned long runtime);

// Command-related function prototypes
byte buildCommand(char ch, char* output_str);
void parseCommand(char* inputStr);
void doTcommand(int address, int argument, int timing);
void doDcommand(int address);
void doPcommand(int address, int argument);
void serialEvent();
void receiveEvent(int eventCode);

#endif // FUNCTIONS_H 