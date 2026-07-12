export default {
    name: 'rainbow-cycle',
    displayName: 'Rainbow Cycle',
    description: 'Color cycling',
    category: 'ideas',
    interval: 200,
    
    init() {
        return {
            offset: 0,
            colors: ['red', 'orange', 'yellow', 'green', 'blue', 'white']
        };
    },
    
    frame(controller, state) {
        for (let col = 0; col < 10; col++) {
            const colorIndex = (col + state.offset) % state.colors.length;
            controller.fillColumn(col, state.colors[colorIndex]);
        }
        
        state.offset = (state.offset + 1) % state.colors.length;
    }
};
