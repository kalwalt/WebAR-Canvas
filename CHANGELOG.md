# Changelog

All notable changes to WebAR-Canvas are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). The version is set by
`project(WebAR-Canvas VERSION ...)` in `CMakeLists.txt`.

## [Unreleased]

## [0.3.0] - 2026-11-20

### Added

- **A framework API for apps** (#2, #17, #18):
  - `ARBaseApp`, with `setup()`, `update(float deltaTime)` (the real elapsed time), `draw()`,
    `onMarkerFound(int)` and `onMarkerLost(int)`, for every marker index. The demo app lives in
    `src/app/`, the framework in `src/core/`.
  - `ARAppConfig` and `AR_APP_MAIN(App, w, h, marker)`; a page can override the canvas, the
    camera parameters, the frame size and the markers with `Module.webarConfig`.
  - `ARAnchor`: objects bound to a marker, centred on it, with optional smoothing;
    `attachToMarker(entity, i)` for Filament entities (#18) and `attachMeshToMarker(id, i)` for
    the mesh API on both renderers (#19).
  - Input: touch, mouse and resize events in canvas pixels (`touchDown`, `mouseDown`,
    `windowResized`, ...).
  - Primitives: boxes, cubes, planes and spheres (`createBox`, `createCube`, `createPlane`,
    `createSphere` with Filament; the mesh API below on both renderers).
  - glTF models through Filament's `gltfio` (`loadModel()`, `placeModelOnMarker()`; from a page
    with `webar_load_model` / `webar_place_model`), with the `uberz` host tool. Filament only;
    `-DWEBAR_CANVAS_GLTF=OFF` builds without it.
- **A second renderer, sokol_gfx** (#13, #17, #19): `IARRenderer` separates the engine from the
  renderer, and `WEBAR_RENDERER=sokol` (`node build-docker.js --renderer sokol`) builds a lighter
  app that needs neither the Filament submodule nor its host tools. A backend-neutral mesh API
  (`addBox`, `addCube`, `addPlane`, `addSphere`, ...) works on both.
- **KPM marker detection in a Web Worker** (#16, #22): while searching for a marker, the main
  thread's tick drops from about 120 ms to under 1 ms on a desktop webcam, and from about 400 ms to
  2 ms on a mid-range Android phone. The worker is its own small module,
  `videoTest.kpm.js/.wasm`, deployed next to `videoTest.js`. The app falls back to the main
  thread when the worker cannot run. `?kpmWorker=0` (`Module.webarConfig.kpmWorker = false`)
  turns it off at runtime, `Module.webarConfig.kpmWorkerUrl` points at another location, and
  `-DWEBAR_KPM_WORKER=OFF` builds the app without it.
- **The repository as a template, published on GitHub Pages** (#18): `.github/workflows/pages.yml`
  builds and deploys the app from the default branch; `docs/template.md` explains how to start a
  project from it.
- **Profiling** (#4, #15): `ARNFTTracker::getLastUpdateTimings()` (luma and tracking time), and
  an opt-in frame timing probe, `Module.webarProfile`.
- `node build-docker.js --debug` (`WEBAR_DEBUG_INFO`) for a development build with C++ function
  names in stack traces (#23, #25).
- CI: a GitHub Actions workflow builds the WebAssembly app on every pull request and push to
  `dev` and `master`, with both renderers, runs the tests and uploads the app as an artifact
  (#9, #10, #19).
- `scripts/build-host-tools.sh`, `scripts/build-wasm.sh` and `scripts/test-wasm.sh`, shared by CI
  and `build-docker.js`; `--dry-run` prints the container commands without Docker.
- Node test runners, run by `node build-docker.js --test` and in CI: `nft_tests` for
  `ARNFTTracker` (tracking `pinball-demo.jpg`, lost marker, out-of-range indices, missing assets,
  frustum and detection settings, the worker-backed detector), `core_tests` for the framework's
  pure C++ parts and `kpm_worker_tests` for the worker module (#9, #11, #16, #17); and
  `node --test` checks of `build-docker.js` and `scripts/check-wasm-debug.js` (#14, #25).
- Agent setup: `AGENTS.md` as the single guide, a `wasm-reviewer` agent, a `/verify-wasm` skill
  and a hook that protects the submodules (#20).

### Changed

- **Breaking: apps derive from `ARBaseApp`** (#17). Moving an app from 0.2.0:
  - `ARBaseEngine::init(canvasId, width, height, ...)` became `init(const ARAppConfig&)`, and an
    app usually starts with `AR_APP_MAIN` instead of calling it;
  - the pure virtual `setup(Engine*, Scene*)` became `setup()` with no arguments; a Filament app
    gets the engine and scene from `getEngine()` / `getScene()`;
  - `update(float deltaTime)` gets the real elapsed time in seconds instead of a constant 0.016,
    so animations written for 0.2.0 change speed on displays faster or slower than 60 Hz;
  - the example class `WebARApp` in `src/main.cpp` became `App` in `src/app/`.
- **Published builds carry no debug information** (#23, #25): `videoTest.wasm` is about 1.8 MB
  gzipped with Filament instead of 2.9 MB with `-g`, and about 0.8 MB with the new sokol renderer. CI and the Pages
  workflow fail if a published `.wasm` has debug sections (`scripts/check-wasm-debug.js`).
- **Each camera frame is processed once** (#4, #15): the texture upload, the frame capture and
  the tracking run when the `<video>` presents a new frame (`requestVideoFrameCallback`), not on
  every display frame; `update()`, `draw()` and the render still run every display frame.
- WebARKitLib 0.10.0 → 0.13.0 (#11, #12, #22). 0.11.0 adds `ARToolKitNFTCore`
  (webarkit/WebARKitLib#94) with fixes from its review: `setup()` validates the frame size and
  frees a camera it cannot apply, separate include guards for the two `WebARKitLog.h`, KPM memory
  and thread fixes. 0.13.0 adds the consumer-supplied KPM detector the worker uses
  (webarkit/WebARKitLib#116) and recreates the detector in `setupAR2()`
  (webarkit/WebARKitLib#124).
- `ARNFTTracker` delegates camera, markers, KPM detection, AR2 tracking and pose filtering to
  `ARToolKitNFTCore`; `ARNFTTracker.cpp` drops from ~480 lines to ~190 (#12), ~250 with the KPM
  worker wiring (#22). Same settings as before:
  filtering on, 200 ms detection interval, projection planes 10 / 5000 (#11, #12).
- `ARNFTTracker::getMarkerData()` returns `NFTMarkerData` by value.
- `ARNFTTracker::setup()` returns -1 and tears the tracker down when the frame size is not
  positive or the camera cannot be applied, also when it is called again, instead of reporting
  success with the previous camera or frame.
- `ARNFTTracker::update()` keeps ARToolKit's log at WARN while the core runs: the core logs every
  tracked frame at INFO ("Tracked page ...").
- The engine moved to `src/core/` and the demo app to `src/app/` (#17).
- README: the reference marker image, fixed links and both renderers named (#21).
- `build-docker.js` keeps the Emscripten cache in the `webar-canvas-emcache-4.0.17` Docker
  volume, so the libjpeg and zlib ports are downloaded once.

### Removed

- `ARNFTTracker::updateVideoLuma()`, `detectNFTMarker()` and `trackMarkers()`: `update()` runs
  the whole cycle in the core.
- `ARNFTTracker::getSurfaceSet()`, `setFilterParameters()` and the non-const
  `getMarkerState()`: no equivalent in `ARToolKitNFTCore` (#11).

### Fixed

- `build-docker.js` failed on a fresh clone because the Filament host tools were missing: it
  now builds them on the first run (#6, #10). Before starting Docker, it now also stops with a
  clear message when a submodule it needs is not checked out or Docker does not answer (#6, #14).
- On Windows, the Filament submodule needs LF line endings: README and AGENTS.md say how (#6,
  #14).

## [0.2.0] - 2026-10-11

### Added

- NFT (Natural Feature Tracking) of image markers with `ARNFTTracker`: camera parameters,
  marker datasets (`.iset` / `.fset` / `.fset3`), KPM detection and AR2 tracking on RGBA frames
  captured from the webcam (#1).
- AR camera projection computed from the NFT camera parameters.
- A rotating 3D cube anchored to the `pinball` marker, as the example app (`WebARApp` in
  `src/main.cpp`).
- `build-docker.js`: one build command for Windows, Linux and macOS, using the
  `emscripten/emsdk:4.0.17` container.
- Version metadata: `project(VERSION)` in CMake, a generated `WebARCanvasVersion.h`
  (`WEBAR_CANVAS_VERSION_*`), the version printed at startup, and this changelog.
- LGPL-3.0 `LICENSE` and agent instruction files (`AGENTS.md`, `CLAUDE.md`, `GEMINI.md`,
  Copilot).

### Changed

- The video background is drawn in device space (`vertexDomain : device`), independent of the
  AR camera projection.
- The NFT tracking helpers (`trackingMod`, `markerDecompress`) and the ARToolKit sources (AR,
  AR2, KPM, ARUtil) now come from the `WebARKitNFT` target of WebARKitLib 0.10.0 instead of
  local copies (#5, closes #3).

### Fixed

- The webcam video was drawn upside down: `video_background.mat` flipped V a second time on top
  of Filament's default `flipUV`.

## [0.1.0] - 2026-02-20

### Added

- `ARBaseEngine`: the application lifecycle in C++, rendering with Google Filament on a WebGL 2
  context created by the engine, driven by the Emscripten main loop.
- Webcam video background through a WebGL texture imported into Filament.
- CMake build for WebAssembly, with Filament materials compiled by `matc` and embedded in the
  binary.

[Unreleased]: https://github.com/kalwalt/WebAR-Canvas/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/kalwalt/WebAR-Canvas/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/kalwalt/WebAR-Canvas/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/kalwalt/WebAR-Canvas/releases/tag/v0.1.0
