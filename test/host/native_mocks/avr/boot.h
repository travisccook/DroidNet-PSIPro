// Host mock of avr-libc's <avr/boot.h>: only boot_signature_byte_get(), which the WDP announce
// seed (src/wdp/wdp_fork.h) uses to read the ATmega32U4's factory serial number (signature row
// 0x0E-0x17). Golden-frame harness support — test instrument, not production code.
#pragma once
#include <stdint.h>

// The signature row a test wants the chip to have. 0x1E 0x95 0x87 is the ATmega32U4's device
// signature (avr-libc iom32u4.h SIGNATURE_0..2); the serial bytes are made up.
inline uint8_t g_mock_sigrow[0x20] = {
  0x1E, 0x00, 0x95, 0x00, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x58, 0x37, 0x33, 0x34, 0x31, 0x30, 0x0B, 0x16, 0x09, 0x21,
};
#define boot_signature_byte_get(addr) (g_mock_sigrow[(addr) & 0x1F])
