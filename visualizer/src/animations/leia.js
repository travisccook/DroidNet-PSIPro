export default {
    name: 'leia',
    displayName: 'Leia Message',
    description: 'Leia Message (Cylon row effect)',
    category: 'core',
    interval: 74,
    
    init() {
        return {
            row: 0,
            direction: 1
        };
    },
    
    frame(controller, state) {
        controller.clear();
        controller.fillRow(state.row, 'grey');
        
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