import { patterns } from '../core/config.js';

export default {
    name: 'heart',
    displayName: 'Heart',
    description: 'Heartbeat pattern',
    category: 'core',
    interval: 100,
    
    init() {
        return {
            beat: 0,
            position: 0,
            pulsePath: [33, 32, 16, 30, 36, 45, 38, 28, 20, 8, 5, 6, 23]
        };
    },
    
    frame(controller, state) {
        // Front PSI - heart
        if (state.beat % 10 < 5) {
            controller.displayMatrix(patterns.heart, {1: 'red', 0: 'grey'}, 'front');
        } else {
            controller.clearFront();
        }
        
        // Rear PSI - pulse
        controller.displayMatrix(patterns.pulse, {1: 'dim-white', 0: 'grey'}, 'rear');
        if (state.position < state.pulsePath.length) {
            controller.setLED(state.pulsePath[state.position], 'green', 'rear');
        }
        
        state.position = (state.position + 1) % state.pulsePath.length;
        state.beat++;
    }
};