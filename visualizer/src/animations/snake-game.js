export default {
    name: 'snakeGame',
    displayName: 'Snake Game',
    description: 'Automated snake game simulation',
    category: 'ideas',
    interval: 200,
    
    init() {
        return {
            snake: [{x: 5, y: 3}],
            direction: {x: 1, y: 0},
            food: {x: 8, y: 2},
            gameOver: false,
            moveCounter: 0
        };
    },
    
    frame(controller, state) {
        if (state.gameOver) {
            // Flash on game over
            if (state.moveCounter % 2 === 0) {
                for (let i = 0; i < 48; i++) {
                    controller.setLED(i, 'red');
                }
            } else {
                controller.clear();
            }
            state.moveCounter++;
            
            // Reset after a few flashes
            if (state.moveCounter > 10) {
                state.snake = [{x: 5, y: 3}];
                state.direction = {x: 1, y: 0};
                state.food = {x: 8, y: 2};
                state.gameOver = false;
                state.moveCounter = 0;
            }
            return;
        }
        
        // Auto-pilot: Turn towards food
        const head = state.snake[0];
        if (state.moveCounter % 3 === 0) {  // Change direction occasionally
            if (state.food.x !== head.x && Math.random() > 0.3) {
                state.direction = {x: state.food.x > head.x ? 1 : -1, y: 0};
            } else if (state.food.y !== head.y && Math.random() > 0.3) {
                state.direction = {x: 0, y: state.food.y > head.y ? 1 : -1};
            }
        }
        
        // Move snake
        const newHead = {
            x: head.x + state.direction.x,
            y: head.y + state.direction.y
        };
        
        // Wrap around edges
        if (newHead.x < 0) newHead.x = 9;
        if (newHead.x > 9) newHead.x = 0;
        if (newHead.y < 0) newHead.y = 5;
        if (newHead.y > 5) newHead.y = 0;
        
        // Check self collision
        if (state.snake.some(seg => seg.x === newHead.x && seg.y === newHead.y)) {
            state.gameOver = true;
            return;
        }
        
        state.snake.unshift(newHead);
        
        // Check food collision
        if (newHead.x === state.food.x && newHead.y === state.food.y) {
            // Generate new food
            do {
                state.food = {
                    x: Math.floor(Math.random() * 10),
                    y: Math.floor(Math.random() * 6)
                };
            } while (state.snake.some(seg => seg.x === state.food.x && seg.y === state.food.y));
        } else {
            // Remove tail
            state.snake.pop();
        }
        
        // Keep snake length reasonable
        if (state.snake.length > 15) {
            state.snake.pop();
        }
        
        // Render
        controller.clear();
        
        // Draw snake
        state.snake.forEach((seg, i) => {
            const index = controller.getLEDIndex(seg.x, seg.y);
            if (index !== -1) {
                controller.setLED(index, i === 0 ? 'white' : 'green');
            }
        });
        
        // Draw food
        const foodIndex = controller.getLEDIndex(state.food.x, state.food.y);
        if (foodIndex !== -1) {
            controller.setLED(foodIndex, 'red');
        }
        
        state.moveCounter++;
    }
};
