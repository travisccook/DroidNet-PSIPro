export default {
    name: 'off',
    displayName: 'Off',
    description: 'All LEDs Off',
    category: 'core',
    
    init() {
        return {};
    },
    
    frame(controller, state) {
        controller.clear();
    }
};