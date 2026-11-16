#!/usr/bin/env node
// PreToolUse hook (.claude/settings.json): blocks Edit / Write / NotebookEdit inside the
// third_party/filament and third_party/WebARKitLib submodules. Changes there belong upstream
// (WebARKitLib) or are build products; third_party/sokol is vendored and stays editable, so its
// update procedure (third_party/sokol/README.md) works. Tests: guard-submodules.test.js.
//
// Input: the tool call as JSON on stdin. Exit 2 with a message on stderr blocks the call;
// exit 0 lets it through (also when the input cannot be read, so a broken payload never blocks
// every edit).
'use strict';

const BLOCKED = ['third_party/filament', 'third_party/webarkitlib'];

function blockedFolder(filePath) {
  // Windows paths ignore case; compare with forward slashes, on whole folder names.
  const normalized = '/' + String(filePath).replace(/\\/g, '/').toLowerCase() + '/';
  return BLOCKED.find((folder) => normalized.includes('/' + folder + '/'));
}

let input = '';
process.stdin.setEncoding('utf8');
process.stdin.on('data', (chunk) => { input += chunk; });
process.stdin.on('end', () => {
  let call;
  try {
    call = JSON.parse(input);
  } catch (e) {
    process.exit(0);
  }
  const toolInput = (call && call.tool_input) || {};
  const filePath = toolInput.file_path || toolInput.notebook_path;
  const folder = filePath && blockedFolder(filePath);
  if (folder) {
    process.stderr.write(
      `Blocked: ${filePath} is inside the ${folder} submodule. Do not edit submodules here: ` +
        'fix it upstream (WebARKitLib) or change this repository instead (AGENTS.md section 7).\n'
    );
    process.exit(2);
  }
  process.exit(0);
});
