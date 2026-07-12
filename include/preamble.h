#ifndef PREAMBLE_H
#define PREAMBLE_H

// This header is designed to be included at the top of main.cpp
// to ensure all function prototypes are available

// External helper function prototypes
#include "functions.h"

// Define any constants or macros needed for compilation
#define CMD_MAX_LENGTH 32
#define FALL_THROUGH() do {} while (0)
#define UNUSED(x) (void)(x)
#define DEBUG_PRINT(x)
#define DEBUG_PRINT_LN(x)

#endif // PREAMBLE_H 