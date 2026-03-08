'use strict';

const { spawn } = require('child_process');
const path = require('path');

function main() {
    const sim6502Path = path.resolve(__dirname, '..', 'sim6502');
    const args = process.argv.slice(2);

    const child = spawn(sim6502Path, args, {
        stdio: 'inherit'
    });

    child.on('error', (err) => {
        console.error('Failed to start sim6502:', err);
    });

    child.on('close', (code) => {
        if (code !== 0) {
            console.error(`sim6502 exited with code ${code}`);
        }
    });
}

if (require.main === module) {
    main();
}