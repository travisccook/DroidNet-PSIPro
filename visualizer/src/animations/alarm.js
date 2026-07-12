export default {
    name: 'alarm',
    displayName: 'Alarm',
    description: 'Slow Flash/Alarm',
    category: 'core',
    interval: 125,
    
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