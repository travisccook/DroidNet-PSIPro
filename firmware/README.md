# Prebuilt firmware — DroidNet PSI Pro fork

Ready-to-flash images built from this repo at commit `fd352e7`, for flashing through the
DroidNet **Flash** tab. Details (sha256, sizes) are in [`manifest.json`](manifest.json).

> **NEVER FLASHED TO HARDWARE.** Compiles cleanly and verified against DroidNet's flasher
> logic only. Bench-test before field use.

Target: SparkFun Pro Micro — **ATmega32U4 @ 16 MHz**.

| File | Variant |
| ---- | ------- |
| `DroidNet-PSIPro-atmega32u4-serial-fd352e7.hex` | Serial-only (default, recommended) |
| `DroidNet-PSIPro-atmega32u4-i2c-fd352e7.hex` | I2C intake enabled (opt-in) |

- **Serial-only** is the default. The upstream I2C intake is disabled because the TWI
  interrupt renders from interrupt context and, with stock FastLED, could re-enter itself.
- **I2C** restores upstream I2C/JawaLite intake with an interrupt-safe (SREG-masked) render.
  Flash it only if you drive the PSI over I2C.

## Flashing via DroidNet

Upload the `.hex`. DroidNet reads the ATmega32U4 signature, performs the 1200-baud
Pro-Micro bootloader reset automatically, and flashes with `avrdude` (avr109 @ 57600).
No manual board selection needed.

## Rebuild

```bash
~/.platformio/penv/bin/pio run -e PSIPro        # serial-only
~/.platformio/penv/bin/pio run -e PSIPro-i2c    # + I2C
```
