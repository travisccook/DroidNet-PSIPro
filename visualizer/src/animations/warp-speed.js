export default {
    name: 'warp-speed',
    displayName: 'Warp Speed',
    description: 'Star Trek warp',
    category: 'ideas',
    interval: 30,
    
    init() {
        const stars = [];
        for (let i = 0; i < 48; i++) {
            stars.push({
                led: i,
                speed: Math.random() * 0.5 + 0.5,
                brightness: 0
            });
        }
        return { stars };
    },
    
    frame(controller, state) {
        controller.clear();
        
        state.stars.forEach(star => {
            star.brightness += star.speed * 0.1;
            
            if (star.brightness > 1) {
                star.brightness = 0;
                star.speed = Math.random() * 0.5 + 0.5;
            }
            
            const col = Math.floor(star.led / 6);
            const distFromCenter = Math.abs(col - 4.5);
            const brightness = star.brightness * (1 - distFromCenter / 5);
            
            if (brightness > 0.6) {
                controller.setLED(star.led, 'white');
            } else if (brightness > 0.3) {
                controller.setLED(star.led, 'blue');
            } else if (brightness > 0.1) {
                controller.setLED(star.led, 'dimblue');
            }
        });
    }
};
