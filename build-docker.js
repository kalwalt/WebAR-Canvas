#!/usr/bin/env node

/**
 * build-docker.js
 * Cross-platform build script for WebAR-Canvas using the Emscripten Docker container.
 * Compatible with Windows, Linux, and macOS.
 */

const { spawnSync } = require('child_process');
const path = require('path');
const os = require('os');
const fs = require('fs');

const DOCKER_IMAGE = 'emscripten/emsdk:4.0.17';
const cwd = process.cwd();

// Parse command line arguments
const args = process.argv.slice(2);
const isClean = args.includes('--clean');
const jobsArgIndex = args.indexOf('-j');
let jobs = 4;
if (jobsArgIndex !== -1 && args[jobsArgIndex + 1]) {
  jobs = parseInt(args[jobsArgIndex + 1], 10) || 4;
} else {
  jobs = Math.max(2, Math.min(os.cpus().length, 8));
}

// Convert local path to WSL/Linux path for the path bridge inside Docker
function getPathBridge(localPath) {
  if (process.platform === 'win32') {
    const drive = localPath.charAt(0).toLowerCase();
    const rest = localPath.slice(2).replace(/\\/g, '/');
    return `/mnt/${drive}${rest}`;
  }
  return localPath.replace(/\\/g, '/');
}

const bridgePath = getPathBridge(cwd);
console.log(`[build-docker] Host directory: ${cwd}`);
console.log(`[build-docker] Path bridge:    ${bridgePath}`);
console.log(`[build-docker] Parallel jobs:  ${jobs}`);

// Clean build directory if requested
if (isClean) {
  const buildDir = path.join(cwd, 'build-wasm');
  if (fs.existsSync(buildDir)) {
    console.log('[build-docker] Cleaning build-wasm directory...');
    fs.rmSync(buildDir, { recursive: true, force: true });
  }
}

// Build shell commands executed inside the container
const containerCommands = [
  'apt-get update && apt-get install -y ninja-build',
  `mkdir -p "${bridgePath}/third_party/filament/out/"`,
  `rm -rf "${bridgePath}/third_party/filament/out/host"`,
  `ln -s /src/third_party/filament/out/host "${bridgePath}/third_party/filament/out/host"`,
  'chmod -R +x /src/third_party/filament/out/host/tools/',
  'mkdir -p /src/build-wasm && cd /src/build-wasm',
  'emcmake cmake -G Ninja .. -DWEBGL=1 -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Emscripten -DMATC_EXEC=/src/third_party/filament/out/host/tools/matc/matc',
  `ninja -j${jobs} WebAR-Canvas`
].join(' && ');

const dockerArgs = [
  'run',
  '--rm',
  '-v', `${cwd}:/src`,
  '-u', '0:0',
  DOCKER_IMAGE,
  'sh',
  '-c',
  containerCommands
];

console.log(`[build-docker] Starting Docker build with ${DOCKER_IMAGE}...`);
const result = spawnSync('docker', dockerArgs, {
  stdio: 'inherit'
});

if (result.error) {
  console.error('[build-docker] Failed to execute docker:', result.error.message);
  process.exit(1);
}

if (result.status !== 0) {
  console.error(`[build-docker] Build failed with exit code ${result.status}`);
  process.exit(result.status);
}

console.log('[build-docker] Build completed successfully! Artifacts in build-wasm/');
