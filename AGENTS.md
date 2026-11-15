# AGENTS.md

Guidance and instructions for AI coding agents (Antigravity, Claude, Copilot) working in **WebAR-Canvas (`Wasm_AR_Engine`)**.
This is the canonical instructions file; `CLAUDE.md`, `GEMINI.md`, and `.github/copilot-instructions.md` point here.

---

## 1. Project Overview

**WebAR-Canvas** is a high-performance, "C++ first" Augmented Reality framework for the Web. It integrates:
1. **Google Filament**: Physically Based Rendering (PBR) engine running on WebGL 2, managing the canvas, camera, scene graph, and video stream background quad.
2. **WebARKitLib / jsartoolkitNFT Core**: Natural Feature Tracking (NFT) computer vision library (AR, AR2, KPM / FreakMatcher) compiled directly to WebAssembly with Emscripten.

The entire application lifecycle, computer vision tracking, camera projection calculation, and 3D rendering loop are managed directly in **native C++**, eliminating JavaScript-to-WASM bridge overhead during frame rendering.

---

## 2. Architecture & Design Principles

### A. Pure C++ Native Evolution
- **No Embind in Engine Core**: The C++ engine code does not rely on Emscripten Embind (`<emscripten/bind.h>`) or `emscripten::val` for internal data structures. Matrices, poses, and tracking states are native C++ arrays and structs.
- **RTTI Disabled (`-fno-rtti`)**: Filament is built with `-fno-rtti`. All engine and NFT C++ code must compile cleanly without RTTI.

