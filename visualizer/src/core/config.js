// PSIPro LED matrix layout (-1 means no LED in that position)
export const ledMatrix = [
    [-1, -1, 23, 24, -1, -1],
    [-1,  6, 22, 25, 41, -1],
    [ 5,  7, 21, 26, 40, 42],
    [ 4,  8, 20, 27, 39, 43],
    [ 3,  9, 19, 28, 38, 44],
    [ 2, 10, 18, 29, 37, 45],
    [ 1, 11, 17, 30, 36, 46],
    [ 0, 12, 16, 31, 35, 47],
    [-1, 13, 15, 32, 34, -1],
    [-1, -1, 14, 33, -1, -1]
];

// Pattern matrices from PSIPro code
export const patterns = {
    heart: [
        [0,0,0,0,0,0],
        [0,1,1,0,0,1,1,0],
        [0,1,1,1,1,1,1,1,1,0],
        [0,0,1,1,1,1,1,1,0,0],
        [0,0,1,1,1,1,0,0],
        [0,0,1,1,0,0]
    ],
    letterI: [
        [0,1,1,1,1,0],
        [0,0,0,1,1,0,0,0],
        [0,0,0,0,1,1,0,0,0,0],
        [0,0,0,0,1,1,0,0,0,0],
        [0,0,0,1,1,0,0,0],
        [0,1,1,1,1,0]
    ],
    letterU: [
        [1,1,0,0,1,1],
        [0,1,1,0,0,1,1,0],
        [0,0,1,1,0,0,1,1,0,0],
        [0,0,1,1,0,0,1,1,0,0],
        [0,1,1,1,1,1,1,0],
        [0,1,1,1,1,0]
    ],
    rebel: [
        [0,0,1,1,0,0],
        [1,0,1,1,1,1,0,1],
        [1,0,0,0,1,1,0,0,0,1],
        [1,1,0,1,1,1,1,0,1,1],
        [1,1,1,1,1,1,1,1],
        [1,1,1,1,1,1]
    ],
    pulse: [
        [0,0,0,0,0,1],
        [0,0,0,0,0,1,0,1],
        [0,0,1,0,0,0,1,0,0,1],
        [1,1,0,1,0,1,0,0,0,0],
        [0,0,1,0,1,0,0,0],
        [0,0,1,0,0,0]
    ]
};

// Color mapping for rear PSI
export const rearColorMap = {
    'blue': 'green',
    'red': 'yellow',
    'dim-blue': 'dim-white',
    'dim-red': 'dim-white'
};

// Animation categories
export const categories = {
    core: 'Core Animations',
    patterns: 'Pattern Animations',
    effects: 'Visual Effects',
    interactive: 'Interactive',
    ideas: 'Ideas'
};