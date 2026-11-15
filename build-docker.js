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
const runTests = args.includes('--test');
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
console.log(`[build-docker] Run tests:      ${runTests ? 'yes' : 'no (use --test)'}`);

// Checks before Docker starts, so a missing prerequisite fails at once with a clear message
// instead of in the middle of the container build.
function fail(lines) {
  console.error(lines.map((line) => `[build-docker] ${line}`).join('\n'));
  process.exit(1);
}

// The submodule directories that CMakeLists.txt adds.
const submodules = [
  ['third_party/filament', 'CMakeLists.txt'],
  ['third_party/WebARKitLib', 'WebARKit/WebARKitTrackers/WebARKitNFT/CMakeLists.txt']
];
const missingSubmodules = submodules
  .filter(([dir, marker]) => !fs.existsSync(path.join(cwd, dir, marker)))
  .map(([dir]) => dir);
if (missingSubmodules.length > 0) {
  fail([
    `Submodule not checked out: ${missingSubmodules.join(', ')}`,
    'Run this from the repository root, after:',
    '  git submodule update --init --recursive',
    'On Windows, check out Filament with LF line endings first (see README, "Prerequisites").'
  ]);
}

const dockerCheck = spawnSync('docker', ['version', '--format', '{{.Server.Version}}'], {
  encoding: 'utf8'
});
if (dockerCheck.error || dockerCheck.status !== 0) {
  const reason = dockerCheck.error
    ? dockerCheck.error.message
    : (dockerCheck.stderr || '').trim().split('\n').pop();
  fail([
    'Docker is not available: start Docker Desktop (or the Docker engine) and try again.',
    `docker version: ${reason}`
  ]);
}

// Clean build directory if requested
if (isClean) {
  const buildDir = path.join(cwd, 'build-wasm');
  if (fs.existsSync(buildDir)) {
    console.log('[build-docker] Cleaning build-wasm directory...');
    fs.rmSync(buildDir, { recursive: true, force: true });
  }
}

// Build shell commands executed inside the container.
// The host tools and the WASM build run the same scripts as the CI (.github/workflows/build.yml).
// The path bridge keeps host tools built outside the container (e.g. in WSL) usable:
// their ImportExecutables file refers to the host path.
const containerCommands = [
  'apt-get update && apt-get install -y ninja-build',
  `mkdir -p "${bridgePath}/third_party/filament/out/"`,
  `rm -rf "${bridgePath}/third_party/filament/out/host"`,
  `ln -s /src/third_party/filament/out/host "${bridgePath}/third_party/filament/out/host"`,
  'sh /src/scripts/build-host-tools.sh',
  `sh /src/scripts/build-wasm.sh ${jobs}`,
  ...(runTests ? [`sh /src/scripts/test-wasm.sh ${jobs}`] : [])
].join(' && ');

const dockerArgs = [
  'run',
  '--rm',
  '-v', `${cwd}:/src`,
  // Keeps the Emscripten cache (sysroot, libjpeg and zlib ports) across --rm containers,
  // so the ports are downloaded once instead of on every build.
  '-v', `webar-canvas-emcache-4.0.17:/emsdk/upstream/emscripten/cache`,
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
