# Changelog

All notable changes to WebAR-Canvas are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). The version is set by
`project(WebAR-Canvas VERSION ...)` in `CMakeLists.txt`.

## [Unreleased]

### Added

- CI: a GitHub Actions workflow builds the WebAssembly app on every pull request and push to
  `dev` and `master`, and uploads `videoTest.{html,js,wasm}` as an artifact (#9).
- `scripts/build-host-tools.sh` and `scripts/build-wasm.sh`, shared by CI and `build-docker.js`.
- Node test runner `nft_tests` for `ARNFTTracker` (tracking `pinball-demo.jpg`, lost marker,
  out-of-range indices, missing assets, frustum and detection settings), run by
  `node build-docker.js --test` and in CI (#9, #11).

### Changed

- WebARKitLib moves to the head of webarkit/WebARKitLib#94 (`6fd0a25`), which adds
  `ARToolKitNFTCore` (#11).
- `build-docker.js` keeps the Emscripten cache in the `webar-canvas-emcache-4.0.17` Docker
  volume, so the libjpeg and zlib ports are downloaded once.
- `ARNFTTracker` delegates camera, markers, KPM detection, AR2 tracking and pose filtering to
  `ARToolKitNFTCore` (WebARKitLib PR #94); its own code drops from ~480 to ~170 lines. Same
  settings as before: filtering on, 200 ms detection interval, projection planes 10 / 5000 (#11).
- `ARNFTTracker::getMarkerData()` returns `NFTMarkerData` by value.
- `ARNFTTracker::setup()` called again checks the camera with `setCamera()` and tears the
  tracker down when it fails, instead of reporting success with the previous camera.
- `ARNFTTracker::update()` keeps ARToolKit's log at WARN while the core runs: the core logs every
  tracked frame at INFO ("Tracked page ...").

### Removed

- `ARNFTTracker::updateVideoLuma()`, `detectNFTMarker()` and `trackMarkers()`: `update()` runs
  the whole cycle in the core.
- `ARNFTTracker::getSurfaceSet()`, `setFilterParameters()` and the non-const
  `getMarkerState()`: no equivalent in `ARToolKitNFTCore` (#11).

### Fixed

- `build-docker.js` failed on a fresh clone because the Filament host tools were missing: it
  now builds them on the first run (#6).

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

[Unreleased]: https://github.com/kalwalt/WebAR-Canvas/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/kalwalt/WebAR-Canvas/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/kalwalt/WebAR-Canvas/releases/tag/v0.1.0
