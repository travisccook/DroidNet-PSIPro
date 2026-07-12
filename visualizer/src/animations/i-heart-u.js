import { patterns } from '../core/config.js';

export default {
    name: 'iHeartU',
    displayName: 'I Heart U',
    description: 'I Heart U sequence',
    category: 'core',
    interval: 500,
    
    init() {
        return {
            state: 0,
            sequences: [
                () => patterns.letterI,
                null, // clear
                () => patterns.heart,
                null, // clear
                () => patterns.letterU,
                null  // clear
            ]
        };
    },
    
    frame(controller, state) {
        const sequence = state.sequences[state.state];
        
        if (sequence === null) {
            controller.clear();
        } else {
            controller.displayMatrix(sequence(), {1: 'red', 0: 'grey'});
        }
        
        state.state = (state.state + 1) % state.sequences.length;
    }
};