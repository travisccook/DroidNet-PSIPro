export default {
    name: 'heartbeat',
    displayName: 'Heartbeat',
    description: 'Rhythmic pulse',
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
