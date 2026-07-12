export default {
    name: 'clock-display',
    displayName: 'Clock Display',
    description: 'Time display',
    category: 'ideas',
    interval: 100,
    
    init() {
        return {
            // TODO: Initialize state
        };
    },
    
    frame(controller, state) {
        // TODO: Implement animation
        controller.clear();
    }
};
