# WebAR-Canvas

<p align="center">
  <img src="https://raw.githubusercontent.com/kalwalt/WebAR-Canvas/dev/assets/pinball.jpg" alt="WebAR-Canvas Banner" width="280" />
</p>

<p align="center">
  <strong>High-performance, pure C++ Augmented Reality framework for the Web</strong><br />
  Powered by <strong>Google Filament</strong> (PBR Rendering) and <strong>WebARKitLib</strong> (Natural Feature Tracking).
</p>

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/17"><img src="https://img.shields.io/badge/C%2B%2B-17-00599C?logo=c%2B%2B" alt="C++17" /></a>
  <a href="https://webassembly.org/"><img src="https://img.shields.io/badge/WebAssembly-WASM-654FF0?logo=webassembly&logoColor=white" alt="WebAssembly" /></a>
  <a href="https://emscripten.org/"><img src="https://img.shields.io/badge/Emscripten-4.0.17-orange" alt="Emscripten 4.0.17" /></a>
  <a href="https://github.com/google/filament"><img src="https://img.shields.io/badge/Renderer-Google_Filament-4285F4?logo=google" alt="Google Filament" /></a>
  <a href="https://www.khronos.org/webgl/"><img src="https://img.shields.io/badge/WebGL-2.0-990000?logo=webgl&logoColor=white" alt="WebGL 2.0" /></a>
  <a href="https://www.gnu.org/licenses/lgpl-3.0"><img src="https://img.shields.io/badge/License-LGPL_v3-blue.svg" alt="License: LGPL v3" /></a>
  <a href="https://github.com/kalwalt/WebAR-Canvas/pulls"><img src="https://img.shields.io/badge/PRs-welcome-brightgreen.svg" alt="PRs Welcome" /></a>
</p>

---

## 🌟 Overview

**WebAR-Canvas** is a "C++ first" Augmented Reality engine compiled directly to WebAssembly. Unlike traditional web AR solutions where computer vision tracking runs in WASM while rendering runs across a JavaScript bridge (e.g. Three.js / Babylon.js), **WebAR-Canvas executes the entire tracking, matrix mathematics, scene graph, and rendering pipeline inside native C++**.

By running Google Filament and WebARKitLib side-by-side in native code, WebAR-Canvas eliminates bridge serialization overhead, minimizes garbage collection pauses, and delivers high-performance 6-DOF tracking with Physically Based Rendering (PBR).

---

## 🚀 Key Features

- **Pure C++ Architecture**: Zero Embind overhead inside the engine core. Matrices, tracking states, and application lifecycle are handled directly in native C++.
- **Google Filament WebGL 2**: Modern Physically Based Rendering (PBR) engine managing camera projection, lights, materials, and scene graph.
- **Natural Feature Tracking (NFT)**: Native integration of WebARKitLib (AR, AR2, KPM / FreakMatcher), supporting planar image markers without QR codes or black borders.
- **Dual Video Pipeline**:
  - **GPU Path**: Streams webcam frames directly to a WebGL texture rendered on an unlit full-screen quad in normalized device coordinates (`vertexDomain: device`).
  - **CPU Path**: Fast RGBA frame capture and persistent grayscale luminance conversion (`WebARKitVideoLuma`) for point matching and template tracking.
- **Precise Coordinate Alignment**: ARToolKit calibration intrinsics (`camera_para.dat`) compute an exact right-handed OpenGL frustum (`arglCameraFrustumRH`) hooked directly into Filament's `camera->setCustomProjection()`, aligned with marker poses converted via `arglCameraViewRH`.
- **Responsive 1:1 Viewport**: Auto-resizing WebGL drawing buffer and CSS `object-fit: contain` preserve exact 4:3 camera proportions across any screen size.
- **Cross-Platform Toolchain**: Containerized compilation using Docker (`build-docker.js`) for seamless builds on Windows, Linux, and macOS.

---

## 🏛 Architecture

```
                  ┌───────────────────────────────────────────────┐
                  │              HTML5 <video> Stream             │
                  └───────────────┬───────────────────────────────┘
                                  │
                  ┌───────────────┴───────────────┐
                  ▼                               ▼
       [ GPU Rendering Path ]           [ CPU Tracking Path ]
    update_webgl_texture()           capture_video_frame()
                  │                               │
                  ▼                               ▼
        Native WebGL Texture            RGBA Frame Buffer
                  │                               │
                  ▼                               ▼
    Filament External Texture        WebARKitVideoLuma (Grayscale)
                  │                               │
                  ▼                               ▼
    Fullscreen Background Quad       KPM Point Feature Recognition
       (vertexDomain: device)                     │
                  │                               ▼
                  │                  AR2 Surface Template Tracking
                  │                               │
                  │                               ▼
                  │                  arFilterTransMat (Low-Pass Filter)
                  │                               │
                  ▼                               ▼
            Filament Scene  ◄───────────  modelViewMatrix (Pose)
                  │
                  ▼
         Renderer::render()
```

---

## 📁 Directory Structure

