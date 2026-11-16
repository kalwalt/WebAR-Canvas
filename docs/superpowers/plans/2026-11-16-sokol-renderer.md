# Sokol Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The rest of #13: a second rendering backend, `SokolRenderer` (WebGL 2 through `sokol_gfx`), chosen at build time with `WEBAR_RENDERER=filament|sokol`. With `sokol`, Filament, its materials and its host tools are not configured at all. The demo shows the video background and the anchored cube on both backends. The PR compares `.wasm` size and FPS.

**Architecture:** `ARBaseEngine` already draws only through `IARRenderer` (stage 4). This stage adds:
- a backend-neutral mesh API to `IARRenderer`, and to `ARBaseApp` for apps, so the cube and anchors work the same on both backends;
- `SokolRenderer`, which implements the interface with hand-written GLSL ES 3.00 shaders (no `sokol-shdc`, no host tool);
- a CMake switch that leaves Filament out entirely.

`ARNFTTracker`, the main loop, input, anchors and the primitives are shared and unchanged.

**Tech Stack:**
- C++17 and Emscripten 4.0.17 (Docker).
- `sokol_gfx.h` + `sokol_log.h` (zlib license, header-only), with the `SOKOL_GLES3` backend.
- Google Filament: the default backend, unchanged.
- CMake and GitHub Actions.

**Spec:**
- Issue #13: "Proposed plan" steps 2–3 and the acceptance criteria. Step 4 (WebGPU) is out of scope.
- The stage 4 plan (D1–D5) and the stage 5 plan (E1–E7) still hold.

## Global Constraints

- No Embind in engine classes; `-fno-rtti`; `--embed-file=<src>@<dst>` syntax.
- Build only with `node build-docker.js` in this worktree, one Docker build at a time.
- `ARBaseEngine` names no backend type.
- The default build (`filament`) stays byte-for-byte the same in behaviour. `node build-docker.js --test` and the CI stay green after every task.
- Tests come first wherever there is pure logic (`core_tests`).
- The `sokol` build must work with **no Filament submodule checked out and no host tools**. CI proves it with a job that does not check Filament out.
- Conventional Commits, `For #13` in bodies.

## Decisions (to approve)

- **F1 — sokol as a pinned submodule.** Add `third_party/sokol` as a git submodule at commit `93aa187` (2026-11-10, zlib license, about 18 MB). Only `sokol_gfx.h` and `sokol_log.h` are used, with `SOKOL_IMPL` in one `.cpp`.
  - Alternative: copy the two headers into `third_party/sokol/`. That is smaller, but updates become manual and the provenance is less clear.
  - The README license list gets sokol (zlib).

- **F2 — a backend-neutral mesh API, used by the demo.** This is beyond the stage text. It adds a **second public API** next to the one merged in #18 (`addBox` returning an id, beside `createBox` returning a Filament `Entity`). The issue's sketch has only `setPose(id, …)`. If approved:
  - The README and `docs/template.md` lead with the neutral API.
  - The Entity API stays, as the Filament-only path (glTF, parenting, Filament materials).
  - **On `IARRenderer`:**
    - `int addMesh(const ARMesh &)`
    - `void setMeshTransform(int id, const float m[16])`
    - `void setMeshVisible(int id, bool)`
    - `void removeMesh(int id)`

    `FilamentRenderer` implements them with entities and the existing `ar_cube.mat`. `SokolRenderer` implements them with one pipeline: unlit, vertex colour, depth test.
  - **On `ARBaseApp`, for both backends:**
    - `ARMeshId addBox/addCube/addPlane/addSphere(...)`, built from `ARPrimitives.h`.
    - `attachMeshToMarker(id, marker, options)`, using `ARAnchor` and updated once per camera frame, as in stage 5.
    - `setMeshLocalTransform(id, m)`: the app's spin, multiplied after the anchor.
    - `setMeshVisible(id, bool)`.
  - **The demo** (`App.cpp`) uses only this API for the cube, so it is the same source on both backends. The glTF click switch stays, behind `#ifdef WEBAR_CANVAS_GLTF` (Filament only). With sokol, a click only prints.
  - **The Filament-only API from stage 5 is unchanged** (`createBox` etc. returning `utils::Entity`, `attachToMarker(Entity)`, `loadModel`, `getEngine/getScene/getCamera`). With sokol, `loadModel` returns `UNSUPPORTED` (-3), as `ARModelError.h` already defines.
  - With sokol, `webar_load_model` / `webar_place_model` stay defined and exported, returning -3 / 0. The link options do not change per backend.
  - Alternative: keep `App.cpp` Filament-only and add a separate sokol demo. That means less change to `ARBaseApp`, but two demos to keep in step, and template users would have no portable API.

