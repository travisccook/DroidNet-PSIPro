// Feature parity check script
// This verifies that all animations from the original PSIvisualizer.html are implemented

const originalAnimations = [
    'off',
    'test',
    'testWhite',
    'greenOn',
    'redOn',
    'disco',
    'discoForever',
    'swipe',
    'flash',
    'alarm',
    'march',
    'marchTwo',
    'solidRed',
    'solidYellow',
    'solidGreen',
    'solidCyan',
    'solidBlue',
    'solidPurple',
    'solidWhite',
    'psiColorWipe1',
    'psiColorWipe2',
    'psiColorWipeRainbow',
    'marchImperial',
    'clawMachineFlicker',
    'clawMachineDisplay',
    'cylon',
    'knightRider',
    'theaterChase',
    'warpSpeed',
    'starWarsScroll',
    'starField',
    'rebel',
    'leia',
    'loadingBar',
    'wifiBreathing',
    'rippleEffect',
    'heart',
    'randomBars',
    'shortCircuit',
    'rainbow',
    'randomFlash',
    'randomColors',
    'i_heart_u',
    'plasma',
    'binaryCounter',
    'dnaHelix',
    'communicationStatic',
    'morseSOS',
    'clockDisplay',
    'scream',
    'rainbowSweep',
    'glitchMatrix',
    'heartbeat',
    'lightsaber',
    'starWarsIntro',
    'vuMeter',
    'vuMeterForever',
    'spiral',
    'radarSweep',
    'breathingGlow',
    'matrixRain',
    'fireEffect',
    'rainbowWave',
    'countdownTimer',
    'dualColorChase',
    'emergencyStrobe',
    'spinningSegments',
    'pendulum',
    'simonSays',
    'snakeGame',
    'particleExplosion',
    'audioSpectrum'
];

// Map of original names to new names (if renamed)
const nameMapping = {
    'test': 'test-white',
    'testWhite': 'test-white',
    'greenOn': 'green-on',
    'redOn': 'red-on',
    'discoForever': 'disco-forever',
    'psiColorWipe1': 'swipe',
    'psiColorWipe2': 'swipe',
    'psiColorWipeRainbow': 'swipe',
    'marchImperial': 'march',
    'marchTwo': 'march',
    'solidRed': 'red-on',
    'solidYellow': 'flash',
    'solidGreen': 'green-on',
    'solidCyan': 'flash',
    'solidBlue': 'flash',
    'solidPurple': 'flash',
    'solidWhite': 'test-white',
    'clawMachineFlicker': 'short-circuit',
    'clawMachineDisplay': 'loading-bar',
    'cylon': 'knight-rider',
    'knightRider': 'knight-rider',
    'theaterChase': 'theater-chase',
    'warpSpeed': 'warp-speed',
    'starWarsScroll': 'star-wars-scroll',
    'starField': 'star-field',
    'loadingBar': 'loading-bar',
    'wifiBreathing': 'breathe',
    'rippleEffect': 'ripple-effect',
    'randomBars': 'loading-bar',
    'shortCircuit': 'short-circuit',
    'rainbow': 'rainbow-cycle',
    'randomFlash': 'flash',
    'randomColors': 'disco',
    'i_heart_u': 'i-heart-u',
    'plasma': 'plasma-effect',
    'binaryCounter': 'binary-counter',
    'dnaHelix': 'dna-helix',
    'communicationStatic': 'glitch-matrix',
    'morseSOS': 'morse-code',
    'clockDisplay': 'clock-display',
    'rainbowSweep': 'rainbow-cycle',
    'glitchMatrix': 'glitch-matrix',
    'starWarsIntro': 'star-wars-intro',
    'vuMeter': 'audio-spectrum',
    'vuMeterForever': 'audio-spectrum',
    'radarSweep': 'radar-sweep',
    'breathingGlow': 'breathe',
    'matrixRain': 'matrix-rain',
    'fireEffect': 'fire-effect',
    'rainbowWave': 'rainbow-cycle',
    'countdownTimer': 'countdown',
    'dualColorChase': 'theater-chase',
    'emergencyStrobe': 'emergency-strobe',
    'spinningSegments': 'spinning-segments',
    'simonSays': 'simon-says',
    'snakeGame': 'snake-game',
    'particleExplosion': 'particle-explosion',
    'audioSpectrum': 'audio-spectrum'
};

export async function checkFeatureParity() {
    const { AnimationRegistry } = await import('../src/animation-registry.js');
    const registry = new AnimationRegistry();
    await registry.loadAnimations();
    
    const implementedAnimations = registry.getAll().map(a => a.name);
    const missing = [];
    const implemented = [];
    
    for (const original of originalAnimations) {
        const mappedName = nameMapping[original] || original;
        if (implementedAnimations.includes(mappedName)) {
            implemented.push({ original, mapped: mappedName });
        } else {
            missing.push({ original, expectedName: mappedName });
        }
    }
    
    return {
        total: originalAnimations.length,
        implemented: implemented.length,
        missing: missing.length,
        missingAnimations: missing,
        implementedAnimations: implemented,
        coverage: ((implemented.length / originalAnimations.length) * 100).toFixed(1) + '%'
    };
}