export default {
    name: 'march',
    displayName: 'March',
    description: 'Imperial March - rhythmic flashing',
    category: 'core',
    interval: 552,
    
    init() {
        return {
            pattern: 0,
            marchPattern: [1,0,1,0,1,0,0,0,1,0,1,0,1,0,0,0]
        };
    },
    
    frame(controller, state) {
        if (state.marchPattern[state.pattern % state.marchPattern.length]) {
            for (let i = 0; i < 48; i++) {
                controller.setLED(i, 'white');
            }
        } else {
            controller.clear();
        }
        state.pattern++;
    }
};