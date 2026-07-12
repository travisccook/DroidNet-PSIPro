export default {
    name: 'flash',
    displayName: 'Flash',
    description: 'Fast Flash',
    category: 'core',
    interval: 60,
    
    init() {
        return {
            on: false
        };
    },
    
    frame(controller, state) {
        if (state.on) {
            controller.clear();
        } else {
            for (let i = 0; i < 48; i++) {
                controller.setLED(i, 'white');
            }
        }
        state.on = !state.on;
    }
};