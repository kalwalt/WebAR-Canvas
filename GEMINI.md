# Gemini / Antigravity Instructions

The comprehensive guidance for AI agents in this repository lives in **[AGENTS.md](AGENTS.md)**.
Read it before making changes — it is the canonical source and this file is a quick reference pointer.

## Quick Reference Commands

- **Build WASM**: `node build-docker.js` (or `node build-docker.js -j 8`)
- **Clean Build**: `node build-docker.js --clean`
- **Tests**: `node build-docker.js --test` (Node runners `nft_tests` for `ARNFTTracker` and `core_tests` for `src/core/`, also run in CI)
- **Host tools / CI**: the first build compiles the Filament host tools (`scripts/build-host-tools.sh`); CI runs the same scripts (`.github/workflows/build.yml`). Pitfalls (glibc, absolute paths, `FILAMENT_LINUX_IS_MOBILE`, CRLF): AGENTS.md section 4
- **Windows**: check out `third_party/filament` with `core.autocrlf=false` in the submodule's config only (README, "Prerequisites & Setup")
- **Build runner tests**: `node --test scripts/build-docker.test.js` (checks before Docker starts; no Docker needed)
- **Writing an app**: derive from `ARBaseApp` (hooks, marker events, input, `attachToMarker`, `create*` primitives, `loadModel` for glTF, `AR_APP_MAIN` over `ARAppConfig`); the demo is `src/app/App.cpp`. See AGENTS.md section 2C
- **Serve & Test**: `python -m http.server 8000 --directory build-wasm` (open `http://localhost:8000/videoTest.html`)
- **Template & Pages**: start a project with **Use this template** and publish it with `.github/workflows/pages.yml`; see `docs/template.md`

## Key Rules to Remember

1. **Pure C++ Engine**: Do not introduce Embind (`<emscripten/bind.h>`) into engine classes (`ARBaseEngine`, `ARNFTTracker`). Keep tracking data structures native C++ and ensure compatibility with `-fno-rtti`.
2. **Docker Build**: The WebAssembly toolchain is containerized in Docker (`emscripten/emsdk:4.0.17`). Always use `build-docker.js` to compile.
3. **CMake `--embed-file` Syntax**: In `CMakeLists.txt`, always format as `"--embed-file=<src>@<dst>"` with an equal sign.
4. **Conventional Commits**: Format commit messages as `<type>(<scope>): <description>` using standard types (`feat`, `fix`, `perf`, `doc`, `refactor`, `test`, `style`, `chore`).
5. **Branching & PRs**: **Every new branch must start from `dev`**, never `master`. **Every PR must point to `dev`**. `master` is reserved for stable releases.
