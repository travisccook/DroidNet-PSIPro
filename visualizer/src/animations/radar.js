import { ledMatrix } from '../core/config.js';

export default {
    name: 'radar',
    displayName: 'Radar',
    description: 'Quarter panel sweep',
    category: 'core',
    interval: 250,
    
    init() {
        return {
            state: 0
        };
    },
    
    frame(controller, state) {
        controller.clear();
        
        switch(state.state) {
            case 0: // Top half of left side (columns 0-4)
                for (let col = 0; col <= 4; col++) {
                    for (let row = 0; row < 3; row++) {
                        const ledIndex = ledMatrix[col][row];
                        if (ledIndex !== -1) controller.setLED(ledIndex, 'red');
                    }
                }
                break;
            case 1: // Clear
                break;
            case 2: // Top half of right side (columns 5-9)
                for (let col = 5; col <= 9; col++) {
                    for (let row = 0; row < 3; row++) {
                        const ledIndex = ledMatrix[col][row];
                        if (ledIndex !== -1) controller.setLED(ledIndex, 'red');
                    }
                }
                break;
            case 3: // Clear
                break;
            case 4: // Bottom half of right side (columns 5-9)
                for (let col = 5; col <= 9; col++) {
                    for (let row = 3; row < 6; row++) {
                        const ledIndex = ledMatrix[col][row];
                        if (ledIndex !== -1) controller.setLED(ledIndex, 'red');
                    }
                }
                break;
            case 5: // Clear
                break;
            case 6: // Bottom half of left side (columns 0-4)
                for (let col = 0; col <= 4; col++) {
                    for (let row = 3; row < 6; row++) {
                        const ledIndex = ledMatrix[col][row];
                        if (ledIndex !== -1) controller.setLED(ledIndex, 'red');
                    }
                }
                break;
            case 7: // Clear
                break;
        }
        
        state.state = (state.state + 1) % 8;
    }
};