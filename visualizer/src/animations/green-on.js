export default {
    name: 'greenOn',
    displayName: 'Green On',
    description: 'All LEDs blue/green',
    category: 'core',
    interval: null, // Static display
    
    init() {
        return {};
    },
    
    frame(controller, state) {
        for (let i = 0; i < 48; i++) {
            controller.setLED(i, 'blue', 'front');
            controller.setLED(i, 'green', 'rear');
        }
    }
};