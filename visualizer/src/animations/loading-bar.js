export default {
    name: 'loading-bar',
    displayName: 'Loading Bar',
    description: 'Progress indicator',
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
