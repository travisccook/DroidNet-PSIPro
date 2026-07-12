export default {
    name: 'testWhite',
    displayName: 'Test White',
    description: 'All LEDs white/grey',
    category: 'core',
    interval: null, // Static display
    
    init() {
        return {};
    },
    
    frame(controller, state) {
        for (let i = 0; i < 48; i++) {
            controller.setLED(i, 'grey');
        }
    }
};