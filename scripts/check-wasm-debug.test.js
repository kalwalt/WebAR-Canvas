// Tests of check-wasm-debug.js, the CI check that published .wasm files carry no debug information.
// Run with: node --test scripts/check-wasm-debug.test.js
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const { spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const script = path.join(__dirname, 'check-wasm-debug.js');

function leb128(n) {
  const out = [];
  do {
    let byte = n & 0x7f;
    n >>>= 7;
    if (n) byte |= 0x80;
    out.push(byte);
  } while (n);
  return out;
}

// A module with an empty type section, then one custom section per name (with some payload).
function wasm(customNames) {
  const bytes = [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00];
  for (const name of customNames) {
    const nameBytes = [...Buffer.from(name, 'utf8')];
    const body = [...leb128(nameBytes.length), ...nameBytes, 1, 2, 3];
    bytes.push(0x00, ...leb128(body.length), ...body);
  }
  return Buffer.from(bytes);
}

function write(t, name, content) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'webar-canvas-check-wasm-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  const file = path.join(dir, name);
  fs.writeFileSync(file, content);
  return file;
}

function run(...files) {
  return spawnSync(process.execPath, [script, ...files], { encoding: 'utf8' });
}

test('passes a module without debug sections', (t) => {
  const file = write(t, 'ok.wasm', wasm(['target_features']));
  const result = run(file);
  assert.strictEqual(result.status, 0, result.stderr);
  assert.match(result.stdout, /ok\.wasm: no debug information/);
});

test('fails a module with DWARF sections and names them', (t) => {
  const file = write(t, 'dwarf.wasm', wasm(['.debug_info', '.debug_line', 'target_features']));
  const result = run(file);
  assert.strictEqual(result.status, 1);
  assert.match(result.stderr, /dwarf\.wasm/);
  assert.match(result.stderr, /\.debug_info/);
  assert.match(result.stderr, /\.debug_line/);
  assert.match(result.stderr, /--debug/);
});

test('fails a module with a name section or a separate-DWARF link', (t) => {
  assert.strictEqual(run(write(t, 'names.wasm', wasm(['name']))).status, 1);
  assert.strictEqual(run(write(t, 'sep.wasm', wasm(['external_debug_info']))).status, 1);
});

test('checks every file and fails if any one has debug information', (t) => {
  const good = write(t, 'good.wasm', wasm([]));
  const bad = write(t, 'bad.wasm', wasm(['.debug_str']));
  const result = run(good, bad);
  assert.strictEqual(result.status, 1);
  assert.match(result.stdout, /good\.wasm: no debug information/);
  assert.match(result.stderr, /bad\.wasm/);
});

test('fails on a file that is not WebAssembly, or a missing file, or no file', (t) => {
  assert.strictEqual(run(write(t, 'text.wasm', 'not wasm')).status, 2);
  assert.strictEqual(run(path.join(os.tmpdir(), 'webar-canvas-no-such-file.wasm')).status, 2);
  assert.strictEqual(run().status, 2);
});

test('fails on a truncated module instead of passing it', (t) => {
  const full = wasm(['.debug_info']);
  const file = write(t, 'cut.wasm', full.subarray(0, full.length - 4));
  assert.strictEqual(run(file).status, 2);
});
