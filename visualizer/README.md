# PSI Pro Visualizer

A web-based LED matrix visualizer for the PSI Pro astromech droid control system. This tool allows you to preview and test PSI (Process State Indicator) animations without requiring the physical hardware.

## Overview

The PSI Pro Visualizer simulates the dual 10x6 LED matrices used in R2-D2 and other astromech droids. It provides:

- **Dual Display**: Front and Rear PSI displays with accurate color mapping
- **22 Hardware Animations**: All animations implemented in the actual PSI Pro firmware
- **25 Concept Animations**: Additional experimental animations for testing
- **Real-time Preview**: See exactly how animations will look on the hardware
- **No Server Required**: Runs entirely in the browser using bundled ES6 modules

## Features

### Implemented Hardware Animations (Modes 0-21, 92)

These animations match the actual PSI Pro firmware:

1. **Off** (Mode 0) - All LEDs off
2. **Swipe** (Mode 1) - Default color swipe pattern
3. **Flash** (Mode 2) - Fast flash sequence
4. **Alarm** (Mode 3) - Slow flash alarm pattern
5. **Short Circuit** (Mode 4) - Random fade/flicker effect
6. **Scream** (Mode 5) - Same as alarm
7. **Leia Message** (Mode 6) - Scrolling pattern
8. **I Heart U** (Mode 7) - Heart with "I ❤ U" text
9. **Radar** (Mode 8) - Quarter panel sweep
10. **Heart/Pulse** (Mode 9) - Heart on front, pulse on rear
11. **Star Wars Scroll** (Mode 10) - Yellow scrolling effect
12. **Imperial March** (Mode 11) - Synchronized to music
13. **Disco Ball** (Mode 12) - 4-second disco effect
14. **Disco Forever** (Mode 13) - Continuous disco
15. **Rebel Symbol** (Mode 14) - Alliance logo
16. **Knight Rider** (Mode 15) - KITT scanner effect
17. **Test White** (Mode 16) - All white LEDs
18. **Red On** (Mode 17) - All red LEDs
19. **Green On** (Mode 18) - All green LEDs
20. **Lightsaber Battle** (Mode 19) - Animated duel
21. **Star Wars Intro** (Mode 20) - Opening crawl effect
22. **VU Meter** (Mode 21/92) - Audio spectrum display

### Color Mapping

The visualizer accurately simulates the PSI Pro color mapping:

**Front PSI:**
- Primary: Blue
- Secondary: Red

**Rear PSI:**
- Blue → Green (color conversion)
- Red → Yellow (color conversion)
- White → White
- Other colors map accordingly

## Installation

1. Clone or download this repository
2. Install dependencies:
   ```bash
   npm install
   ```
3. Build the project:
   ```bash
   npm run build
   ```
4. Open `dist/index.html` in your web browser

## Usage

### Running Animations

1. Open `dist/index.html` in any modern web browser
2. Click any animation button to start
3. Click "Stop Animation" to halt the current animation
4. The display shows both Front and Rear PSI matrices

### Development

To modify or add animations:

1. Create a new file in `src/animations/`
2. Follow the animation module format:
   ```javascript
   export default {
       name: 'animation-name',
       displayName: 'Display Name',
       description: 'Description',
       category: 'core|ideas',  // 'core' for hardware, 'ideas' for concepts
       interval: 100,           // milliseconds between frames
       
       init() {
           return { /* initial state */ };
       },
       
       frame(controller, state) {
           // Animation logic
           controller.setLED(index, color);
       }
   };
   ```
3. Rebuild: `npm run build`

### Testing

- Run automated tests: Open `test/test-runner.html`
- Check feature parity: Open `verify-parity.html`

## Project Structure

```
visualizer/
├── src/                    # Source code
│   ├── animations/         # Animation modules
│   ├── core/              # Core LED controller
│   ├── app.js             # Main application
│   ├── animation-registry.js
│   ├── index.html
│   └── styles.css
├── dist/                   # Built application
├── test/                   # Test suite
├── build.js               # Build configuration
├── package.json
└── README.md
```

## Reference Files

The following files are kept for historical reference:
- `PSIvisualizer.html` - Original monolithic implementation
- `PSIvisualizer_backup.html` - Backup of original
- `PSIvisualizer_single.html` - Single PSI version

## Technical Details

- **Build System**: esbuild for fast ES6 module bundling
- **LED Matrix**: 10 columns × 6 rows = 48 LEDs per PSI
- **Frame Rate**: Animation-specific, typically 30-250ms intervals
- **Browser Support**: Any modern browser supporting ES6 modules

## Contributing

When adding new animations:
1. Verify they work on both Front and Rear displays
2. Test color mapping (especially blue→green, red→yellow)
3. Keep frame intervals reasonable (30ms minimum)
4. Document any special requirements

## License

Part of the C2B5 astromech control system project.

## Credits

- Original PSI Pro hardware and firmware by Maxstang
- Visualizer adaptation for testing and development
- Based on the MaxPSI sketch for PSI PRO Connected