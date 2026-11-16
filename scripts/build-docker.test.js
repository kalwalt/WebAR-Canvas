// Tests of build-docker.js's checks before it starts Docker.
// Run with: node --test scripts/build-docker.test.js
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const { spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const buildDocker = path.join(__dirname, '..', 'build-docker.js');

// The files build-docker.js expects from each submodule (the directories CMakeLists.txt adds).
const SUBMODULE_MARKERS = [
  'third_party/filament/CMakeLists.txt',
  'third_party/WebARKitLib/WebARKit/WebARKitTrackers/WebARKitNFT/CMakeLists.txt'
];

function makeTree(t, markers) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'webar-canvas-build-docker-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  for (const marker of markers) {
    fs.mkdirSync(path.dirname(path.join(dir, marker)), { recursive: true });
    fs.writeFileSync(path.join(dir, marker), '');
  }
  return dir;
}

// Every run points Docker at a port where nothing listens, so a broken check can never start
// a real container build from a test.
function runIn(dir) {
  return spawnSync(process.execPath, [buildDocker], {
    cwd: dir,
    env: { ...process.env, DOCKER_HOST: 'tcp://127.0.0.1:1' },
    encoding: 'utf8'
  });
}

test('fails before Docker when the Filament submodule is not checked out', (t) => {
  const dir = makeTree(t, SUBMODULE_MARKERS.slice(1));
  const result = runIn(dir);
  assert.strictEqual(result.status, 1);
  assert.match(result.stderr, /third_party\/filament/);
  assert.match(result.stderr, /git submodule update --init --recursive/);
  assert.doesNotMatch(result.stdout, /Starting Docker build/);
});

test('fails before Docker when the WebARKitLib submodule is not checked out', (t) => {
  const dir = makeTree(t, SUBMODULE_MARKERS.slice(0, 1));
  const result = runIn(dir);
  assert.strictEqual(result.status, 1);
  assert.match(result.stderr, /third_party\/WebARKitLib/);
  assert.match(result.stderr, /git submodule update --init --recursive/);
  assert.doesNotMatch(result.stdout, /Starting Docker build/);
});

test('fails with a clear message when the Docker daemon cannot be reached', (t) => {
  const dir = makeTree(t, SUBMODULE_MARKERS);
  // Nothing listens on port 1: the Docker client fails at once (or is not installed at all).
  const result = runIn(dir);
  assert.strictEqual(result.status, 1);
  assert.match(result.stderr, /Docker is not available/);
  assert.doesNotMatch(result.stdout, /Starting Docker build/);
});

// --- The renderer switch (WEBAR_RENDERER=filament|sokol, #13) ---

const SOKOL_MARKERS = [
  'third_party/WebARKitLib/WebARKit/WebARKitTrackers/WebARKitNFT/CMakeLists.txt',
  'third_party/sokol/sokol_gfx.h'
];

function runWith(dir, args) {
  return spawnSync(process.execPath, [buildDocker, ...args], {
    cwd: dir,
    env: { ...process.env, DOCKER_HOST: 'tcp://127.0.0.1:1' },
    encoding: 'utf8'
  });
}

test('rejects an unknown renderer before Docker', (t) => {
  const dir = makeTree(t, [...SUBMODULE_MARKERS, 'third_party/sokol/sokol_gfx.h']);
  const result = runWith(dir, ['--renderer', 'vulkan']);
  assert.strictEqual(result.status, 1);
  assert.match(result.stderr, /Unknown renderer: vulkan/);
  assert.doesNotMatch(result.stdout, /Starting Docker build/);
});

test('sokol needs no Filament submodule', (t) => {
  const dir = makeTree(t, SOKOL_MARKERS);
  const result = runWith(dir, ['--renderer', 'sokol']);
  // Past the checks: it stops only because Docker cannot be reached.
  assert.strictEqual(result.status, 1);
  assert.doesNotMatch(result.stderr, /third_party\/filament/);
  assert.match(result.stderr, /Docker is not available/);
});

test('sokol fails before Docker without the vendored sokol headers', (t) => {
  const dir = makeTree(t, SOKOL_MARKERS.slice(0, 1));
  const result = runWith(dir, ['--renderer=sokol']);
  assert.strictEqual(result.status, 1);
  assert.match(result.stderr, /third_party\/sokol\/sokol_gfx\.h/);
  assert.doesNotMatch(result.stdout, /Starting Docker build/);
});

test('--dry-run with sokol builds into build-wasm-sokol without the host tools', (t) => {
  const dir = makeTree(t, SOKOL_MARKERS);
  const result = runWith(dir, ['--renderer', 'sokol', '--test', '--dry-run']);
  assert.strictEqual(result.status, 0, result.stderr);
  assert.match(result.stdout, /WEBAR_RENDERER=sokol/);
  assert.match(result.stdout, /BUILD_DIR=\/src\/build-wasm-sokol/);
  assert.match(result.stdout, /test-wasm\.sh/);
  assert.doesNotMatch(result.stdout, /build-host-tools\.sh/);
  assert.doesNotMatch(result.stdout, /Starting Docker build/);
});

test('--dry-run by default keeps the Filament build in build-wasm', (t) => {
  const dir = makeTree(t, SUBMODULE_MARKERS);
  const result = runWith(dir, ['--dry-run']);
  assert.strictEqual(result.status, 0, result.stderr);
  assert.match(result.stdout, /build-host-tools\.sh/);
  assert.match(result.stdout, /WEBAR_RENDERER=filament/);
  assert.match(result.stdout, /BUILD_DIR=\/src\/build-wasm /);
  assert.doesNotMatch(result.stdout, /test-wasm\.sh/);
});
