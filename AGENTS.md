# AGENTS.md

Guidance and instructions for AI coding agents (Antigravity, Claude, Copilot, Gemini) working in **WebAR-Canvas**.
This is the canonical instructions file: new guidance and corrections go here. `CLAUDE.md` keeps only Claude Code's day-to-day fast path; `GEMINI.md`, `.agent/instructions.md` and `.github/copilot-instructions.md` are short pointers to this file. Do not copy sections into them.

---

## 1. Project Overview

**WebAR-Canvas** is a high-performance, "C++ first" Augmented Reality framework for the Web. It integrates:
1. **A renderer behind `IARRenderer`**: Google Filament by default (Physically Based Rendering on WebGL 2: camera, scene graph, glTF, the video background quad), or the light `sokol_gfx` backend (`--renderer sokol`; section 2B).
2. **WebARKitLib / jsartoolkitNFT Core**: Natural Feature Tracking (NFT) computer vision library (AR, AR2, KPM / FreakMatcher) compiled directly to WebAssembly with Emscripten.

The entire application lifecycle, computer vision tracking, camera projection calculation, and 3D rendering loop are managed directly in **native C++**, eliminating JavaScript-to-WASM bridge overhead during frame rendering.

---

## 2. Architecture & Design Principles

### A. Pure C++ Native Evolution
- **No Embind in Engine Core**: The C++ engine code does not rely on Emscripten Embind (`<emscripten/bind.h>`) or `emscripten::val` for internal data structures. Matrices, poses, and tracking states are native C++ arrays and structs.
- **RTTI Disabled (`-fno-rtti`)**: Filament is built with `-fno-rtti`. All engine and NFT C++ code must compile cleanly without RTTI.

