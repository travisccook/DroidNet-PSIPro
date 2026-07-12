export class AnimationRegistry {
    constructor() {
        this.animations = new Map();
        this.categories = new Map();
    }

    async loadAnimations() {
        // Get all animation modules
        const modules = {
            // Core animations
            'off': () => import('./animations/off.js'),
            'swipe': () => import('./animations/swipe.js'),
            'flash': () => import('./animations/flash.js'),
            'alarm': () => import('./animations/alarm.js'),
            'shortCircuit': () => import('./animations/short-circuit.js'),
            'scream': () => import('./animations/scream.js'),
            'leia': () => import('./animations/leia.js'),
            'iHeartU': () => import('./animations/i-heart-u.js'),
            'radar': () => import('./animations/radar.js'),
            'heart': () => import('./animations/heart.js'),
            'starWarsScroll': () => import('./animations/star-wars-scroll.js'),
            'march': () => import('./animations/march.js'),
            'disco': () => import('./animations/disco.js'),
            'discoForever': () => import('./animations/disco-forever.js'),
            'rebel': () => import('./animations/rebel.js'),
            'knightRider': () => import('./animations/knight-rider.js'),
            'testWhite': () => import('./animations/test-white.js'),
            'redOn': () => import('./animations/red-on.js'),
            'greenOn': () => import('./animations/green-on.js'),
            'lightsaber': () => import('./animations/lightsaber.js'),
            'countdown': () => import('./animations/countdown.js'),
            'binaryCounter': () => import('./animations/binary-counter.js'),
            'starWarsIntro': () => import('./animations/star-wars-intro.js'),
            'glitchMatrix': () => import('./animations/glitch-matrix.js'),
            'emergencyStrobe': () => import('./animations/emergency-strobe.js'),
            'rippleEffect': () => import('./animations/ripple-effect.js'),
            'spinningSegments': () => import('./animations/spinning-segments.js'),
            'morseCode': () => import('./animations/morse-code.js'),
            'plasmaEffect': () => import('./animations/plasma-effect.js'),
            'dnaHelix': () => import('./animations/dna-helix.js'),
            'loadingBar': () => import('./animations/loading-bar.js'),
            'theaterChase': () => import('./animations/theater-chase.js'),
            'starField': () => import('./animations/star-field.js'),
            'fireEffect': () => import('./animations/fire-effect.js'),
            'spiral': () => import('./animations/spiral.js'),
            'radarSweep': () => import('./animations/radar-sweep.js'),
            'matrixRain': () => import('./animations/matrix-rain.js'),
            'rainbowCycle': () => import('./animations/rainbow-cycle.js'),
            'breathe': () => import('./animations/breathe.js'),
            'warpSpeed': () => import('./animations/warp-speed.js'),
            'heartbeat': () => import('./animations/heartbeat.js'),
            'pendulum': () => import('./animations/pendulum.js'),
            'simonSays': () => import('./animations/simon-says.js'),
            'snakeGame': () => import('./animations/snake-game.js'),
            'audioSpectrum': () => import('./animations/audio-spectrum.js'),
            'clockDisplay': () => import('./animations/clock-display.js'),
            'particleExplosion': () => import('./animations/particle-explosion.js')
        };

        // Load all animations
        for (const [name, loader] of Object.entries(modules)) {
            try {
                const module = await loader();
                const animation = module.default;
                this.animations.set(name, animation);
                this.addToCategory(animation);
            } catch (error) {
                console.warn(`Failed to load animation ${name}:`, error);
            }
        }
    }

    addToCategory(animation) {
        const category = animation.category || 'misc';
        if (!this.categories.has(category)) {
            this.categories.set(category, []);
        }
        this.categories.get(category).push(animation);
    }

    get(name) {
        return this.animations.get(name);
    }

    getAll() {
        return Array.from(this.animations.values());
    }

    getByCategory(category) {
        return this.categories.get(category) || [];
    }

    getAllCategories() {
        return Array.from(this.categories.keys());
    }
}