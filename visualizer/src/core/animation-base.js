// Base class for animations
export class AnimationBase {
    constructor(config) {
        this.name = config.name;
        this.displayName = config.displayName;
        this.description = config.description;
        this.category = config.category || 'misc';
        this.interval = config.interval || 100;
        this.state = {};
    }

    // Initialize animation state
    init() {
        return {};
    }

    // Called on each frame
    frame(controller) {
        // Override in subclass
    }

    // Start the animation
    start(controller) {
        this.state = this.init();
        return setInterval(() => {
            this.frame(controller);
        }, this.interval);
    }

    // Clean up when animation stops
    cleanup() {
        // Override if needed
    }
}

// Simple animation that doesn't need state management
export function createSimpleAnimation(config) {
    return {
        name: config.name,
        displayName: config.displayName,
        description: config.description,
        category: config.category || 'misc',
        interval: config.interval || 100,
        
        init() {
            return config.init ? config.init() : {};
        },
        
        frame(controller, state) {
            if (config.frame) {
                config.frame(controller, state);
            }
        }
    };
}