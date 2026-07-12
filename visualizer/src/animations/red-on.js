export default {
    name: 'redOn',
    displayName: 'Red On',
    description: 'All LEDs red/yellow',
    category: 'core',
    interval: null, // Static display
    
    init() {
        return {};
    },
    
    frame(controller, state) {
        for (let i = 0; i < 48; i++) {
            controller.setLED(i, 'red', 'front');
            controller.setLED(i, 'yellow', 'rear');
        }
    }
};