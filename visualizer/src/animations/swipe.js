export default {
    name: 'swipe',
    displayName: 'Swipe',
    description: 'Default swipe pattern',
    category: 'core',
    interval: 100,
    
    init() {
        return {
            position: 0,
            direction: 1,
            state: 'primary', // primary, transition, secondary
            pauseCounter: 0,
            pauseDuration: 0
        };
    },
    
    frame(controller, state) {
        if (state.pauseCounter > 0) {
            state.pauseCounter--;
            return;
        }
        
        controller.clear();
        
        if (state.state === 'primary') {
            // All primary color
            for (let col = 0; col < 10; col++) {
                controller.fillColumn(col, 'blue', 'front');
                controller.fillColumn(col, 'green', 'rear');
            }
            // Random pause on primary
            if (state.pauseDuration === 0) {
                state.pauseDuration = 20 + Math.floor(Math.random() * 80); // 2-10 seconds at 100ms intervals
                state.pauseCounter = state.pauseDuration;
                state.state = 'transition';
                state.position = 0;
                state.direction = 1;
            }
        } else if (state.state === 'transition') {
            // Swipe effect
            for (let col = 0; col < 10; col++) {
                if (col < state.position) {
                    controller.fillColumn(col, 'red', 'front');
                    controller.fillColumn(col, 'yellow', 'rear');
                } else {
                    controller.fillColumn(col, 'blue', 'front');
                    controller.fillColumn(col, 'green', 'rear');
                }
            }
            
            state.position += state.direction;
            
            if (state.position > 10 && state.direction === 1) {
                state.state = 'secondary';
                state.pauseDuration = 40 + Math.floor(Math.random() * 80); // 4-12 seconds
                state.pauseCounter = state.pauseDuration;
            } else if (state.position < 0 && state.direction === -1) {
                state.state = 'primary';
                state.pauseDuration = 20 + Math.floor(Math.random() * 80);
                state.pauseCounter = state.pauseDuration;
            }
        } else if (state.state === 'secondary') {
            // All secondary color
            for (let col = 0; col < 10; col++) {
                controller.fillColumn(col, 'red', 'front');
                controller.fillColumn(col, 'yellow', 'rear');
            }
            if (state.pauseDuration === 0) {
                state.state = 'transition';
                state.position = 10;
                state.direction = -1;
            }
        }
        
        if (state.pauseDuration > 0) state.pauseDuration--;
    }
};