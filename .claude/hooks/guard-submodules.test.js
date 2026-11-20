// Tests of the PreToolUse hook that blocks edits inside the Filament and WebARKitLib submodules.
// Run with: node --test .claude/hooks/guard-submodules.test.js  (also run in CI)
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const { spawnSync } = require('child_process');
const path = require('path');

const hook = path.join(__dirname, 'guard-submodules.js');

// Runs the hook as Claude Code does: the tool call as JSON on stdin. These cases run without a
// project folder (CLAUDE_PROJECT_DIR empty), where any whole-folder match counts.
function run(toolName, toolInput) {
  return spawnSync(process.execPath, [hook], {
    input: JSON.stringify({ hook_event_name: 'PreToolUse', tool_name: toolName, tool_input: toolInput }),
    env: { ...process.env, CLAUDE_PROJECT_DIR: '' },
    encoding: 'utf8'
  });
}

const blocked = [
  ['Edit', { file_path: 'D:\\kalwalt-github\\WebAR-Canvas\\third_party\\filament\\CMakeLists.txt' }],
  ['Write', { file_path: '/src/third_party/filament/libs/utils/src/Log.cpp' }],
  ['Edit', { file_path: 'D:/kalwalt-github/WebAR-Canvas/third_party/WebARKitLib/WebARKit/foo.cpp' }],
  ['Edit', { file_path: 'd:\\x\\Third_Party\\webarkitlib\\lib\\a.c' }], // Windows paths ignore case
  ['NotebookEdit', { notebook_path: '/repo/third_party/filament/docs/x.ipynb' }]
];

for (const [tool, input] of blocked) {
  test(`blocks ${tool} on ${input.file_path || input.notebook_path}`, () => {
    const result = run(tool, input);
    assert.strictEqual(result.status, 2);
    assert.match(result.stderr, /submodule/);
  });
}

const allowed = [
  ['Edit', { file_path: 'D:\\kalwalt-github\\WebAR-Canvas\\third_party\\sokol\\sokol_gfx.h' }],
  ['Write', { file_path: '/repo/third_party/sokol/README.md' }],
  ['Edit', { file_path: '/repo/src/core/ARBaseApp.cpp' }],
  ['Edit', { file_path: '/repo/docs/third_party/filament-notes.md' }], // not the submodule folder
  ['Edit', { file_path: '/repo/third_party/filament-extra/a.cpp' }], // a different folder name
  ['Write', {}] // no path: nothing to judge
];

for (const [tool, input] of allowed) {
  test(`allows ${tool} on ${input.file_path || '(no path)'}`, () => {
    const result = run(tool, input);
    assert.strictEqual(result.status, 0, result.stderr);
  });
}

// With the project folder known (Claude Code sets CLAUDE_PROJECT_DIR), only the submodules at
// the repository root count, and "." / ".." segments cannot hide one.
function runIn(projectDir, filePath) {
  return spawnSync(process.execPath, [hook], {
    input: JSON.stringify({ tool_name: 'Edit', tool_input: { file_path: filePath } }),
    env: { ...process.env, CLAUDE_PROJECT_DIR: projectDir },
    encoding: 'utf8'
  });
}

test('allows a third_party/filament folder below the root of the project', () => {
  assert.strictEqual(runIn('/repo', '/repo/docs/third_party/filament/notes.md').status, 0);
});

test('blocks a submodule path with "." or ".." segments', () => {
  assert.strictEqual(runIn('/repo', '/repo/third_party/./filament/x.cpp').status, 2);
  assert.strictEqual(runIn('/repo', '/repo/src/../third_party/WebARKitLib/x.cpp').status, 2);
});

test('blocks a path relative to the project', () => {
  assert.strictEqual(runIn('/repo', 'third_party/filament/x.cpp').status, 2);
});

test('matches Windows and Git Bash spellings of the project folder', () => {
  assert.strictEqual(runIn('D:\\kalwalt\\repo', 'D:\\kalwalt\\repo\\third_party\\filament\\x.cpp').status, 2);
  assert.strictEqual(runIn('/d/kalwalt/repo', 'D:\\kalwalt\\repo\\third_party\\filament\\x.cpp').status, 2);
  assert.strictEqual(runIn('D:\\kalwalt\\repo', 'D:\\kalwalt\\repo\\docs\\third_party\\filament\\x.md').status, 0);
});

test('allows files outside the project', () => {
  assert.strictEqual(runIn('/repo', '/other/third_party/filament/x.cpp').status, 0);
});

test('allows malformed input instead of blocking every edit', () => {
  const result = spawnSync(process.execPath, [hook], { input: 'not json', encoding: 'utf8' });
  assert.strictEqual(result.status, 0);
});