### B. Renderer Interface
- `ARBaseEngine` draws only through `IARRenderer` (`src/core/IARRenderer.h`: `init`, `setProjection`, `updateVideoTexture`, `render`); it owns the main loop, the CPU video path, the tracker and the profiling probe (#13).
- Two backends, chosen at build time by `WEBAR_RENDERER` (CMake; `node build-docker.js --renderer filament|sokol`, default `filament`):
  - `FilamentRenderer` (`src/core/FilamentRenderer.*`): the WebGL 2 context, the Filament engine, scene, view and camera, the video background, and the render call. The app can add its own Filament entities to its scene.
  - `SokolRenderer` (`src/core/SokolRenderer.*`, `sokol_impl.cpp`): `sokol_gfx` with the GLES3 backend (WebGL 2), hand-written GLSL ES 3.00 shaders, no host tools. With `sokol`, CMake does not configure Filament at all (no submodule, no `matc`, no materials) and forces `WEBAR_CANVAS_GLTF` off; the build goes to `build-wasm-sokol/`.
- Both draw the camera video from the same WebGL texture (`src/core/ARVideoTexture.*`: created once, filled from `<video id="ar-video">` with `texImage2D` on every camera frame, imported by each backend).
- The backend-neutral mesh API (`IARRenderer::addMesh / setMeshTransform / setMeshVisible / removeMesh`; for apps `ARBaseApp::addBox / addCube / addPlane / addSphere`, `attachMeshToMarker`, `setMeshLocalTransform`, `setMeshVisible`, `removeMesh`) covers only unlit, vertex-coloured meshes: what the demo needs. Materials, textures and lighting are deliberately not in it. `ARMeshAnchors` (`src/core/ARMeshAnchors.h`, pure, tested in `core_tests`) binds mesh ids to markers and gives each mesh `anchored * local` (all zeros while lost), reading poses once per camera frame. The demo (`src/app/App.cpp`) uses only this API for the cube, so it is one source for both backends.
- Keep Filament (and sokol) types out of `ARBaseEngine`; renderer-specific code goes in the renderer, and Filament-only app API behind `WEBAR_RENDERER_FILAMENT`.
- Destruction order: `~App` and `~ARBaseApp` run before `~ARBaseEngine` releases the renderer, so the app releases its own entities, glTF assets and anchors first. In `FilamentRenderer`, meshes are destroyed before the vertex-colour material they share, and the engine last. In `SokolRenderer`, `sg_shutdown()` releases every sokol resource, then the injected video texture is deleted with `glDeleteTextures` (sokol never deletes injected textures).
- sokol pitfalls (vendored commit, see `third_party/sokol/README.md`):
  - Raw GL calls (the video upload, `createVideoTexture`) change GL state behind sokol's back: call `sg_reset_state_cache()` after them, before the next sokol call.
  - An injected texture (`sg_image_desc.gl_texture`) keeps its own mip range, but sokol's GL samplers always use a mipmap min filter: set `GL_TEXTURE_MAX_LEVEL` to 0 on a one-level injected texture, or it is incomplete and samples black (no error, not even with `SOKOL_DEBUG`).
  - Shaders sample through views: `sg_make_view(.texture.image)` bound in `sg_bindings.views[]`, declared in `sg_shader_desc.views[]` with a `texture_sampler_pairs[]` entry; on WebGL 2 every uniform and texture-sampler pair needs its `glsl_name`.
  - The validation layer is compiled only with `SOKOL_DEBUG`; Release builds report nothing. Define it temporarily in `sokol_impl.cpp` when a sokol call misbehaves.
- Updating the vendored sokol headers: `git clone --depth 1 https://github.com/floooh/sokol.git` into a folder outside the repository, copy `sokol_gfx.h` and `sokol_log.h` into `third_party/sokol/`, record the commit and date in `third_party/sokol/README.md`, then build with `--renderer sokol --test` and check the sokol demo in the browser (sokol's API changes between commits). Do not use curl or wget for it.

### C. App API (`ARBaseApp`)
- Apps derive from `ARBaseApp` (`src/core/ARBaseApp.h`) and override `setup()` (once, after a successful init), `update(float deltaTime)`, `draw()` (every tick, after `update()`, before the render), `onMarkerFound(int)` and `onMarkerLost(int)`; all are no-ops by default.
- Each tick: on a new camera frame the texture upload, capture, tracking and marker events; then `update()`, `draw()` and the render.
- `update(float deltaTime)` gets the real elapsed time in seconds since the previous tick: 0 on the first tick, at most 0.1 (after a background tab or a debugger pause).
- Marker events fire for every marker index when its tracked state changes, from the raw tracking state (no debounce). `getIsTracking()` / `getModelViewMatrix()` describe marker 0; `getMarkerPose(i, m)` gives any marker's model-view matrix (false and zeros while it is not tracked).
- Anchors: `ARAnchor` (`src/core/ARAnchor.h`, pure C++, tested in `core_tests`) turns a marker pose into an object matrix, centred on the marker (`markerCenterMm` from width, height, dpi) and optionally smoothed (`ARAnchorOptions::smoothing`, off by default; no blending across a loss; applied once per new camera frame, not per display frame). `ARBaseApp::attachMeshToMarker(id, i)` (both backends, through `ARMeshAnchors`) and `ARBaseApp::attachToMarker(entity, i)` (Filament) apply it to an entity before every `update()`, and scales it to 0 while the marker is lost; parent other entities to it to place them relative to the marker.
- Filament parenting pitfall: building a renderable can already give the entity a transform component. Then `tcm.create(entity, parentInstance)` first destroys that component, which moves other instances (possibly the parent's), so the parent is silently not set. Use `if (!tcm.hasComponent(e)) tcm.create(e);` followed by `tcm.setParent(tcm.getInstance(e), tcm.getInstance(parent))`.
- Input: `touchDown/touchMoved/touchUp(x, y, pointerId)`, `mouseDown/mouseUp(x, y, button, pointerId)`, `mouseMoved(x, y, pointerId)`, `windowResized(w, h)` (no-ops by default). `src/core/ARInput.cpp` registers the Emscripten HTML5 callbacks on the canvas (and the window for resize) after a successful init. Coordinates are drawing-buffer pixels: `clientToCanvas()` (`src/core/ARInput.h`, tested) removes the CSS-to-pixel scale and the bars of `object-fit: contain`, and down and move events on the bars are not delivered; up and cancel events are delivered for every touch or button whose down event was, even off the image, clamped to the image's edge (`clientToCanvasClamped()`; `mouseup` is registered on the window). `pointerId` is the browser's touch identifier (only changed touches are reported; `touchcancel` → `touchUp`), 0 for the mouse. `windowResized` reports the window's inner size, once per change.
- Primitives: `createBox(sx, sy, sz, faceColors)`, `createCube(size, color)`, `createPlane(sx, sy, color)`, `createSphere(radius, color, segments, rings)` (segments and rings at most 255: 16-bit indices) (Filament) build renderable entities from `src/core/ARPrimitives.h` (pure, tested: boxes stand on z = 0, planes lie in z = 0, spheres are centred; millimetres) with the unlit vertex-colour material `ar_cube.mat`, and add them to the scene. `ARBaseApp` owns and releases them; they have no transform component, so attach or parent them as needed.
- glTF (`WEBAR_CANVAS_GLTF`, CMake option, default ON; Filament's `gltfio`, so Filament-only for now): `loadModel(path)` loads a `.glb` / `.gltf` from the Emscripten file system at any time after `init()` and returns a handle >= 0 or an `ARModelError` (`src/core/ARModelError.h`: not found, invalid, unsupported (built without glTF), not ready (before init)). `placeModelOnMarker(handle, marker, sizeMm)` stands the model on the marker's centre (glTF +Y up becomes the marker's +Z; the largest extent of the asset's bounding box becomes `sizeMm`, and for a model with morph targets that box includes them); `placeModelOnMarker` and `webar_place_model` fail (false / 0) for an unknown handle or marker. `setModelVisible(handle, bool)`; `getModelEntity(handle)`. The first load adds a default sun and, if the scene has none, a uniform ambient light (glTF materials are lit; the video and the primitives are unlit). Pages call `webar_load_model(path)` and `webar_place_model(handle, marker, sizeMm)` with `Module.ccall`, after writing a file with `Module.FS_createDataFile` (`src/core/ARModel.*`, `ARBaseApp.cpp`). Models embedded at build time go in `CMakeLists.txt` with `--embed-file=<src>@/assets/models/<name>`.
- Configuration is plain data, `ARAppConfig` (`src/core/ARAppConfig.h`): canvas selector, camera frame size, camera parameter file, marker paths. `AR_APP_MAIN(App, w, h, marker)` only fills one; `runApp()` then applies `Module.webarConfig` from the page (`canvasSelector`, `cameraParamPath`, `videoWidth`, `videoHeight`, `markers`) before `init()`. `runApp()` returns 1 and starts no loop if init fails.
- `getEngine()` / `getScene()` / `getCamera()` return Filament objects and exist only with `WEBAR_RENDERER_FILAMENT` (set by `CMakeLists.txt`); an app that uses them is a Filament app. `ARBaseEngine` itself depends only on `IARRenderer`: the backend is created by `ARBaseApp::createRenderer()`.

### D. Dual Video Pipeline
- **GPU Path (Rendering)**: on each camera frame, `update_webgl_texture` (`src/core/ARVideoTexture.cpp`) copies the `<video>` frame into one WebGL texture shared by both backends. Filament draws it on a background quad with `materials/video_background.mat`; sokol with its own GLSL ES 3.00 shader (`SokolRenderer.cpp`).
- **CPU Path (Tracking)**: frames are copied into `ARNFTTracker`'s RGBA buffer (`getVideoFrame()`), converted to luma by `webarkitVideoLuma` into `lumaInfo` (`getVideoLuma()`) and used for KPM feature matching and AR2 template tracking; both buffers are released in `ARNFTTracker::teardown()`.
- **Frame pacing**: both paths run once per new camera frame (`requestVideoFrameCallback`, in `ARBaseEngine::updateVideoFrame()`), not once per display frame; `update()`, `draw()` and the render run every display frame. Without `requestVideoFrameCallback`, or when it has not fired for 250 ms while the video plays, every tick processes the frame.
- **KPM in a Web Worker** (#16): while no marker is tracked, `ARToolKitNFTCore` runs KPM detection on every frame (the 200 ms detection interval only limits re-detection while some markers are tracked and others are not). A pass takes tens of milliseconds, so with `WEBAR_KPM_WORKER` (CMake, on by default) it runs in a Web Worker with its own small WASM module, `videoTest.kpm.js/.wasm` (`src/worker/`, no `-g`), and the main thread only copies the luma frame:
  - `ARNFTTracker` builds the core with `Detector::External`. The core's factory (`ARNFTTracker::makeKpmDetector`) receives the tracker, the core's KPM handle and the resized camera, and returns a `WorkerKpmDetector` (`src/nft/`); `markersAdded()` tells it the markers (all at creation, new batches after). The core recreates the detector after every `setupAR2()`; `KpmWorkerLink`, owned by the tracker, outlives detectors, so the worker is not respawned.
  - Protocol (`WorkerTransport.cpp` on the main side, `src/worker/kpm_worker_main.js` in the worker): `init` (the camera as numbers) → `ready`, `markers` (each marker's `.fset3` bytes from the main instance's file system, with the core's page numbers), `match` (a generation number and the luma frame) → `result`, `error`. Binary payloads are transferred `ArrayBuffer`s; two luma buffers go back and forth. No `SharedArrayBuffer`, so no cross-origin isolation.
  - A pass abandoned by `waitIdle()` or by destroying the detector is dropped by its generation number when its result arrives; `idle()` stays false until a finished pass is collected (the `NFTDetector` contract). `markersAdded()` must not call back into the core.
  - Fallback, final for the session and logged once (`KPM worker unavailable (...)`): when the worker cannot be created, sends no `ready` within 5 s, reports an error, leaves a pass unanswered for 2 s (a frozen page does not count), or the page disables it (`Module.webarConfig.kpmWorker = false`; `?kpmWorker=0` in `src/shell.html`). KPM then runs on the main thread on the core's own KPM handle, as before #16. `Module.webarConfig.kpmWorkerUrl` overrides the worker's URL (default: `videoTest.kpm.js` next to `videoTest.js`).

### E. Camera Projection & Coordinate Alignment
- ARToolKit intrinsic parameters (`camera_para.dat`) are loaded using `arParamLoad()` and resized with `arParamChangeSize()`.
- `arglCameraFrustumRH()` calculates an OpenGL right-handed column-major projection matrix (`cameraLens[16]`).
- This matrix goes to `IARRenderer::setProjection()`: `FilamentRenderer` calls `camera->setCustomProjection(...)`, `SokolRenderer` keeps it as the projection uniform (`projection * transform` per mesh).
- Marker poses (`trans[3][4]`) become column-major 4x4 model-view matrices (`getMarkerPose()`), which `ARAnchor` / `ARMeshAnchors` turn into object transforms.

---

## 3. Directory Layout

```
WebAR-Canvas/
├── assets/                  # Embedded assets (camera_para.dat, pinball.fset, pinball.fset3, pinball.iset)
├── materials/               # Filament source materials (.mat), compiled to .fil with matc (Filament only)
├── src/
│   ├── core/                # Framework core
│   │   ├── ARBaseApp.h/.cpp        # The app base class, AR_APP_MAIN, Module.webarConfig, mesh API
│   │   ├── ARAppConfig.h           # ARAppConfig: what an app needs to start
│   │   ├── AREvents.h              # Marker found/lost event tracking
│   │   ├── ARAnchor.h              # Marker pose -> object matrix, centred, optional smoothing (pure, tested)
│   │   ├── ARInput.h/.cpp          # Touch / mouse / resize -> app hooks; clientToCanvas (pure, tested)
│   │   ├── ARPrimitives.h          # Box / plane / sphere geometry (pure, tested)
│   │   ├── ARMeshAnchors.h         # Mesh ids bound to markers (pure, tested)
│   │   ├── ARModel.h/.cpp          # glTF loading with gltfio (Filament, WEBAR_CANVAS_GLTF)
│   │   ├── ARModelError.h          # loadModel error codes
│   │   ├── ARBaseEngine.h/.cpp     # Main loop, CPU video path, NFT tracker, profiling probe
│   │   ├── IARRenderer.h           # Rendering backend interface (video, projection, meshes)
│   │   ├── ARVideoTexture.h/.cpp   # The camera video texture, shared by both backends
│   │   ├── FilamentRenderer.h/.cpp # Filament backend: WebGL 2 context, scene, video background
│   │   ├── SokolRenderer.h/.cpp    # sokol_gfx backend (WEBAR_RENDERER=sokol)
│   │   └── sokol_impl.cpp          # SOKOL_IMPL: the sokol implementation, compiled once
│   ├── app/                 # The application (user code)
│   │   ├── App.h            # class App : public ARBaseApp (the demo: a cube on the marker; with Filament, a click switches to a glTF model)
│   │   └── App.cpp          # App's hooks: setup(), update(), marker events, input
│   ├── main.cpp             # Entry point: AR_APP_MAIN(App, 640, 480, "/assets/markers/pinball")
│   ├── shell.html           # Emscripten HTML shell template
│   ├── WebARCanvasVersion.h.in  # Version header template (project(VERSION) in CMakeLists.txt)
│   ├── nft/                 # Native C++ NFT tracking subsystem
│   │   ├── ARNFTTracker.h   # C++ NFT tracker interface
│   │   ├── ARNFTTracker.cpp # Adapter over WebARKitLib's ARToolKitNFTCore (RGBA frame + luma here, tracking in the core)
│   │   └── WorkerKpmDetector, KpmWorkerLink, WorkerTransport, KpmTransport.h  # KPM in a Web Worker (#16)
│   └── worker/              # The KPM worker module (videoTest.kpm.js/.wasm): kpm_worker.cpp, kpm_worker_main.js
├── tests/                   # Node runners: nft_tests.cpp (ARNFTTracker, worker detector), core_tests.cpp (pure src/core), kpm_worker_tests.cpp; kpm_worker_browser.{cpp,html} (browser check, built on request)
├── scripts/                 # build-host-tools.sh, build-wasm.sh, test-wasm.sh (run in the container), build-docker.test.js
├── docs/                    # template.md (using the repository as a template), superpowers/plans/ (stage plans)
├── third_party/
│   ├── filament/            # Google Filament submodule (Filament renderer only)
│   ├── sokol/               # sokol_gfx.h, sokol_log.h: vendored, not a submodule (README inside)
│   └── WebARKitLib/         # WebARKitLib submodule (AR, AR2, ARICP, ARUtil, KPM, WebARKitNFT: ARToolKitNFTCore, trackingMod, markerDecompress, NFTMarkerState)
├── .github/workflows/       # build.yml (CI: wasm, wasm-sokol), pages.yml (GitHub Pages)
├── .claude/                 # Claude Code: settings.json, agents/, skills/, hooks/ (section 7)
├── build-wasm/              # Build output, Filament (videoTest.html/.js/.wasm, videoTest.kpm.js/.wasm); not committed
├── build-wasm-sokol/        # Build output, sokol; not committed
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

# Build, then run the Node test runners (nft_tests, core_tests)
node build-docker.js --test

# The sokol renderer: no Filament submodule, no host tools; the fastest way to run the tests
node build-docker.js --renderer sokol --test

# Print the container commands after the checks, without starting Docker
node build-docker.js --renderer sokol --dry-run

# A development build: the app linked with -g, for readable stack traces (never publish it)
node build-docker.js --debug
```

Published and development builds (#23): the default is the published build, which CI tests and Pages deploys, with no debug information. `--debug` sets `WEBAR_DEBUG_INFO=ON` (CMake option, default OFF), which links the app with `-g`: DWARF and the `name` section, so stack traces show function names and source lines, at several times the download size. It goes to the same build folder; switching relinks the app only. The KPM worker module never gets `-g`. `-g` also changes the generated code (Binaryen keeps the DWARF valid by running fewer optimizations, and the JavaScript is not minified), so check a fix in the published build too.

Build times on the maintainer's machine: the first Filament build in a checkout takes ~15–30 min (the host tools; `uberz` alone ~13 min), later ones ~3–10 min; a sokol build ~3 min. Run long builds in the background, and never run two builds on the same build folder (they share its CMake cache). The Node runners use no renderer, so `--renderer sokol --test` checks a change to `src/core/` or `src/nft/` in ~3 min.

`build-docker.js` runs these scripts inside the container, the same ones the CI runs (`.github/workflows/build.yml`):
- `scripts/build-host-tools.sh`: builds the Filament host tools (`matc`, `resgen`, `cmgen`, `glslminifier`, and `uberz` for the ubershader archive that glTF loading needs) in `third_party/filament/out/host` with emsdk's clang, once; it builds only the missing ones. When the list changes, bump the `-vN` suffix of the CI cache key in `.github/workflows/build.yml` (and `pages.yml`).
- `scripts/build-wasm.sh [jobs] [target]`: configures and builds a target (`WebAR-Canvas` by default). Environment: `WEBAR_RENDERER` (`filament` or `sokol`, default `filament`), `BUILD_DIR` (relative to the repository; default `build-wasm/`, or `build-wasm-<renderer>/` for another renderer) and `WEBAR_DEBUG_INFO` (`OFF` or `ON`, default `OFF`; passed on every configure, so a cached `ON` never reaches a published build). With sokol, `build-docker.js` skips `build-host-tools.sh`; `--dry-run` prints the container commands without starting Docker.
- `scripts/test-wasm.sh` (with `--test`): builds and runs three Node runners, with no browser or webcam, and links the worker module; the exit code is the total number of failed checks.
  - `nft_tests` (`tests/nft_tests.cpp`) drives `ARNFTTracker` on WebARKitLib's fixtures (`tests/data`, embedded at `/data`, including `pinball-demo.jpg`), and the worker-backed detector with a fake transport and a loopback transport that runs the worker's entry points in-process. In Node there is no `Worker`, so trackers built with the default transport fall back (one `KPM worker unavailable` line each).
  - `kpm_worker_tests` (`tests/kpm_worker_tests.cpp`) tests the worker module's entry points (`src/worker/kpm_worker.cpp`).
  - `kpm_worker_browser` (built only on request, `cmake --build <dir> --target kpm_worker_browser`; open `tests/kpm_worker_browser.html` next to it) runs the detector through a real worker in a browser.
  - `core_tests` (`tests/core_tests.cpp`) tests the pure C++ parts of `src/core/` (no renderer, no tracker), such as `MarkerEventTracker` and `ARMeshAnchors`.
  - The runners do not use a renderer: `--test` runs the same with either backend. CI runs them in both jobs (`wasm`, and `wasm-sokol` without the Filament submodule).

The Emscripten cache lives in the `webar-canvas-emcache-4.0.17` Docker volume, so the libjpeg and zlib ports are downloaded once.

CI (`.github/workflows/build.yml`) runs on pushes and pull requests to `dev` and `master` (and by hand): the `wasm` job (Filament, host tools cached under the key `filament-host-tools-<filament sha>-emsdk-4.0.17-v2`) and the `wasm-sokol` job, which checks out only WebARKitLib. Both run the Node runners; the runner tests (`node --test scripts/build-docker.test.js .claude/hooks/guard-submodules.test.js`) run first.

GitHub Pages: `.github/workflows/pages.yml` builds the app with the same scripts and deploys it, on the default branch only. The repository is a GitHub template; `docs/template.md` explains how to start a project from it and the Pages settings.

Keep `*.sh` files with LF line endings (`.gitattributes`): they run in Linux containers.

Before starting Docker, `build-docker.js` stops with a clear message when a submodule it needs is not checked out (`third_party/WebARKitLib`; also `third_party/filament` for Filament), the vendored sokol headers are missing (sokol), the renderer is unknown, or the Docker daemon does not answer. Its tests: `node --test scripts/build-docker.test.js` (no Docker needed; also run in CI).

#### Filament host tools: pitfalls
- **Same container, same glibc**: the tools must run in `emscripten/emsdk:4.0.17` (Ubuntu 22.04, glibc 2.35). Tools built on a newer distribution fail with `GLIBC_2.38' not found`; `scripts/build-host-tools.sh` builds them in that image with emsdk's clang and `-DUSE_STATIC_LIBCXX=OFF`.
- **Absolute paths**: `third_party/filament/ImportExecutables-Release.cmake` records the tools' absolute paths, so the WASM build must see the repository at the same path (`/src` in `build-docker.js`, the workspace in CI).
- **Do not set `FILAMENT_LINUX_IS_MOBILE=ON`** for the host build: Filament then skips the tools (`export given target "matc" which is not built by this project`).
- **Worktrees**: a git worktree needs its own submodule checkout. Use `git -c core.autocrlf=false submodule update --init --reference <main checkout>/third_party/<sub> -- third_party/<sub>`, then `git -C third_party/filament config core.autocrlf false`. The built host tools (`third_party/filament/out/host/tools/{matc,resgen,cmgen,glslminifier,uberz}/<tool>`) can be copied from another checkout of the same Filament commit: their import file refers to `/src/...`, not to the host path; `scripts/build-host-tools.sh` then builds only the missing ones and writes a fresh import file.
- **Windows line endings**: with `core.autocrlf=true`, Filament's shell scripts get CRLF (`/usr/bin/env: 'bash\r'`). Check out the Filament submodule with `core.autocrlf=false` in the submodule's own config (README, "Prerequisites & Setup"); never change the global setting. The root `.gitattributes` does not apply inside submodules.

### Key CMake & Linker Requirements
- **Web Ports**: Emscripten port dependencies `-sUSE_ZLIB=1` and `-sUSE_LIBJPEG=1` must be provided in both compile and link options.
- **Embedded Files**: In `CMakeLists.txt`, always format `--embed-file` with an equal sign (`--embed-file=<src>@<dst>`) to prevent CMake from splitting the flag and its destination path during linking.
- **Exports**: a C function called from JavaScript (`Module.ccall`) must be `EMSCRIPTEN_KEEPALIVE` and listed in `EXPORTED_FUNCTIONS` (e.g. `'_webar_load_model'`); a runtime method used from JavaScript must be listed in `EXPORTED_RUNTIME_METHODS` (e.g. `'GL'`, `'UTF8ToString'`, `'ccall'`, `'FS_createDataFile'`). Both lists are in `CMakeLists.txt` and shared by both renderers. With `WEBAR_KPM_WORKER`, `WEBAR_KPM_WORKER_EXPORTS` adds `'_webar_kpm_deliver'`, `'_malloc'` and `'_free'` (called by `WorkerTransport`'s JavaScript) to the app's and `nft_tests`' lists; the worker module (`kpm_worker`) has its own list.

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
Allowed types: `feat`, `fix`, `perf`, `doc`, `refactor`, `test`, `style`, `chore`. There is no `revert` type: a revert gets one of these (e.g. `perf(engine): go back to ...`).

---

## 6. Testing & Verification

1. Compile the WebAssembly build and run the tests using `node build-docker.js --test` (each runner ends with `N checks, 0 failed`). Changes to `ARNFTTracker` need a test in `tests/nft_tests.cpp`; pure C++ logic in `src/core/` gets its tests in `tests/core_tests.cpp`.
2. Start a local HTTP server serving `build-wasm/` (sokol: `build-wasm-sokol/`):
   ```bash
   python -m http.server 8000 --directory build-wasm
   ```
3. Open `http://localhost:8000/videoTest.html` in a WebGL 2 capable browser (Chrome, Edge, Firefox).
4. Verify the DevTools console:
   - WebGL 2 context creation and Filament Engine initialization (sokol: `sokol_gfx initialized`).
   - Camera parameters from `/assets/camera_para.dat` (`*** Camera Parameter ***`, `SIZE = 640, 480`).
   - `setCamera(): arParamLTCreated` (lookup table creation).
   - NFT marker loaded: `NFT marker 0: 893 x 1117 at 120 dpi`.
   - NFT Camera Frustum projection matrix printed in 4x4 column-major format.
   - Smooth continuous frame rendering without WASM runtime exceptions (`Rendered frame` every 120 frames, no `WebGL Error`).

The `/verify-wasm` skill (`.claude/skills/verify-wasm/`) runs this sequence.

### Browser checks
- The built-in browser of the Claude desktop app blocks the camera: feed the page a synthetic source (`canvas.captureStream()` drawing a recorded video or `third_party/WebARKitLib/tests/data/pinball-demo.jpg` (the marker in a scene, also used by `nft_tests`)) instead of `getUserMedia`.
- A hidden or covered window stops `requestAnimationFrame` (Playwright's Chrome drops to 1 Hz): keep the window uncovered and discard runs with too few rAF ticks.
- Compare builds in the same browser, alternating runs; read the pose as the mean over a run (single printed lines jitter by ±4 mm).
- Playwright screenshots take ~4 s and can show a stale frame: confirm a state change over several captures.
- A page that loads `videoTest.js` shares its globals (`run`, `Module`, `FS`): put the page's own code in an IIFE and only set `Module`.

### Profiling the frame pipeline
Set `Module.webarProfile = { enabled: true }` in the page (it is checked on every tick). Every tick then appends a row to `Module.webarProfile.samples`, with the column names in `Module.webarProfile.columns`: texture upload, capture (the JS capture plus the pose read-out; `drawImage`, `getImageData` and the heap copy also on their own), luma, tracking, app update, render, whole tick (milliseconds of main-thread wall time; GPU work is asynchronous and not included), and whether a frame was captured and the marker tracked. Samples accumulate while it is on. Without it, `tick()` runs unprofiled. `ARNFTTracker::getLastUpdateTimings()` gives the luma and tracking times of the last `update()`.

---

## 7. Claude Code Setup (`.claude/`)

- **`agents/wasm-reviewer.md`**: a read-only reviewer (Read, Grep, Glob) that checks a diff against the rules in sections 2 and 4 of this file. Ask for it before a PR.
- **`skills/verify-wasm/`**: `/verify-wasm` runs the checks of section 6 (tests, serve, console lines) for one or both renderers.
- **`settings.json`**:
  - Allows `node build-docker.js`, the runner tests and read-only git commands without a prompt; git's `--output=<file>` (which writes a file) is denied.
  - A `PreToolUse` hook (`hooks/guard-submodules.js`, tested by `hooks/guard-submodules.test.js`) blocks Edit / Write / NotebookEdit under `third_party/filament/` and `third_party/WebARKitLib/`: changes there belong upstream (WebARKitLib) or are build products. `third_party/sokol/` is not blocked, so its update procedure works.
- Personal, machine-specific notes go in `CLAUDE.local.md` at the repository root (git-ignored), not in the shared files.
