const esbuild = require('esbuild');
const fs = require('fs');
const path = require('path');

// Ensure dist directory exists
if (!fs.existsSync('dist')) {
    fs.mkdirSync('dist');
}

// Copy HTML file
fs.copyFileSync('src/index.html', 'dist/index.html');

// Copy CSS file
fs.copyFileSync('src/styles.css', 'dist/styles.css');

// Build options
const buildOptions = {
    entryPoints: ['src/app.js'],
    bundle: true,
    outfile: 'dist/bundle.js',
    format: 'iife',
    globalName: 'PSIApp',
    minify: process.argv.includes('--minify'),
    sourcemap: true,
    loader: {
        '.js': 'js',
    },
};

// Check if watch mode
if (process.argv.includes('--watch')) {
    esbuild.context(buildOptions).then(ctx => {
        ctx.watch();
        console.log('Watching for changes...');
    });
} else {
    esbuild.build(buildOptions).then(() => {
        console.log('Build complete!');
        console.log('Open dist/index.html in your browser');
    }).catch(() => process.exit(1));
}