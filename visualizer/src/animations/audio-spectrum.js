export default {
    name: 'audio-spectrum',
    displayName: 'Audio Spectrum',
    description: 'VU meter frequency display',
    category: 'core',
    interval: 250,
    
    init() {
        return {
            levels: new Array(10).fill(3)
        };
    },
    
    frame(controller, state) {
        // VU meter colors: 1=green, 2=yellow, 3=orange, 4=red
        const vuColors = {0: 'off', 1: 'green', 2: 'yellow', 3: 'orange', 4: 'red'};
        
        // Display the VU chart pattern
        controller.displayMatrix([
            [4,4,4,4,4,4],      // Row 0 - Red
            [0,3,3,3,3,3,3,0],  // Row 1 - Orange
            [0,0,2,2,2,2,2,2,0,0], // Row 2 - Yellow
            [0,0,1,1,1,1,1,1,0,0], // Row 3 - Green
            [0,1,1,1,1,1,1,0],  // Row 4 - Green
            [1,1,1,1,1,1]       // Row 5 - Green
        ], vuColors);
        
        // Update and display levels for each column
        for (let col = 0; col < 10; col++) {
            // Update level randomly
            const change = Math.random() > 0.5 ? 1 : -1;
            const changeAmount = Math.random() > 0.7 ? 2 : 1;
            state.levels[col] = Math.max(0, Math.min(6, state.levels[col] + (change * changeAmount)));
            
            // Turn off LEDs above the level
            for (let row = 0; row < 6; row++) {
                const ledIndex = controller.getLEDIndex(col, row);
                if (ledIndex !== -1 && row < (6 - state.levels[col])) {
                    controller.setLED(ledIndex, 'off');
                }
            }
        }
    }
};
