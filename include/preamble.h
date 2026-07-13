#ifndef PREAMBLE_H
#define PREAMBLE_H

// This header is designed to be included at the top of main.cpp
// to ensure all function prototypes are available

// External helper function prototypes
#include "functions.h"

// Define any constants or macros needed for compilation
// DroidNet fork: was 32 here and 64 in config.h — two different values for the SAME macro,
// working only because config.h is included second and its definition wins by redefinition.
// Both are now 96 (identical bodies, so no redefinition diff): a v1.2 scored contract line
// carrying an accent runs to 64+ chars and buildCommand() SILENTLY TRUNCATES anything past
// CMD_MAX_LENGTH-1. Keep this in lockstep with config.h (guarded by test/host/run.sh).
#define CMD_MAX_LENGTH 96
#define FALL_THROUGH() do {} while (0)
#define UNUSED(x) (void)(x)
#define DEBUG_PRINT(x)
#define DEBUG_PRINT_LN(x)

#endif // PREAMBLE_H 