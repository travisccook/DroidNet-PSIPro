export default {
    name: 'knightRider',
    displayName: 'Knight Rider',
    description: 'KITT scanner effect',
    category: 'core',
    interval: 250,
    
    init() {
        return {
            position: 0,
            direction: 1
        };
    },
    
    frame(controller, state) {
        controller.clear();
        
        controller.fillColumn(state.position, 'red');
        
        // Trailing effect
        if (state.position - state.direction >= 0 && state.position - state.direction < 10) {
            controller.fillColumn(state.position - state.direction, 'dim-red');
        }
        
        state.position += state.direction;
        if (state.position >= 10) {
            state.position = 9;
            state.direction = -1;
        } else if (state.position < 0) {
            state.position = 0;
            state.direction = 1;
        }
    }
};