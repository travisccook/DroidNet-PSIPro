import { ledMatrix } from '../core/config.js';

export default {
    name: 'ripple-effect',
    displayName: 'Ripple Effect',
    description: 'Water drop ripples',
    category: 'ideas',
    interval: 100,
    
    init() {
        return {
            ripples: [],
            fadingLeds: new Map()
        };
    },
    
    frame(controller, state) {
        // Fade existing LEDs
        for (let [index, fadeLevel] of state.fadingLeds) {
            if (fadeLevel === 3) {
                controller.setLED(index, 'white');
            } else if (fadeLevel === 2) {
                controller.setLED(index, 'grey');
            } else if (fadeLevel === 1) {
                controller.setLED(index, 'dimwhite');
            } else {
                controller.setLED(index, 'off');
                state.fadingLeds.delete(index);
                continue;
            }
            state.fadingLeds.set(index, fadeLevel - 1);
        }
        
        // Add new ripple occasionally
        if (Math.random() < 0.1 && state.ripples.length < 3) {
            state.ripples.push({
                x: 4.5,
                y: 2.5,
                radius: 0
            });
        }
        
        // Update and draw ripples
        state.ripples = state.ripples.filter(ripple => {
            ripple.radius += 0.5;
            
            // Draw ripple ring
            for (let col = 0; col < 10; col++) {
                for (let row = 0; row < 6; row++) {
                    const ledIndex = ledMatrix[col][row];
                    if (ledIndex !== -1) {
                        const dx = col - ripple.x;
                        const dy = row - ripple.y;
                        const distance = Math.sqrt(dx * dx + dy * dy);
                        
                        if (Math.abs(distance - ripple.radius) < 0.7) {
                            controller.setLED(ledIndex, 'white');
                            state.fadingLeds.set(ledIndex, 3);
                        }
                    }
                }
            }
            
            return ripple.radius < 8;
        });
    }
};
