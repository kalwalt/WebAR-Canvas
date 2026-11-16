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

const path = require('path');

const BLOCKED = ['third_party/filament', 'third_party/webarkitlib'];

// Forward slashes, "." and ".." resolved, lower case (Windows paths ignore case), and a Git Bash
// drive (/d/...) spelled as a Windows one (d:/...), so both spellings of a folder compare equal.
function canonical(p) {
  let s = String(p).replace(/\\/g, '/');
  const drive = s.match(/^\/([a-zA-Z])\//);
  if (drive) s = drive[1] + ':/' + s.slice(3);
  return path.posix.normalize(s).toLowerCase().replace(/\/+$/, '');
}

function isAbsolute(p) {
  return p.startsWith('/') || /^[a-z]:\//.test(p);
}

function blockedFolder(filePath, projectDir) {
  const file = canonical(filePath);
  if (!projectDir) {
    // Project folder unknown: any whole-folder match counts.
    return BLOCKED.find((folder) => ('/' + file + '/').includes('/' + folder + '/'));
  }
  // Only the submodules at the root of the project count.
  const root = canonical(projectDir);
  let relative;
  if (!isAbsolute(file)) {
    relative = file;
  } else if (file.startsWith(root + '/')) {
    relative = file.slice(root.length + 1);
  } else {
    return undefined; // outside the project
  }
  return BLOCKED.find((folder) => (relative + '/').startsWith(folder + '/'));
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
  const folder = filePath && blockedFolder(filePath, process.env.CLAUDE_PROJECT_DIR);
  if (folder) {
    process.stderr.write(
      `Blocked: ${filePath} is inside the ${folder} submodule. Do not edit submodules here: ` +
        'fix it upstream (WebARKitLib) or change this repository instead (AGENTS.md section 7).\n'
    );
    process.exit(2);
  }
  process.exit(0);
});
