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

### B. Dual Video Pipeline
- **GPU Path (Rendering)**: The HTML `<video>` stream is uploaded directly to a WebGL texture via `update_webgl_texture` in `ARBaseEngine.cpp` and rendered as a background quad using an unlit Filament material (`video_background.fil`).
- **CPU Path (Tracking)**: Frames are ingested into CPU memory (`videoFrame` RGBA and `videoLuma` grayscale luminance) for KPM feature matching and AR2 template tracking.

### C. Camera Projection & Coordinate Alignment
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
│   ├── ARBaseEngine.h       # Base engine header (Filament core + NFT tracker integration)
│   ├── ARBaseEngine.cpp     # Base engine implementation (WebGL context, background quad, main loop)
│   ├── main.cpp             # WebARApp application implementation & entry point
│   ├── shell.html           # Emscripten HTML shell template
│   └── nft/                 # Native C++ NFT tracking subsystem
│       ├── ARNFTTracker.h   # C++ NFT tracker interface
│       ├── ARNFTTracker.cpp # C++ NFT tracker implementation (loadCamera, setup, setupAR2)
│       ├── trackingMod.h/.c # Web-optimized cooperative AR2 tracking
│       ├── trackingMod2d.c  # 2D template tracking routines
│       ├── markerDecompress.h/.c # Marker decompression (.zft)
│       └── NFTMarkerState.h # Tracking state struct per marker
├── third_party/
│   ├── filament/            # Google Filament submodule
│   └── WebARKitLib/         # WebARKitLib submodule (AR, AR2, ARICP, ARUtil, KPM)
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
```

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

1. Compile the WebAssembly build using `node build-docker.js`.
2. Start a local HTTP server serving `build-wasm/`:
   ```bash
   python -m http.server 8000 --directory build-wasm
   ```
3. Open `http://localhost:8000/videoTest.html` in a WebGL 2 capable browser (Chrome, Edge, Firefox).
4. Verify the DevTools console:
   - WebGL 2 context creation and Filament Engine initialization.
   - Camera parameters loaded from `/assets/camera_para.dat`.
   - `setCamera()` lookup table creation.
   - NFT Camera Frustum projection matrix printed in 4x4 column-major format.
   - Smooth continuous frame rendering without WASM runtime exceptions.