### B. Renderer Interface
- `ARBaseEngine` draws only through `IARRenderer` (`src/core/IARRenderer.h`: `init`, `setProjection`, `updateVideoTexture`, `render`); it owns the main loop, the CPU video path, the tracker and the profiling probe (#13).
- `FilamentRenderer` (`src/core/FilamentRenderer.*`) is the backend: the WebGL 2 context, the Filament engine, scene, view and camera, the video texture and background quad, and the render call. The app adds its own Filament entities to its scene.
- Keep Filament types out of `ARBaseEngine`; renderer-specific code goes in the renderer.

### C. App API (`ARBaseApp`)
- Apps derive from `ARBaseApp` (`src/core/ARBaseApp.h`) and override `setup()` (once, after a successful init), `update(float deltaTime)`, `draw()` (every tick, after `update()`, before the render), `onMarkerFound(int)` and `onMarkerLost(int)`; all are no-ops by default.
- Each tick: on a new camera frame the texture upload, capture, tracking and marker events; then `update()`, `draw()` and the render.
- `update(float deltaTime)` gets the real elapsed time in seconds since the previous tick: 0 on the first tick, at most 0.1 (after a background tab or a debugger pause).
- Marker events fire for every marker index when its tracked state changes, from the raw tracking state (no debounce). `getIsTracking()` / `getModelViewMatrix()` describe marker 0; `getMarkerPose(i, m)` gives any marker's model-view matrix (false and zeros while it is not tracked).
- Anchors: `ARAnchor` (`src/core/ARAnchor.h`, pure C++, tested in `core_tests`) turns a marker pose into an object matrix, centred on the marker (`markerCenterMm` from width, height, dpi) and optionally smoothed (`ARAnchorOptions::smoothing`, off by default; no blending across a loss). `ARBaseApp::attachToMarker(entity, i)` (Filament) applies it to an entity before every `update()`, and scales it to 0 while the marker is lost; parent other entities to it to place them relative to the marker.
- Filament parenting pitfall: building a renderable can already give the entity a transform component. Then `tcm.create(entity, parentInstance)` first destroys that component, which moves other instances (possibly the parent's), so the parent is silently not set. Use `if (!tcm.hasComponent(e)) tcm.create(e);` followed by `tcm.setParent(tcm.getInstance(e), tcm.getInstance(parent))`.
- Input: `touchDown/touchMoved/touchUp(x, y, pointerId)`, `mouseDown/mouseUp(x, y, button, pointerId)`, `mouseMoved(x, y, pointerId)`, `windowResized(w, h)` (no-ops by default). `src/core/ARInput.cpp` registers the Emscripten HTML5 callbacks on the canvas (and the window for resize) after a successful init. Coordinates are drawing-buffer pixels: `clientToCanvas()` (`src/core/ARInput.h`, tested) removes the CSS-to-pixel scale and the bars of `object-fit: contain`, and events on the bars are not delivered. `pointerId` is the browser's touch identifier (only changed touches are reported; `touchcancel` → `touchUp`), 0 for the mouse. `windowResized` reports the window's inner size, once per change.
- Primitives: `createBox(sx, sy, sz, faceColors)`, `createCube(size, color)`, `createPlane(sx, sy, color)`, `createSphere(radius, color, segments, rings)` (Filament) build renderable entities from `src/core/ARPrimitives.h` (pure, tested: boxes stand on z = 0, planes lie in z = 0, spheres are centred; millimetres) with the unlit vertex-colour material `ar_cube.mat`, and add them to the scene. `ARBaseApp` owns and releases them; they have no transform component, so attach or parent them as needed.
- Configuration is plain data, `ARAppConfig` (`src/core/ARAppConfig.h`): canvas selector, camera frame size, camera parameter file, marker paths. `AR_APP_MAIN(App, w, h, marker)` only fills one; `runApp()` then applies `Module.webarConfig` from the page (`canvasSelector`, `cameraParamPath`, `videoWidth`, `videoHeight`, `markers`) before `init()`. `runApp()` returns 1 and starts no loop if init fails.
- `getEngine()` / `getScene()` / `getCamera()` return Filament objects and exist only with `WEBAR_RENDERER_FILAMENT` (set by `CMakeLists.txt`); an app that uses them is a Filament app. `ARBaseEngine` itself depends only on `IARRenderer`: the backend is created by `ARBaseApp::createRenderer()`.

### D. Dual Video Pipeline
- **GPU Path (Rendering)**: The HTML `<video>` stream is uploaded directly to a WebGL texture via `update_webgl_texture` in `FilamentRenderer.cpp` and rendered as a background quad using an unlit Filament material (`video_background.fil`).
- **CPU Path (Tracking)**: Frames are ingested into CPU memory (`videoFrame` RGBA and `videoLuma` grayscale luminance) for KPM feature matching and AR2 template tracking.
- **Frame pacing**: both paths run once per new camera frame (`requestVideoFrameCallback`, in `ARBaseEngine::updateVideoFrame()`), not once per display frame; `update()`, `draw()` and the render run every display frame. Without `requestVideoFrameCallback`, or when it has not fired for 250 ms while the video plays, every tick processes the frame.
- **KPM on the main thread**: while no marker is tracked, `ARToolKitNFTCore` runs KPM detection on every frame. The 200 ms detection interval only limits re-detection while some markers are tracked and others are not; with a single marker, KPM stops once it is tracked. A KPM pass takes tens of milliseconds, so searching is the slowest state of the app (#4).

### E. Camera Projection & Coordinate Alignment
- ARToolKit intrinsic parameters (`camera_para.dat`) are loaded using `arParamLoad()` and resized with `arParamChangeSize()`.
- `arglCameraFrustumRH()` calculates an OpenGL right-handed column-major projection matrix (`cameraLens[16]`).
- This matrix is passed directly to Filament's camera via:
  ```cpp
  camera->setCustomProjection(customProj, nearPlane, farPlane);
  ```
- Marker poses (`trans[3][4]`) are converted into Filament's column-major 4x4 transform matrices to position 3D entities in the scene.

---

## 3. Directory Layout

```
Wasm_AR_Engine/
├── assets/                  # Embedded assets (camera_para.dat, pinball.fset, pinball.fset3, pinball.iset)
├── materials/               # Source shaders (.mat) compiled to .fil with matc
├── src/
│   ├── core/                # Framework core
│   │   ├── ARBaseApp.h/.cpp        # The app base class, AR_APP_MAIN, Module.webarConfig
│   │   ├── ARAppConfig.h           # ARAppConfig: what an app needs to start
│   │   ├── AREvents.h              # Marker found/lost event tracking
│   │   ├── ARBaseEngine.h/.cpp     # Main loop, CPU video path, NFT tracker, profiling probe
│   │   ├── IARRenderer.h           # Rendering backend interface
│   │   └── FilamentRenderer.h/.cpp # Filament backend: WebGL 2 context, scene, video background
│   ├── app/                 # The application (user code)
│   │   ├── App.h            # class App : public ARBaseApp (the demo: a cube on the marker)
│   │   └── App.cpp          # App's hooks: setup(), update(), marker events
│   ├── main.cpp             # Entry point: AR_APP_MAIN(App, 640, 480, "/assets/markers/pinball")
│   ├── shell.html           # Emscripten HTML shell template
│   └── nft/                 # Native C++ NFT tracking subsystem
│       ├── ARNFTTracker.h   # C++ NFT tracker interface
│       └── ARNFTTracker.cpp # Adapter over WebARKitLib's ARToolKitNFTCore (RGBA frame + luma here, tracking in the core)
├── third_party/
│   ├── filament/            # Google Filament submodule
│   └── WebARKitLib/         # WebARKitLib submodule (AR, AR2, ARICP, ARUtil, KPM, WebARKitNFT: ARToolKitNFTCore, trackingMod, markerDecompress, NFTMarkerState)
├── build-wasm/              # Build directory containing compiled WASM, JS, and HTML
├── build-docker.js          # Cross-platform Node.js build runner for Docker (Windows & Linux)
├── CMakeLists.txt           # Main CMake project build definition
└── README.md                # Project documentation
```

---

## 4. Build System & Compilation

### Standard Build Command
The build environment is pinned to `emscripten/emsdk:4.0.17` and executed via Docker. Use the cross-platform runner:

```bash
# Incremental build
node build-docker.js

# Specify parallel jobs
node build-docker.js -j 8

# Clean rebuild
node build-docker.js --clean

# Build, then run the ARNFTTracker tests with Node
node build-docker.js --test
```

`build-docker.js` runs these scripts inside the container, the same ones the CI runs (`.github/workflows/build.yml`):
- `scripts/build-host-tools.sh`: builds the Filament host tools (`matc`, `resgen`, `cmgen`, `glslminifier`) in `third_party/filament/out/host` with emsdk's clang, once; it skips them when already built.
- `scripts/build-wasm.sh [jobs] [target]`: configures `build-wasm/` and builds a target (`WebAR-Canvas` by default).
- `scripts/test-wasm.sh` (with `--test`): builds and runs two Node runners, with no browser or webcam; the exit code is the total number of failed checks.
  - `nft_tests` (`tests/nft_tests.cpp`) drives `ARNFTTracker` on WebARKitLib's fixtures (`tests/data`, embedded at `/data`, including `pinball-demo.jpg`).
  - `core_tests` (`tests/core_tests.cpp`) tests the pure C++ parts of `src/core/` (no Filament, no tracker), such as `MarkerEventTracker`.

The Emscripten cache lives in the `webar-canvas-emcache-4.0.17` Docker volume, so the libjpeg and zlib ports are downloaded once.

Keep `*.sh` files with LF line endings (`.gitattributes`): they run in Linux containers.

Before starting Docker, `build-docker.js` stops with a clear message when a submodule is not checked out (`third_party/filament`, `third_party/WebARKitLib`) or the Docker daemon does not answer. Its tests: `node --test scripts/build-docker.test.js` (no Docker needed; also run in CI).

#### Filament host tools: pitfalls
- **Same container, same glibc**: the tools must run in `emscripten/emsdk:4.0.17` (Ubuntu 22.04, glibc 2.35). Tools built on a newer distribution fail with `GLIBC_2.38' not found`; `scripts/build-host-tools.sh` builds them in that image with emsdk's clang and `-DUSE_STATIC_LIBCXX=OFF`.
- **Absolute paths**: `third_party/filament/ImportExecutables-Release.cmake` records the tools' absolute paths, so the WASM build must see the repository at the same path (`/src` in `build-docker.js`, the workspace in CI).
- **Do not set `FILAMENT_LINUX_IS_MOBILE=ON`** for the host build: Filament then skips the tools (`export given target "matc" which is not built by this project`).
- **Windows line endings**: with `core.autocrlf=true`, Filament's shell scripts get CRLF (`/usr/bin/env: 'bash\r'`). Check out the Filament submodule with `core.autocrlf=false` in the submodule's own config (README, "Prerequisites & Setup"); never change the global setting. The root `.gitattributes` does not apply inside submodules.

### Key CMake & Linker Requirements
- **Web Ports**: Emscripten port dependencies `-sUSE_ZLIB=1` and `-sUSE_LIBJPEG=1` must be provided in both compile and link options.
- **Embedded Files**: In `CMakeLists.txt`, always format `--embed-file` with an equal sign (`--embed-file=<src>@<dst>`) to prevent CMake from splitting the flag and its destination path during linking.
- **Exported Runtime Methods**: Any method accessed from JavaScript must be explicitly listed in `EXPORTED_RUNTIME_METHODS` (e.g. `'GL'`, `'UTF8ToString'`).

---

## 5. Development Workflow & Git Guidelines

### Branching Strategy
- **Every new branch must start from `dev`**, never from `master`.
- **Every pull request must point to `dev`**.
- `master` is strictly reserved for stable releases.
- Always develop features and fixes on dedicated branches (e.g. `feat/nft-integration`, `fix/camera-frustum`).

```bash
git checkout dev && git pull origin dev
git checkout -b your-branch-name
```

### Releases
- Versions follow [Semantic Versioning](https://semver.org/). The version lives in `project(WebAR-Canvas VERSION x.y.z)` in `CMakeLists.txt`; CMake generates `WebARCanvasVersion.h` (`WEBAR_CANVAS_VERSION_*`) and the engine prints it at startup.
- To release: on a branch from `dev`, bump the version and add its section to `CHANGELOG.md` (`chore: bump version to x.y.z`) and merge it into `dev`; then open a PR from `dev` to `master`, merge it with a merge commit, and tag `vx.y.z` on `master`.

### Commit Messages
Follow [Conventional Commits](https://www.conventionalcommits.org/):
```
<type>(<scope>): <short summary>

- Detailed bullet point 1
- Detailed bullet point 2
```
Allowed types: `feat`, `fix`, `perf`, `doc`, `refactor`, `test`, `style`, `chore`.

---

## 6. Testing & Verification

1. Compile the WebAssembly build and run the tests using `node build-docker.js --test` (each runner ends with `N checks, 0 failed`). Changes to `ARNFTTracker` need a test in `tests/nft_tests.cpp`; pure C++ logic in `src/core/` gets its tests in `tests/core_tests.cpp`.
2. Start a local HTTP server serving `build-wasm/`:
   ```bash
   python -m http.server 8000 --directory build-wasm
   ```
3. Open `http://localhost:8000/videoTest.html` in a WebGL 2 capable browser (Chrome, Edge, Firefox).
4. Verify the DevTools console:
   - WebGL 2 context creation and Filament Engine initialization.
   - Camera parameters from `/assets/camera_para.dat` (`*** Camera Parameter ***`, `SIZE = 640, 480`).
   - `setCamera(): arParamLTCreated` (lookup table creation).
   - NFT marker loaded: `NFT marker 0: 893 x 1117 at 120 dpi`.
   - NFT Camera Frustum projection matrix printed in 4x4 column-major format.
   - Smooth continuous frame rendering without WASM runtime exceptions.

### Profiling the frame pipeline
Set `Module.webarProfile = { enabled: true }` in the page (it is checked on every tick). Every tick then appends a row to `Module.webarProfile.samples`, with the column names in `Module.webarProfile.columns`: texture upload, capture (the JS capture plus the pose read-out; `drawImage`, `getImageData` and the heap copy also on their own), luma, tracking, app update, render, whole tick (milliseconds of main-thread wall time; GPU work is asynchronous and not included), and whether a frame was captured and the marker tracked. Samples accumulate while it is on. Without it, `tick()` runs unprofiled. `ARNFTTracker::getLastUpdateTimings()` gives the luma and tracking times of the last `update()`.
