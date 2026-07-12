export default {
    name: 'starWarsScroll',
    displayName: 'Star Wars Scroll',
    description: 'Cylon row with yellow color',
    category: 'core',
    interval: 500,
    
    init() {
        return {
            row: 0,
            direction: 1
        };
    },
    
    frame(controller, state) {
        controller.clear();
        controller.fillRow(state.row, 'yellow');
        
        state.row += state.direction;
        if (state.row >= 6) {
            state.row = 5;
            state.direction = -1;
        } else if (state.row < 0) {
            state.row = 0;
            state.direction = 1;
        }
    }
};