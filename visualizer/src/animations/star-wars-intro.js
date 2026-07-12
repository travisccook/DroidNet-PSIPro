export default {
    name: 'star-wars-intro',
    displayName: 'Star Wars Intro',
    description: 'Opening crawl effect',
    category: 'core',
    interval: 500,
    
    init() {
        return {
            state: 0
        };
    },
    
    frame(controller, state) {
        controller.clear();
        
        switch(state.state % 8) {
            case 0: 
                controller.fillRow(5, 'yellow'); 
                break;
            case 1: 
                break;
            case 2: 
                controller.fillRow(4, 'yellow'); 
                break;
            case 3: 
                break;
            case 4:
                controller.fillRow(3, 'yellow');
                // Remove outer LEDs to simulate perspective
                controller.setLED(controller.getLEDIndex(2, 3), 'off');
                controller.setLED(controller.getLEDIndex(7, 3), 'off');
                break;
            case 5:
                controller.fillRow(4, 'yellow');
                controller.fillRow(2, 'dimwhite');
                // Make row 2 smaller (remove outer LEDs)
                for (let col of [0, 1, 8, 9]) {
                    const ledIndex = controller.getLEDIndex(col, 2);
                    if (ledIndex !== -1) {
                        controller.setLED(ledIndex, 'off');
                    }
                }
                break;
            case 6:
                controller.fillRow(3, 'yellow');
                controller.setLED(controller.getLEDIndex(2, 3), 'off');
                controller.setLED(controller.getLEDIndex(7, 3), 'off');
                controller.fillRow(1, 'dimwhite');
                // Make row 1 even smaller
                for (let col of [0, 1, 2, 7, 8, 9]) {
                    const ledIndex = controller.getLEDIndex(col, 1);
                    if (ledIndex !== -1) {
                        controller.setLED(ledIndex, 'off');
                    }
                }
                break;
            case 7: 
                break;
        }
        
        state.state++;
    }
};
