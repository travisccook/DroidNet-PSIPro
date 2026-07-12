import { ledMatrix, rearColorMap } from './config.js';

export class LEDController {
    constructor() {
        this.frontLeds = [];
        this.rearLeds = [];
    }

    initialize(frontContainerId, rearContainerId) {
        this.initializeMatrix(frontContainerId, this.frontLeds);
        this.initializeMatrix(rearContainerId, this.rearLeds);
    }

    initializeMatrix(containerId, ledArray) {
        const container = document.getElementById(containerId);
        if (!container) return;
        
        container.innerHTML = '';
        ledArray.length = 0;
        
        for (let row = 0; row < 6; row++) {
            for (let col = 0; col < 10; col++) {
                const led = document.createElement('div');
                const ledIndex = ledMatrix[col][row];
                
                if (ledIndex === -1) {
                    led.className = 'led empty';
                } else {
                    led.className = 'led off';
                    led.id = `${containerId}-led-${ledIndex}`;
                    ledArray[ledIndex] = led;
                }
                
                container.appendChild(led);
            }
        }
    }

    clear() {
        this.frontLeds.forEach(led => {
            if (led) led.className = 'led off';
        });
        this.rearLeds.forEach(led => {
            if (led) led.className = 'led off';
        });
    }

    clearFront() {
        this.frontLeds.forEach(led => {
            if (led) led.className = 'led off';
        });
    }

    clearRear() {
        this.rearLeds.forEach(led => {
            if (led) led.className = 'led off';
        });
    }

    setLED(index, color, target = 'both') {
        if (target === 'both' || target === 'front') {
            if (this.frontLeds[index]) {
                this.frontLeds[index].className = `led ${color}`;
            }
        }
        if (target === 'both' || target === 'rear') {
            if (this.rearLeds[index]) {
                // Map colors for rear PSI
                let rearColor = rearColorMap[color] || color;
                this.rearLeds[index].className = `led ${rearColor}`;
            }
        }
    }

    fillColumn(col, color, target = 'both') {
        for (let row = 0; row < 6; row++) {
            const ledIndex = ledMatrix[col][row];
            if (ledIndex !== -1) {
                this.setLED(ledIndex, color, target);
            }
        }
    }

    fillRow(row, color, target = 'both') {
        for (let col = 0; col < 10; col++) {
            const ledIndex = ledMatrix[col][row];
            if (ledIndex !== -1) {
                this.setLED(ledIndex, color, target);
            }
        }
    }

    displayMatrix(pattern, colors = {0: 'off', 1: 'red', 2: 'blue', 3: 'green', 4: 'white'}, target = 'both') {
        if (target === 'both') {
            this.clear();
        } else if (target === 'front') {
            this.clearFront();
        } else {
            this.clearRear();
        }
        
        // Pattern is stored as a linear array, need to map to our matrix
        for (let i = 0; i < pattern.length; i++) {
            for (let j = 0; j < pattern[i].length; j++) {
                // Map pattern position to actual LED
                const actualCol = j + (pattern[i].length === 6 ? 2 : 
                                       pattern[i].length === 8 ? 1 : 0);
                const actualRow = i;
                
                if (actualCol < 10 && actualRow < 6) {
                    const ledIdx = ledMatrix[actualCol][actualRow];
                    if (ledIdx !== -1 && pattern[i][j] !== 0) {
                        const colorKey = pattern[i][j];
                        this.setLED(ledIdx, colors[colorKey] || 'red', target);
                    }
                }
            }
        }
    }

    // Helper method to get LED position from index
    getLEDPosition(index) {
        for (let col = 0; col < 10; col++) {
            for (let row = 0; row < 6; row++) {
                if (ledMatrix[col][row] === index) {
                    return { col, row };
                }
            }
        }
        return null;
    }

    // Get LED index from position
    getLEDIndex(col, row) {
        if (col >= 0 && col < 10 && row >= 0 && row < 6) {
            return ledMatrix[col][row];
        }
        return -1;
    }
}