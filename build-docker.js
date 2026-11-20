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
// --dry-run prints the container commands after the checks, without starting Docker.
const dryRun = args.includes('--dry-run');
// --debug links the app with -g (WEBAR_DEBUG_INFO=ON, #23): readable stack traces, but a
// download several times larger. Without it the build is the published one, as in CI and Pages.
const debugInfo = args.includes('--debug');

// --renderer filament|sokol (or --renderer=sokol): the rendering backend (WEBAR_RENDERER, #13).
const RENDERERS = ['filament', 'sokol'];
let renderer = 'filament';
const rendererArgIndex = args.indexOf('--renderer');
const rendererArg = args.find((arg) => arg.startsWith('--renderer='));
if (rendererArgIndex !== -1) {
  renderer = args[rendererArgIndex + 1] || '';
} else if (rendererArg) {
  renderer = rendererArg.slice('--renderer='.length);
}
// Each backend has its own build directory, so the two never share a CMake cache.
const buildDirName = renderer === 'filament' ? 'build-wasm' : `build-wasm-${renderer}`;
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
console.log(`[build-docker] Renderer:       ${renderer} (${buildDirName}/)`);
console.log(`[build-docker] Debug info:     ${debugInfo ? 'yes (-g)' : 'no (use --debug)'}`);

// Checks before Docker starts, so a missing prerequisite fails at once with a clear message
// instead of in the middle of the container build.
function fail(lines) {
  console.error(lines.map((line) => `[build-docker] ${line}`).join('\n'));
  process.exit(1);
}

if (!RENDERERS.includes(renderer)) {
  fail([`Unknown renderer: ${renderer} (use --renderer ${RENDERERS.join(' or ')})`]);
}

// The submodule directories that CMakeLists.txt adds; the sokol backend needs no Filament.
const submodules = [
  ...(renderer === 'filament' ? [['third_party/filament', 'CMakeLists.txt']] : []),
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
// sokol is vendored (third_party/sokol/README.md), not a submodule.
if (renderer === 'sokol' && !fs.existsSync(path.join(cwd, 'third_party/sokol/sokol_gfx.h'))) {
  fail([
    'Missing third_party/sokol/sokol_gfx.h: the sokol headers are part of the repository.',
    'Run this from the repository root (see third_party/sokol/README.md).'
  ]);
}

// Build shell commands executed inside the container.
// The host tools and the WASM build run the same scripts as the CI (.github/workflows/build.yml).
// The path bridge keeps host tools built outside the container (e.g. in WSL) usable:
// their ImportExecutables file refers to the host path. The sokol backend needs no host tools.
// WEBAR_DEBUG_INFO goes to every step: build-wasm.sh passes it to each configure, so switching
// --debug on or off relinks the app in the same build directory.
const buildEnv = `WEBAR_RENDERER=${renderer} BUILD_DIR=/src/${buildDirName} WEBAR_DEBUG_INFO=${debugInfo ? 'ON' : 'OFF'}`;
const containerCommands = [
  'apt-get update && apt-get install -y ninja-build',
  ...(renderer === 'filament'
    ? [
        `mkdir -p "${bridgePath}/third_party/filament/out/"`,
        `rm -rf "${bridgePath}/third_party/filament/out/host"`,
        `ln -s /src/third_party/filament/out/host "${bridgePath}/third_party/filament/out/host"`,
        'sh /src/scripts/build-host-tools.sh'
      ]
    : []),
  `${buildEnv} sh /src/scripts/build-wasm.sh ${jobs}`,
  ...(runTests ? [`${buildEnv} sh /src/scripts/test-wasm.sh ${jobs}`] : [])
].join(' && ');

if (dryRun) {
  console.log(containerCommands.split(' && ').join('\n'));
  process.exit(0);
}

// The timeout keeps a Docker that is still starting, or a remote context waiting for input,
// from hanging the script.
const dockerCheck = spawnSync('docker', ['version', '--format', '{{.Server.Version}}'], {
  encoding: 'utf8',
  timeout: 30000
});
if (dockerCheck.error || dockerCheck.status !== 0) {
  const reason = dockerCheck.error
    ? dockerCheck.error.message
    : (dockerCheck.stderr || '').trim().split('\n').pop() || `exit code ${dockerCheck.status}`;
  fail([
    'Docker is not available: start Docker Desktop (or the Docker engine) and try again.',
    `docker version: ${reason}`
  ]);
}

// Clean build directory if requested
if (isClean) {
  const buildDir = path.join(cwd, buildDirName);
  if (fs.existsSync(buildDir)) {
    console.log(`[build-docker] Cleaning ${buildDirName} directory...`);
    fs.rmSync(buildDir, { recursive: true, force: true });
  }
}

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

console.log(`[build-docker] Build completed successfully! Artifacts in ${buildDirName}/`);
