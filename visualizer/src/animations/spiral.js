import { ledMatrix } from '../core/config.js';

export default {
    name: 'spiral',
    displayName: 'Spiral',
    description: 'Spiral pattern',
    category: 'ideas',
    interval: 100,
    
    init() {
        return {
            position: 0,
            direction: 1,
            spiralPath: []
        };
    },
    
    frame(controller, state) {
        // Initialize spiral path if empty
        if (state.spiralPath.length === 0) {
            const path = [];
            const visited = new Set();
            let x = 4, y = 2; // Start near center
            let dx = 1, dy = 0; // Initial direction: right
            let steps = 1, stepCount = 0;
            let turnCount = 0;
            
            while (path.length < 48) {
                // Add current position if valid and not visited
                if (x >= 0 && x < 10 && y >= 0 && y < 6) {
                    const ledIndex = ledMatrix[x][y];
                    if (ledIndex !== -1 && !visited.has(ledIndex)) {
                        path.push(ledIndex);
                        visited.add(ledIndex);
                    }
                }
                
                // Move to next position
                x += dx;
                y += dy;
                stepCount++;
                
                // Check if we need to turn
                if (stepCount >= steps) {
                    stepCount = 0;
                    
                    // Turn 90 degrees counter-clockwise
                    const temp = dx;
                    dx = -dy;
                    dy = temp;
                    
                    turnCount++;
                    if (turnCount % 2 === 0) {
                        steps++;
                    }
                }
                
                // Safety check to prevent infinite loop
                if (path.length === visited.size && visited.size < 48) {
                    // If we can't add more to path, find the next unvisited LED
                    for (let i = 0; i < 48; i++) {
                        if (!visited.has(i)) {
                            path.push(i);
                            visited.add(i);
                            break;
                        }
                    }
                }
            }
            
            state.spiralPath = path;
        }
        
        controller.clear();
        
        // Draw the spiral with a trail
        const trailLength = 8;
        for (let i = 0; i < trailLength; i++) {
            const idx = (state.position - i + state.spiralPath.length) % state.spiralPath.length;
            const ledIndex = state.spiralPath[idx];
            
            if (i === 0) {
                controller.setLED(ledIndex, 'white');
            } else if (i < 3) {
                controller.setLED(ledIndex, 'blue');
            } else {
                controller.setLED(ledIndex, 'darkblue');
            }
        }
        
        // Update position
        state.position = (state.position + state.direction + state.spiralPath.length) % state.spiralPath.length;
    }
};
