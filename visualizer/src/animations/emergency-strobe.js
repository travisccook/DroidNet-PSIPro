export default {
    name: 'emergency-strobe',
    displayName: 'Emergency Strobe',
    description: 'SOS pattern',
    category: 'ideas',
    interval: 100,
    
    init() {
        return {
            strobePatterns: [
                [1,1,0,0,1,1,0,0,1,1,0,0,0,0,0,0], // Quick double flash
                [1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0], // SOS pattern start
                [1,1,1,0,1,1,1,0,1,1,1,0,0,0,0,0], // SOS middle
                [1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0]  // SOS end
            ],
            patternIndex: 0,
            step: 0
        };
    },
    
    frame(controller, state) {
        if (state.strobePatterns[state.patternIndex][state.step]) {
            for (let index = 0; index < 48; index++) {
                controller.setLED(index, 'white');
            }
        } else {
            controller.clear();
        }
        
        state.step++;
        if (state.step >= state.strobePatterns[state.patternIndex].length) {
            state.step = 0;
            state.patternIndex = (state.patternIndex + 1) % state.strobePatterns.length;
        }
    }
};
