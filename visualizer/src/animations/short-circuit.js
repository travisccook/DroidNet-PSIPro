export default {
    name: 'shortCircuit',
    displayName: 'Short Circuit',
    description: 'Short Circuit Effect',
    category: 'core',
    interval: 257,
    
    init() {
        return {
            initialized: false
        };
    },
    
    frame(controller, state) {
        if (!state.initialized) {
            // Initialize all LEDs to blue
            for (let index = 0; index < 48; index++) {
                controller.setLED(index, 'blue');
            }
            state.initialized = true;
        }
        
        // Randomly dim or brighten pixels
        for (let index = 0; index < 48; index++) {
            if (Math.random() > 0.7) {
                const brightness = Math.random();
                if (brightness < 0.3) {
                    controller.setLED(index, 'off');
                } else if (brightness < 0.6) {
                    controller.setLED(index, 'dim-blue');
                } else {
                    controller.setLED(index, 'blue');
                }
            }
        }
    }
};