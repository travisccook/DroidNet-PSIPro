export default {
    name: 'theater-chase',
    displayName: 'Theater Chase',
    description: 'Classic marquee lights',
    category: 'ideas',
    interval: 150,
    
    init() {
        return {
            position: 0,
            position2: 5,
            color1: 'red',
            color2: 'blue'
        };
    },
    
    frame(controller, state) {
        controller.clear();
        
        // Draw two colors chasing each other
        controller.fillColumn(state.position, state.color1);
        controller.fillColumn((state.position + 1) % 10, 'dimred');
        
        controller.fillColumn(state.position2, state.color2);
        controller.fillColumn((state.position2 + 1) % 10, 'dimblue');
        
        state.position = (state.position + 1) % 10;
        state.position2 = (state.position2 + 1) % 10;
    }
};
