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
