export default {
    name: 'breathe',
    displayName: 'Breathe',
    description: 'Breathing effect',
    category: 'ideas',
    interval: 50,
    
    init() {
        return {
            brightness: 0,
            direction: 1
        };
    },
    
    frame(controller, state) {
        controller.clear();
        
        // Apply breathing effect to all LEDs
        const dimLevel = Math.floor(state.brightness / 33);
        const frontColorMap = {
            0: 'off',
            1: 'dimblue',
            2: 'blue',
            3: 'blue'
        };
        const rearColorMap = {
            0: 'off',
            1: 'dimwhite',
            2: 'green',
            3: 'green'
        };
        
        for (let index = 0; index < 48; index++) {
            controller.setLED(index, frontColorMap[dimLevel] || 'off', 'front');
            controller.setLED(index, rearColorMap[dimLevel] || 'off', 'rear');
        }
        
        state.brightness += state.direction * 5;
        if (state.brightness >= 100) {
            state.brightness = 100;
            state.direction = -1;
        } else if (state.brightness <= 0) {
            state.brightness = 0;
            state.direction = 1;
        }
    }
};
