import { ledMatrix } from '../core/config.js';

export default {
    name: 'fire-effect',
    displayName: 'Fire Effect',
    description: 'Flame simulation',
    category: 'ideas',
    interval: 100,
    
    init() {
        return {
            heat: new Array(10).fill(0).map(() => new Array(6).fill(0))
        };
    },
    
    frame(controller, state) {
        // Cool down all cells
        for (let col = 0; col < 10; col++) {
            for (let row = 0; row < 6; row++) {
                state.heat[col][row] = Math.max(0, state.heat[col][row] - Math.random() * 0.1);
            }
        }
        
        // Heat bottom row
        for (let col = 0; col < 10; col++) {
            if (Math.random() > 0.3) {
                state.heat[col][5] = 0.8 + Math.random() * 0.2;
            }
        }
        
        // Rise and diffuse heat
        for (let row = 0; row < 5; row++) {
            for (let col = 0; col < 10; col++) {
                const below = state.heat[col][row + 1];
                state.heat[col][row] = (state.heat[col][row] + below * 0.8) / 2;
            }
        }
        
        // Display fire
        for (let col = 0; col < 10; col++) {
            for (let row = 0; row < 6; row++) {
                const ledIndex = ledMatrix[col][row];
                if (ledIndex !== -1) {
                    const h = state.heat[col][row];
                    let color = 'off';
                    if (h > 0.9) color = 'white';
                    else if (h > 0.7) color = 'yellow';
                    else if (h > 0.5) color = 'orange';
                    else if (h > 0.3) color = 'red';
                    else if (h > 0.1) color = 'dimred';
                    
                    controller.setLED(ledIndex, color);
                }
            }
        }
    }
};
