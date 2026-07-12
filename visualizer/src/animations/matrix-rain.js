import { ledMatrix } from '../core/config.js';

export default {
    name: 'matrix-rain',
    displayName: 'Matrix Rain',
    description: 'Digital rain effect',
    category: 'ideas',
    interval: 100,
    
    init() {
        return {
            drops: Array(10).fill(0).map(() => ({
                y: Math.floor(Math.random() * 6),
                speed: Math.random() * 0.5 + 0.5,
                length: Math.floor(Math.random() * 3) + 2
            }))
        };
    },
    
    frame(controller, state) {
        controller.clear();
        
        // Update and draw each drop
        state.drops.forEach((drop, col) => {
            // Draw the drop trail
            for (let i = 0; i < drop.length; i++) {
                const y = Math.floor(drop.y - i);
                if (y >= 0 && y < 6) {
                    const ledIndex = ledMatrix[col][y];
                    if (ledIndex !== -1) {
                        if (i === 0) {
                            // Head of the drop is bright green
                            controller.setLED(ledIndex, 'green');
                        } else if (i === 1) {
                            // Second LED is dimmer
                            controller.setLED(ledIndex, 'darkgreen');
                        } else {
                            // Trail fades to very dark
                            controller.setLED(ledIndex, 'darkgreen');
                        }
                    }
                }
            }
            
            // Move the drop down
            drop.y += drop.speed;
            
            // Reset drop when it goes off screen
            if (drop.y - drop.length > 6) {
                drop.y = -1;
                drop.speed = Math.random() * 0.5 + 0.5;
                drop.length = Math.floor(Math.random() * 3) + 2;
            }
        });
    }
};
