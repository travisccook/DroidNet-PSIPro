import { LEDController } from './core/led-controller.js';
import { AnimationRegistry } from './animation-registry.js';

class PSIVisualizerApp {
    constructor() {
        this.controller = new LEDController();
        this.registry = new AnimationRegistry();
        this.currentAnimation = null;
        this.animationInterval = null;
        this.animationState = {};
    }

    async initialize() {
        // Initialize LED display
        this.controller.initialize('frontMatrix', 'rearMatrix');
        
        // Load all animations
        await this.registry.loadAnimations();
        
        // Set up UI
        this.setupUI();
        
        // Load rejected animations from localStorage
        this.loadRejectedIdeas();
    }

    setupUI() {
        // Create buttons for all animations
        const categories = {
            'core': document.getElementById('coreControls'),
            'patterns': document.getElementById('patternControls'),
            'effects': document.getElementById('effectControls'),
            'interactive': document.getElementById('interactiveControls'),
            'ideas': document.getElementById('ideaControls')
        };

        // Clear existing buttons
        Object.values(categories).forEach(container => {
            if (container) container.innerHTML = '';
        });

        // Add buttons for each animation
        this.registry.getAll().forEach(animation => {
            const container = categories[animation.category];
            if (!container) return;

            if (animation.category === 'ideas') {
                // Special handling for idea buttons
                const buttonContainer = document.createElement('div');
                buttonContainer.className = 'idea-button-container';
                
                const button = document.createElement('button');
                button.textContent = animation.displayName;
                button.onclick = () => this.startAnimation(animation.name);
                button.style.backgroundColor = '#004466';
                
                const rejectButton = document.createElement('button');
                rejectButton.className = 'reject-button';
                rejectButton.textContent = '×';
                rejectButton.title = 'Reject this idea';
                rejectButton.onclick = () => this.rejectIdea(animation.name, animation.displayName);
                
                buttonContainer.appendChild(button);
                buttonContainer.appendChild(rejectButton);
                container.appendChild(buttonContainer);
            } else {
                const button = document.createElement('button');
                button.textContent = animation.displayName;
                button.onclick = () => this.startAnimation(animation.name);
                container.appendChild(button);
            }
        });

        // Set up stop button
        const stopButton = document.getElementById('stopButton');
        if (stopButton) {
            stopButton.onclick = () => this.stopAnimation();
        }
    }

    startAnimation(name) {
        this.stopAnimation();
        
        const animation = this.registry.get(name);
        if (!animation) {
            console.error('Animation not found:', name);
            return;
        }

        // Update UI
        document.getElementById('animationName').textContent = 
            animation.description || animation.displayName;
        
        // Initialize animation state
        this.animationState = animation.init ? animation.init() : {};
        
        // Start animation
        this.currentAnimation = animation;
        if (animation.interval === null || animation.interval === undefined) {
            // One-time animation
            animation.frame(this.controller, this.animationState);
        } else {
            // Repeating animation
            this.animationInterval = setInterval(() => {
                animation.frame(this.controller, this.animationState);
            }, animation.interval || 100);
        }
        
        // Update active button
        document.querySelectorAll('button').forEach(btn => btn.classList.remove('active'));
        if (event && event.target) event.target.classList.add('active');
    }

    stopAnimation() {
        if (this.animationInterval) {
            clearInterval(this.animationInterval);
            this.animationInterval = null;
        }
        
        if (this.currentAnimation && this.currentAnimation.cleanup) {
            this.currentAnimation.cleanup();
        }
        
        this.controller.clear();
        this.currentAnimation = null;
        this.animationState = {};
        
        document.querySelectorAll('button').forEach(btn => btn.classList.remove('active'));
        document.getElementById('animationName').textContent = 'Animation stopped';
    }

    rejectIdea(animationId, buttonText) {
        // Stop the animation if it's currently running
        if (this.currentAnimation && this.currentAnimation.name === animationId) {
            this.stopAnimation();
        }
        
        // Save to localStorage
        let rejectedIds = JSON.parse(localStorage.getItem('rejectedPSIAnimations') || '[]');
        if (!rejectedIds.includes(animationId)) {
            rejectedIds.push(animationId);
            localStorage.setItem('rejectedPSIAnimations', JSON.stringify(rejectedIds));
        }
        
        // Move button to rejected section
        const ideaControls = document.getElementById('ideaControls');
        const containers = ideaControls.getElementsByClassName('idea-button-container');
        
        let containerToMove = null;
        for (let container of containers) {
            const button = container.querySelector('button');
            if (button && button.textContent === buttonText) {
                containerToMove = container;
                break;
            }
        }
        
        if (containerToMove) {
            // Remove the reject button
            const rejectBtn = containerToMove.querySelector('.reject-button');
            if (rejectBtn) {
                rejectBtn.remove();
            }
            
            // Move to rejected section
            const rejectedControls = document.getElementById('rejectedControls');
            rejectedControls.appendChild(containerToMove);
        }
    }

    loadRejectedIdeas() {
        const rejectedIds = JSON.parse(localStorage.getItem('rejectedPSIAnimations') || '[]');
        rejectedIds.forEach(id => {
            const animation = this.registry.get(id);
            if (animation) {
                this.rejectIdea(id, animation.displayName);
            }
        });
    }

    resetRejections() {
        localStorage.removeItem('rejectedPSIAnimations');
        window.location.reload();
    }
}

// Initialize app when DOM is ready
window.addEventListener('DOMContentLoaded', async () => {
    const app = new PSIVisualizerApp();
    window.psiApp = app; // Make available globally for debugging
    
    await app.initialize();
    
    // Set up reset button
    const resetButton = document.getElementById('resetRejectionsBtn');
    if (resetButton) {
        resetButton.onclick = () => app.resetRejections();
    }
});