- **F3 — CMake.**
  - `set(WEBAR_RENDERER filament CACHE STRING ...)` with `STRINGS filament sokol`. Any other value is a configure error.
  - **With `sokol`:**
    - no `add_subdirectory(filament)`;
    - no `MATC_EXEC`, no materials;
    - `WEBAR_CANVAS_GLTF` forced OFF, with a message;
    - sources `SokolRenderer.cpp` and `sokol_impl.cpp`;
    - definition `WEBAR_RENDERER_SOKOL=1`;
    - include `third_party/sokol`.
  - **With `filament`:** exactly the current setup.
  - The output keeps the name `videoTest.html/.js/.wasm`.
  - The Filament-only parts move into one `if(WEBAR_RENDERER STREQUAL "filament")` block.

- **F4 — build runner and scripts.**
  - `node build-docker.js --renderer sokol` (default `filament`) builds into `build-wasm-sokol/`, so the two builds never share a CMake cache.
  - It skips `scripts/build-host-tools.sh` and the Filament submodule check, and checks for `third_party/sokol` instead.
  - `scripts/build-wasm.sh` reads `WEBAR_RENDERER` and `BUILD_DIR` from the environment, with the defaults that keep the CI and Pages unchanged.
  - `--test` runs the Node runners. They do not use a renderer, so they run the same with either backend.
  - Tests for the new runner flags go in `scripts/build-docker.test.js`.

- **F5 — CI.**
  - `build.yml` gets a second job, "WebAssembly build (sokol)". It checks out only `third_party/WebARKitLib` and `third_party/sokol` (`submodules: false`, then `git submodule update --init` for those two). It does not build host tools, and it runs the build and the Node tests.
  - The Filament job is unchanged.
  - Pages keeps deploying the Filament build.

- **F6 — the video texture in sokol.** The sokol details below (`gl_textures` injection, `sg_reset_state_cache`, the `sg_swapchain` fields) are from memory and are **to confirm at the pinned commit**; reading the pinned header is the first step of Task 1. Fallback: let sokol create the image and pass its GL id (`sg_gl_query_image_info`) to the JS upload.
  - The same path as Filament: a GL texture created with `glGenTextures`, filled each camera frame by the existing `update_webgl_texture` JS (`texImage2D` from `<video id="ar-video">`), and given to sokol as an injected image (`sg_image_desc.gl_textures[0]`).
  - The EM_JS helper moves from `FilamentRenderer.cpp` to a shared `src/core/ARVideoTexture.{h,cpp}`.
  - After the JS upload, `sg_reset_state_cache()` runs, because the upload changes GL bindings behind sokol's back.
  - The WebGL context is created as in `FilamentRenderer` (`emscripten_webgl_create_context`, WebGL 2, depth). sokol renders to the default framebuffer (`sg_swapchain` with `gl.framebuffer = 0`).

- **F7 — alignment and the comparison.**
  - **Projection:** both backends use the same projection and pose matrices (`getCameraLens()`, anchor output). Sokol draws `projection * model` directly in GL clip space.
  - **Alignment check:** a temporary, uncommitted build of each backend. The spin is off and the video is paused at a fixed time, so both frames are deterministic. Then compare the cube's corner pixels, which should be within ~1 px. The spin uses wall-clock time and the pose varies slightly between runs, so free-running screenshots cannot be compared. Both screenshots go in the PR.
  - **Size:** `videoTest.wasm`, `.js` and the total download (gzip too) for both builds, from the same commit.
  - **FPS and frame time:** 6 runs per backend, alternating, with the harness's profile mode. Both backends will probably sit at the display rate, so the comparison leads with the `render` column (ms per tick) and long frames, then mean FPS.
  - **Harness:** `bench.html` parses lines the Filament path prints (`Rendered frame`, `WebGL Error`, `AR 3D Cube added`, `Pose Translation`). Before measuring, check which of them drive the "not rendering" discard and the GL error count, and make `SokolRenderer` print the same lines.
  - **Sizes:** both builds link with `-g` (limited binaryen optimisation), so the figures compare like with like under the current flags.

## Review Focus

- `ARBaseEngine` and `ARBaseApp`'s shared part compile without Filament headers. A sokol build that includes any `filament/` header is a bug.
- GL state: sokol's state cache, against the JS texture upload.
- Mesh lifetime: ids and buffers are released in `removeMesh` and in the renderer's destructor.
- The sokol API at the pinned commit. Its texture views changed in late 2025, so write against the pinned header, not memory.

## File Structure

