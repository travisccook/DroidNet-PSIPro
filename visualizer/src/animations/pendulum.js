import { ledMatrix } from '../core/config.js';

export default {
    name: 'pendulum',
    displayName: 'Pendulum',
    description: 'Swinging motion',
    category: 'ideas',
    interval: 100,
    
    init() {
        return {
            angle: 0,
            angleVelocity: 0.05,
            trails: []
        };
    },
    
    frame(controller, state) {
        controller.clear();
        
        // Calculate pendulum position
        const centerX = 4.5;
        const amplitude = 4;
        const x = centerX + amplitude * Math.sin(state.angle);
        const y = 2.5; // Middle height
        
        // Add current position to trails
        state.trails.unshift({ x, y });
        if (state.trails.length > 8) {
            state.trails.pop();
        }
        
        // Draw the pendulum bob and trail
        state.trails.forEach((pos, i) => {
            const col = Math.round(pos.x);
            const row = Math.round(pos.y);
            
            if (col >= 0 && col < 10 && row >= 0 && row < 6) {
                const ledIndex = ledMatrix[col][row];
                if (ledIndex !== -1) {
                    if (i === 0) {
                        controller.setLED(ledIndex, 'white');
                    } else if (i < 3) {
                        controller.setLED(ledIndex, 'blue');
                    } else {
                        controller.setLED(ledIndex, 'darkblue');
                    }
                }
            }
        });
        
        // Draw vertical support line
        for (let row = 0; row <= 2; row++) {
            const ledIndex = ledMatrix[4][row];
            if (ledIndex !== -1) {
                controller.setLED(ledIndex, 'grey');
            }
        }
        
        // Update angle with simple harmonic motion
        state.angle += state.angleVelocity;
        
        // Add slight damping and reverse at extremes
        if (Math.abs(state.angle) > Math.PI / 3) {
            state.angleVelocity *= -0.98;
        }
    }
};
