export default {
    name: 'discoForever',
    displayName: 'Disco Forever',
    description: 'Continuous disco sparkles',
    category: 'core',
    interval: 150,
    
    init() {
        return {};
    },
    
    frame(controller, state) {
        controller.clear();
        
        // 3 random sparkles on each PSI
        for (let i = 0; i < 3; i++) {
            const frontLed = Math.floor(Math.random() * 48);
            const rearLed = Math.floor(Math.random() * 48);
            controller.setLED(frontLed, 'grey', 'front');
            controller.setLED(rearLed, 'grey', 'rear');
        }
    }
};