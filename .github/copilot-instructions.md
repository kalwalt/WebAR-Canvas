# GitHub Copilot Instructions

Guidance for GitHub Copilot in the **WebAR-Canvas (`Wasm_AR_Engine`)** repository.
The full canonical documentation lives in **[AGENTS.md](../AGENTS.md)**.

## Project Summary
WebAR-Canvas is a high-performance Augmented Reality framework compiling C++17 to WebAssembly using Emscripten. It marries **Google Filament** for WebGL 2 PBR 3D rendering with **WebARKitLib** for Natural Feature Tracking (NFT).

## Coding Standards & Conventions
- **Language**: C++17.
- **RTTI**: Disabled (`-fno-rtti`) to match Filament's build configuration. Avoid `dynamic_cast` and `typeid`.
- **Bindings**: Do not wrap C++ engine classes in Embind (`<emscripten/bind.h>`). The engine runs natively in WebAssembly; avoid round-tripping through JavaScript.
- **Memory Management**: Use `std::unique_ptr` and RAII. For raw AR buffers (`videoFrame`, `videoLuma`), manage lifetimes cleanly and ensure deallocation in `teardown()`.
- **Math & Matrices**:
  - ARToolKit uses column-major 4x4 projection matrices (`cameraLens[16]`) via `arglCameraFrustumRH()`.
  - ARToolKit tracking outputs 3x4 pose matrices (`trans[3][4]`).
  - Filament uses `filament::math::mat4` and `filament::math::mat4f` (column-major).
- **Build System**:
  - Pinned Emscripten toolchain in Docker: compile using `node build-docker.js`.
  - Do not commit generated build files in `build-wasm/`.
- **Branching & PRs**: Every new branch must start from `dev`, and every PR must point to `dev`. `master` is reserved for stable releases.
- **Git Commits**: Use [Conventional Commits](https://www.conventionalcommits.org/) (`feat`, `fix`, `perf`, `doc`, `refactor`, `test`, `style`, `chore`).