- `third_party/sokol` (submodule, new).
- `src/core/IARRenderer.h`: the mesh API (F2).
- `src/core/ARVideoTexture.{h,cpp}` (new): the shared `update_webgl_texture` JS and the GL texture creation (F6).
- `src/core/FilamentRenderer.{h,cpp}`: the mesh API, and the video texture moved to the shared helper.
- `src/core/SokolRenderer.{h,cpp}` (new), `src/core/sokol_impl.cpp` (new: `SOKOL_IMPL` + `SOKOL_GLES3`).
- `src/core/ARBaseApp.{h,cpp}`: the neutral mesh API and `createRenderer()` per backend. The Filament part stays behind `WEBAR_RENDERER_FILAMENT`.
- `src/core/ARMeshAnchors.h` (new, pure, tested): binding mesh ids to markers and composing anchor × local transforms, so this logic is tested in `core_tests` without a renderer.
- `src/app/App.{h,cpp}`: the cube through the neutral API, glTF behind `WEBAR_CANVAS_GLTF`.
- `CMakeLists.txt`, `build-docker.js`, `scripts/build-docker.test.js`, `scripts/build-wasm.sh`, `scripts/test-wasm.sh`, `.github/workflows/build.yml`.
- Docs: `README.md`, `AGENTS.md`, `CLAUDE.md`, `GEMINI.md`, `docs/template.md`.

### Task 1: The renderer switch and an empty sokol backend

- [ ] Add the `third_party/sokol` submodule at the pinned commit, then read the pinned `sokol_gfx.h` for the F6 details.
- [ ] `build-docker.test.js`: tests for `--renderer sokol` (build dir, no host tools, the sokol submodule check, unknown renderer rejected); see them fail.
- [ ] `build-docker.js`, `build-wasm.sh`, `test-wasm.sh`: the renderer flag and env (F4).
- [ ] `CMakeLists.txt`: the `WEBAR_RENDERER` switch (F3).
- [ ] `SokolRenderer`, minimal: context, `sg_setup`, clear to black, `render()`. `ARBaseApp::createRenderer()` per backend. With sokol, `App.cpp`'s Filament code is behind `WEBAR_RENDERER_FILAMENT` for now.
- [ ] Both builds green (`--test` on each). In the browser, sokol shows a black canvas, tracking runs (the log shows "found"), and there are no GL errors.
- [ ] CI: the sokol job (F5).
- [ ] Commit: `build: add the WEBAR_RENDERER switch and a sokol backend skeleton`.

### Task 2: The video background in sokol

- [ ] Move the EM_JS upload and the GL texture creation to `ARVideoTexture` (F6). Filament uses it with no change in behaviour (browser check: same video as before).
- [ ] `SokolRenderer`: the video quad pipeline (GLSL ES 3.00), the injected image, `sg_reset_state_cache()` after the upload.
- [ ] Browser check, sokol: the video plays, with the same orientation as Filament (screenshot at the same timestamp).
- [ ] Commit: `feat(render): draw the camera video in the sokol backend`.

### Task 3: The neutral mesh API and the cube on both backends

- [ ] `core_tests`: `ARMeshAnchors` (bind, rebind, unbind, compose anchor × local, hidden while lost, one update per camera frame); see them fail; implement.
- [ ] `IARRenderer` mesh methods. `FilamentRenderer` implements them with entities and `ar_cube.mat`. `SokolRenderer` implements them with a vertex-colour pipeline, depth test and `projection × model` (F2).
- [ ] `ARBaseApp`: `addBox/addCube/addPlane/addSphere`, `attachMeshToMarker`, `setMeshLocalTransform`, `setMeshVisible`.
- [ ] `App.cpp`: the cube through the neutral API; glTF behind `WEBAR_CANVAS_GLTF`.
- [ ] Browser checks:
  - Filament: the cube and the glTF click switch behave as before.
  - sokol: the cube sits on the marker and spins.
  - Alignment within ~1 px at the same timestamp (F7).
- [ ] Commit: `feat(render): a backend-neutral mesh API and the demo cube on sokol`.

### Task 4: Comparison, docs, review, PR

- [ ] Size and FPS comparison (F7). Raw results go in `D:/kalwalt-github/WebAR-Canvas-runs/stage6/`.
- [ ] Docs:
  - README: the renderer option, the license list, sizes;
  - AGENTS.md: the architecture, the switch and the pitfalls;
  - CLAUDE.md / GEMINI.md: one line each;
  - `docs/template.md`: the portable API vs the Filament-only API.
- [ ] A final read-only review on `origin/dev...HEAD`; fix the findings.
- [ ] Push, then the PR to `dev`: "For #13", the comparison table, screenshots, and what is not covered.

## Execution and subsessions

The CMake option, the renderer and the mesh API are sequential: each task builds on the previous one and touches `CMakeLists.txt`, `ARBaseApp` and the renderers. All tasks run in this session, in order. The size and FPS comparison is also done here, as the stage asks. No subsession is planned: there is no independent piece large enough to hand out.

## Not in this stage

- WebGPU (#13 step 4), sokol's WebGPU backend, `emdawnwebgpu`.
- glTF on sokol (a loader such as cgltf would be a follow-up issue).
- Lighting in sokol: the cube is unlit, as on Filament.
- Pages deploying the sokol build.
