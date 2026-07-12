import { patterns } from '../core/config.js';

export default {
    name: 'rebel',
    displayName: 'Rebel',
    description: 'Rebel Alliance logo',
    category: 'core',
    interval: null, // Static display
    
    init() {
        return {};
    },
    
    frame(controller, state) {
        controller.displayMatrix(patterns.rebel, {1: 'red', 0: 'grey'});
    }
};