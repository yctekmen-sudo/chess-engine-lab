// Hash the encompassing WASM artifact before invoking its bundled providers.
const fs = require('node:fs');
const crypto = require('node:crypto');
const child = require('node:child_process');
const runner = process.argv[2];
const artifact = runner.replace(/\.js$/, '.wasm');
const digest = crypto.createHash('sha256').update(fs.readFileSync(artifact)).digest('hex');
console.log(`Bundled evaluator encompassing WASM SHA-256: ${digest}`);
const result = child.spawnSync(process.execPath, [runner, digest], {stdio: 'inherit'});
if (result.error) throw result.error;
process.exit(result.status === null ? 1 : result.status);
