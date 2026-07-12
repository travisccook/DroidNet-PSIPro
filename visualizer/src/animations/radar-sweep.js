export default {
    name: 'radar-sweep',
    displayName: 'Radar Sweep',
    description: 'Rotating radar beam',
    category: 'ideas',
    interval: 100,
    
    init() {
        return {
            angle: 0
        };
    },
    
    frame(controller, state) {
        controller.clear();
        
        // Calculate sweep line
        const centerX = 4.5;
        const centerY = 2.5;
        
        // Light up LEDs along the sweep line
        for (let col = 0; col < 10; col++) {
            for (let row = 0; row < 6; row++) {
                const ledIndex = controller.getLEDIndex(col, row);
                if (ledIndex !== -1) {
                    // Calculate angle from center to this LED
                    const ledAngle = Math.atan2(row - centerY, col - centerX) * 180 / Math.PI;
                    const normalizedAngle = (ledAngle + 360) % 360;
                    const angleDiff = Math.abs(normalizedAngle - state.angle);
                    
                    if (angleDiff < 20 || angleDiff > 340) {
                        controller.setLED(ledIndex, 'green');
                    } else if (angleDiff < 40 || angleDiff > 320) {
                        controller.setLED(ledIndex, 'dimwhite');
                    }
                }
            }
        }
        
        state.angle = (state.angle + 10) % 360;
    }
};
