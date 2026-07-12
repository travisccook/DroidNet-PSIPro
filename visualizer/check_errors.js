const fs = require('fs');

// Read the HTML file
const html = fs.readFileSync('/Users/travisccook/Library/CloudStorage/GoogleDrive-travisccook@gmail.com/My Drive/Code/Arduino/C2B5/PSIPro/visualizer/PSIvisualizer.html', 'utf8');

// Extract JavaScript content
const scriptMatch = html.match(/<script>([\s\S]*?)<\/script>/);
if (!scriptMatch) {
    console.log('No script tag found');
    process.exit(1);
}

const jsCode = scriptMatch[1];

// Check for common syntax errors
console.log('Checking for common JavaScript issues...\n');

// Check for unmatched braces
const openBraces = (jsCode.match(/{/g) || []).length;
const closeBraces = (jsCode.match(/}/g) || []).length;
console.log(`Braces: { count: ${openBraces}, } count: ${closeBraces}`);
if (openBraces !== closeBraces) {
    console.log('❌ MISMATCH in braces!');
}

// Check for unmatched parentheses
const openParens = (jsCode.match(/\(/g) || []).length;
const closeParens = (jsCode.match(/\)/g) || []).length;
console.log(`Parentheses: ( count: ${openParens}, ) count: ${closeParens}`);
if (openParens !== closeParens) {
    console.log('❌ MISMATCH in parentheses!');
}

// Check for unmatched brackets
const openBrackets = (jsCode.match(/\[/g) || []).length;
const closeBrackets = (jsCode.match(/\]/g) || []).length;
console.log(`Brackets: [ count: ${openBrackets}, ] count: ${closeBrackets}`);
if (openBrackets !== closeBrackets) {
    console.log('❌ MISMATCH in brackets!');
}

// Look for syntax patterns that might cause issues
console.log('\nChecking for problematic patterns...');

// Check for });) pattern
const badPattern = /}\);?\s*\)/g;
const badMatches = jsCode.match(badPattern);
if (badMatches) {
    console.log(`❌ Found ${badMatches.length} instances of problematic });) pattern`);
}

// Check if animations object is properly closed
const animStart = jsCode.indexOf('const animations = {');
if (animStart !== -1) {
    let braceCount = 0;
    let inString = false;
    let stringChar = '';
    let i = animStart;
    
    while (i < jsCode.length && (braceCount > 0 || i === animStart)) {
        const char = jsCode[i];
        
        if (!inString) {
            if (char === '"' || char === "'") {
                inString = true;
                stringChar = char;
            } else if (char === '{') {
                braceCount++;
            } else if (char === '}') {
                braceCount--;
                if (braceCount === 0) {
                    console.log('✓ animations object properly closed at position', i);
                    break;
                }
            }
        } else {
            if (char === stringChar && jsCode[i-1] !== '\\') {
                inString = false;
            }
        }
        i++;
    }
    
    if (braceCount !== 0) {
        console.log('❌ animations object not properly closed!');
    }
}

// Check for undefined variables in critical functions
console.log('\nChecking for undefined variables...');
const undefinedLedPattern = /\bled\./g;
const undefinedMatches = jsCode.match(undefinedLedPattern);
if (undefinedMatches) {
    console.log(`❌ Found ${undefinedMatches.length} references to 'led.' which might be undefined`);
}

// Check initialization
if (jsCode.includes('initializeLEDs()')) {
    console.log('✓ initializeLEDs() is called');
} else {
    console.log('❌ initializeLEDs() is NOT called!');
}

console.log('\nDone checking.');