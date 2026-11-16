# GitHub Copilot Instructions

The canonical guidance for this repository lives in **[AGENTS.md](../AGENTS.md)**: read it for the architecture, build, rules and verification. In short:

- **What it is**: a C++17 WebAR engine compiled to WebAssembly with Emscripten 4.0.17 (Docker). Rendering goes through `IARRenderer`, with Google Filament (default) or sokol_gfx (`--renderer sokol`); tracking is WebARKitLib NFT.
- **Rules**: no Embind in engine classes; `-fno-rtti` (no `dynamic_cast`, `typeid`); no Filament or sokol types in `ARBaseEngine`; `--embed-file=<src>@<dst>` in `CMakeLists.txt`.
- **Build and test**: `node build-docker.js --test` (or `--renderer sokol --test`); tracker changes need a test in `tests/nft_tests.cpp`, pure `src/core/` logic in `tests/core_tests.cpp`.
- **Git**: branches from `dev`, PRs to `dev`; Conventional Commits (`feat`, `fix`, `perf`, `doc`, `refactor`, `test`, `style`, `chore`).
