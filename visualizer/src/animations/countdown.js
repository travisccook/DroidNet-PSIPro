import { ledMatrix } from '../core/config.js';

export default {
    name: 'countdown',
    displayName: 'Countdown',
    description: '10 to 0 countdown',
    category: 'ideas',
    interval: 600,
    
    init() {
        return {
            number: 9,
            flashState: 0,
            numbers: {
                9: [[0,1,1,1,0], [1,0,0,0,1], [1,0,0,0,1], [0,1,1,1,1], [0,0,0,0,1], [0,1,1,1,0]],
                8: [[0,1,1,1,0], [1,0,0,0,1], [0,1,1,1,0], [1,0,0,0,1], [1,0,0,0,1], [0,1,1,1,0]],
                7: [[1,1,1,1,1], [0,0,0,0,1], [0,0,0,1,0], [0,0,1,0,0], [0,1,0,0,0], [1,0,0,0,0]],
                6: [[0,1,1,1,0], [1,0,0,0,0], [1,1,1,1,0], [1,0,0,0,1], [1,0,0,0,1], [0,1,1,1,0]],
                5: [[1,1,1,1,1], [1,0,0,0,0], [1,1,1,1,0], [0,0,0,0,1], [0,0,0,0,1], [1,1,1,1,0]],
                4: [[0,0,0,1,0], [0,0,1,1,0], [0,1,0,1,0], [1,1,1,1,1], [0,0,0,1,0], [0,0,0,1,0]],
                3: [[0,1,1,1,0], [1,0,0,0,1], [0,0,1,1,0], [0,0,0,0,1], [1,0,0,0,1], [0,1,1,1,0]],
                2: [[0,1,1,1,0], [1,0,0,0,1], [0,0,0,1,0], [0,0,1,0,0], [0,1,0,0,0], [1,1,1,1,1]],
                1: [[0,0,1,0,0], [0,1,1,0,0], [0,0,1,0,0], [0,0,1,0,0], [0,0,1,0,0], [0,1,1,1,0]],
                0: [[0,1,1,1,0], [1,0,0,0,1], [1,0,0,0,1], [1,0,0,0,1], [1,0,0,0,1], [0,1,1,1,0]]
            }
        };
    },
    
    frame(controller, state) {
        controller.clear();
        
        if (state.flashState > 0) {
            // Flash red at zero
            if (state.flashState === 2) {
                for (let index = 0; index < 48; index++) {
                    controller.setLED(index, 'red');
                }
            }
            state.flashState--;
            if (state.flashState === 0) {
                state.number = 9; // Reset
            }
            return;
        }
        
        // Display the number centered
        const pattern = state.numbers[state.number];
        if (pattern) {
            for (let row = 0; row < 6; row++) {
                for (let col = 0; col < 5; col++) {
                    const actualCol = col + 2; // Center the 5-wide number
                    const ledIndex = ledMatrix[actualCol][row];
                    if (ledIndex !== -1 && pattern[row][col] === 1) {
                        controller.setLED(ledIndex, state.number === 0 ? 'red' : 'white');
                    }
                }
            }
        }
        
        state.number--;
        if (state.number < 0) {
            state.number = 0;
            state.flashState = 2;
        }
    }
};