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

// Command-related function prototypes
byte buildCommand(char ch, char* output_str);
void parseCommand(char* inputStr);
void doTcommand(int address, int argument, int timing);
void doDcommand(int address);
void doPcommand(int address, int argument);
void serialEvent();
void receiveEvent(int eventCode);

#endif // FUNCTIONS_H 