```
WebAR-Canvas/
├── assets/                     # Embedded runtime assets
│   ├── camera_para.dat         # Camera intrinsic calibration file
│   ├── pinball.fset            # AR2 multi-resolution surface template dataset
│   ├── pinball.fset3           # KPM FREAK point feature descriptor dataset
│   ├── pinball.iset            # Compressed multi-scale image pyramid dataset
│   └── pinball.jpg             # Reference marker image for testing
├── materials/                  # Filament source shaders (.mat)
│   ├── video_background.mat    # Fullscreen device-domain video quad shader
│   └── ar_cube.mat             # Unlit vertex-colored 3D entity shader
├── src/                        # Native C++ source code
│   ├── ARBaseEngine.h/.cpp     # Core engine: WebGL context, dual pipeline, main loop
│   ├── main.cpp                # WebARApp application entry point & 3D AR scene
│   ├── shell.html              # Emscripten HTML shell template
│   └── nft/                    # Native C++ NFT tracking subsystem
│       ├── ARNFTTracker.h/.cpp # C++ tracker interface (camera, handles, tracking loop)
│       ├── trackingMod.h/.c    # Cooperative AR2 frame tracking
│       ├── trackingMod2d.c     # 2D template tracking routines
│       ├── markerDecompress.h/.c # Marker decompression (.zft)
│       └── NFTMarkerState.h    # Per-marker tracking state struct
├── third_party/
│   ├── filament/               # Google Filament submodule
│   └── WebARKitLib/            # WebARKitLib submodule (AR, AR2, ARICP, KPM)
├── build-wasm/                 # Compiled output directory (HTML, JS, WASM)
├── build-docker.js             # Cross-platform Node.js build runner for Docker
├── CMakeLists.txt              # CMake build definition
└── AGENTS.md                   # Canonical development rules & instructions
```

---

## ⚙️ Prerequisites & Setup

### 1. Clone Repository & Submodules
```bash
git clone --recursive https://github.com/kalwalt/WebAR-Canvas.git
cd WebAR-Canvas
```

If you previously cloned without `--recursive`:
```bash
git submodule update --init --recursive
```

### 2. Docker & Node.js
Building WebAR-Canvas requires:
- **Docker Desktop** (or Docker engine on Linux).
- **Node.js** (v16+).

---

## 🔨 Building the Project

The WebAssembly build environment is containerized in `emscripten/emsdk:4.0.17`. Use the included cross-platform build runner:

```bash
# Standard incremental build
node build-docker.js

# Parallel compilation with custom job count (e.g. 8 jobs)
node build-docker.js -j 8

# Full clean rebuild
node build-docker.js --clean
```

### Build Artifacts
Upon completion, the following artifacts are generated in `build-wasm/`:
- `videoTest.html`: The ready-to-run HTML shell.
- `videoTest.js`: Emscripten JavaScript runtime glue.
- `videoTest.wasm`: The compiled native C++ engine binary.

---

## 🖥 Running & Testing

Due to browser security policies regarding webcam access (`getUserMedia`) and WebAssembly modules, files must be served over HTTP/HTTPS:

### 1. Start Local HTTP Server
```bash
python -m http.server 8000 --directory build-wasm
```
*(or use Node `npx serve build-wasm`)*

### 2. Open in Browser
Open `http://localhost:8000/videoTest.html` in Chrome, Edge, or Firefox.

### 3. Test with the Pinball Marker
1. Display or print the reference marker image located at [`assets/pinball.jpg`](assets/pinball.jpg).
2. Point your camera at the marker.
3. When recognized:
   - Console logs will report: `>>> NFT Marker 0 RECOGNIZED & TRACKING! <<<`.
   - An 80mm multi-colored 3D cube will appear anchored to the center of the marker, standing on the paper and spinning in real-time AR.

---

## 🎨 Developing Your Own AR Application

To build a custom AR experience, inherit from `ARBaseEngine` and implement `setup()` and `update()`:

```cpp
#include "ARBaseEngine.h"
#include <filament/TransformManager.h>

class CustomARApp : public ARBaseEngine {
public:
    void setup(Engine* engine, Scene* scene) override {
        // 1. Create renderable entities (e.g. glTF models, custom meshes, lights)
        // 2. Add entities to scene
        // 3. Register with TransformManager
    }

    void update(float deltaTime) override {
        if (isTracking) {
            // Retrieve current column-major modelViewMatrix
            math::mat4f pose;
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    pose[c][r] = modelViewMatrix[c * 4 + r];
                }
            }

            // Apply pose matrix to entity transforms
            auto& tcm = engine->getTransformManager();
            auto inst = tcm.getInstance(myEntity);
            tcm.setTransform(inst, pose);
        } else {
            // Hide or freeze entities when tracking is lost
        }
    }
};

int main(int argc, char** argv) {
    static CustomARApp app;
    if (!app.init("canvas", 640, 480, "/assets/markers/pinball")) {
        return 1;
    }
    app.run();
    return 0;
}
```

---

## 🌿 Git & Contribution Guidelines

- **Branching Strategy**:
  - **Every new branch must branch off `dev`**, never `master`.
  - **Every pull request must target `dev`**.
  - `master` is strictly reserved for tagged, stable releases.
- **Commit Messages**: Follow [Conventional Commits](https://www.conventionalcommits.org/):
  ```
  <type>(<scope>): <short summary>

  - Detailed bullet point 1
  - Detailed bullet point 2
  ```
  Allowed types: `feat`, `fix`, `perf`, `doc`, `refactor`, `test`, `style`, `chore`.

---

## 📄 License

This project is licensed under the **GNU Lesser General Public License v3.0** (LGPL-3.0) with a static-linking exception — see the [LICENSE](LICENSE) file for details.

### Third-Party Licenses & Acknowledgements
- **[Google Filament](https://github.com/google/filament)**: Apache 2.0 License.
- **[WebARKitLib](https://github.com/webarkit/WebARKitLib)** / **[jsartoolkitNFT](https://github.com/webarkit/jsartoolkitNFT)**: LGPL-3.0 / GPL-3.0 License.
- **[ARToolKit](https://github.com/artoolkitx/artoolkit5)**: GPL-3.0 License.