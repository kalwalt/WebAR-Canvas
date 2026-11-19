#!/usr/bin/env node
// Fails if a .wasm file carries debug information (#23): DWARF (.debug_* sections), a name section,
// or a link to separate DWARF (external_debug_info). CI and the Pages workflow run it on the
// published build, so a -g that slips back into the link options fails the build instead of
// shipping. No dependencies: it reads the module's section headers itself.
//
// Usage: node scripts/check-wasm-debug.js <file.wasm>...
// Exit code: 0 clean, 1 debug information found, 2 a file is missing or not WebAssembly.
'use strict';

const fs = require('fs');
const path = require('path');

const DEBUG_SECTION = /^(\.debug_|name$|external_debug_info$)/;

function readLeb128(bytes, offset) {
  let value = 0;
  let shift = 0;
  for (;;) {
    if (offset >= bytes.length || shift > 28) throw new Error('truncated module');
    const byte = bytes[offset++];
    value += (byte & 0x7f) * 2 ** shift;
    shift += 7;
    if (!(byte & 0x80)) return [value, offset];
  }
}

// The names of a module's custom sections, in order.
function customSections(bytes) {
  const magic = [0x00, 0x61, 0x73, 0x6d];
  if (bytes.length < 8 || magic.some((b, i) => bytes[i] !== b)) throw new Error('not a WebAssembly module');
  const names = [];
  let offset = 8;
  while (offset < bytes.length) {
    const id = bytes[offset++];
    let size;
    [size, offset] = readLeb128(bytes, offset);
    const end = offset + size;
    if (end > bytes.length) throw new Error('truncated module');
    if (id === 0) {
      const [nameLength, nameStart] = readLeb128(bytes, offset);
      if (nameStart + nameLength > end) throw new Error('truncated module');
      names.push(Buffer.from(bytes.subarray(nameStart, nameStart + nameLength)).toString('utf8'));
    }
    offset = end;
  }
  return names;
}

const files = process.argv.slice(2);
if (files.length === 0) {
  console.error('Usage: node scripts/check-wasm-debug.js <file.wasm>...');
  process.exit(2);
}

let status = 0;
for (const file of files) {
  let names;
  try {
    names = customSections(fs.readFileSync(file));
  } catch (e) {
    console.error(`${file}: ${e.code === 'ENOENT' ? 'no such file' : e.message}`);
    status = 2;
    continue;
  }
  const debug = names.filter((name) => DEBUG_SECTION.test(name));
  if (debug.length > 0) {
    console.error(`${file}: debug information in a published build: ${debug.join(', ')}`);
    console.error(`  -g belongs only to development builds (node build-docker.js --debug, WEBAR_DEBUG_INFO=ON).`);
    if (status === 0) status = 1;
  } else {
    console.log(`${path.basename(file)}: no debug information`);
  }
}
process.exit(status);
