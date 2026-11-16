---
name: verify-wasm
description: Build WebAR-Canvas, run its Node test runners, serve the build and check it in the browser (console lines, tracking, rendering). Use at the end of a change or a stage, before a PR, or when asked to verify the WebAssembly app. Argument: "filament", "sokol" or "both" (default "both").
---

# Verify the WebAssembly app

The checks of AGENTS.md section 6, for one or both renderers. Report every result with the command that produced it; a step that could not run is reported as not run, never as passed.

## 1. Build and test

For each renderer asked for (`both` = sokol first, then Filament):

```bash
node build-docker.js --renderer sokol --test   # build-wasm-sokol/, ~3 min
node build-docker.js --test                    # build-wasm/, Filament; the first build in a checkout also builds the host tools (~15–30 min)
```

- Run each in the background, one at a time (never two builds on one folder), output to a log outside the repository.
- Pass: exit code 0, and both runners end with `N checks, 0 failed` (`nft_tests`, then `core_tests`). Report the two counts.
- Also run `node --test scripts/build-docker.test.js .claude/hooks/guard-submodules.test.js` (no Docker).

## 2. Serve

Use the project's dev-server entry if `.claude/launch.json` has one for the build folder; otherwise:

```bash
python -m http.server 8000 --directory build-wasm        # or build-wasm-sokol
```

Open `http://localhost:8000/videoTest.html`. Stop the server at the end.

## 3. Check in the browser

The page asks for the camera (`getUserMedia` in `src/shell.html`). The Claude desktop app's built-in browser blocks it: either use a browser with a camera pointed at the printed marker, or replace `navigator.mediaDevices.getUserMedia` before the page starts with one that returns `canvas.captureStream()` of a canvas drawing `third_party/WebARKitLib/tests/data/pinball-demo.jpg` (the marker in a scene, also used by `nft_tests`) or a recorded video. Keep the window uncovered: a covered window stops `requestAnimationFrame`.

Console, in order:
- `WebGL 2 Context created and active`, then `Filament Engine created successfully!` (Filament) or `sokol_gfx initialized` (sokol).
- `*** Camera Parameter ***`, `SIZE = 640, 480`, `setCamera(): arParamLTCreated`.
- `NFT marker 0: 893 x 1117 at 120 dpi`, then the camera frustum matrix.
- `AR 3D Cube added to scene successfully!`
- `Rendered frame N` every 120 frames, and no `WebGL Error` or WASM exception.
- With the marker in view: `>>> NFT Marker 0 found <<<` and `[App] Tracking active! Pose Translation: [...]` lines.

Screen: the video the right way up; the cube on the marker, spinning; on Filament a click switches to the glTF model and back (on sokol a click only prints, and `loadModel` reports -3). Screenshots can lag: confirm a change over several captures.

## 4. Report

One short table per renderer: tests (counts), console lines seen, screen checks, and anything not checked. Clean up what you started (servers, temporary files in the main checkout).
