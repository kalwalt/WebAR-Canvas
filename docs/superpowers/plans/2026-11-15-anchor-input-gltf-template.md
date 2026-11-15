# Anchors, Input, glTF, Primitives and Template Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The rest of #2 (phases 3–6): `ARAnchor` (entity bound to a marker, centred, optionally smoothed), the Emscripten input bridge (touch, mouse, resize into `ARBaseApp` hooks), primitive geometry helpers, glTF loading through Filament's `gltfio`, and the repository as a template with a GitHub Pages deployment.

**Architecture:** Each feature splits into a pure C++ part in `src/core/` (math, geometry, coordinate mapping; tested in the Node runner `core_tests`) and a thin platform part (Emscripten HTML5 callbacks, Filament entities) that is checked in the browser. Everything that creates Filament objects lives in `ARBaseApp` behind `WEBAR_RENDERER_FILAMENT` (stage 4, D1); `ARBaseEngine` stays renderer-independent.

**Tech Stack:** C++17, Emscripten 4.0.17 (Docker, HTML5 API), Google Filament + `gltfio`, WebARKitLib `ARToolKitNFTCore`, CMake, GitHub Actions (`actions/upload-pages-artifact`, `actions/deploy-pages`).

**Spec:** issue #2, "Proposed Architecture" sections 2–4 and roadmap phases 3–6; stage 4 plan `docs/superpowers/plans/2026-11-15-app-and-renderer-interface.md` (decisions D1–D5 still hold).

## Global Constraints

- No Embind in engine classes; `-fno-rtti`; `--embed-file=<src>@<dst>` syntax.
- Build only with `node build-docker.js` (`emscripten/emsdk:4.0.17`), never in the main checkout; one Docker build at a time (the Emscripten cache volume `webar-canvas-emcache-4.0.17` is shared).
- `ARBaseEngine` names no Filament type; Filament-specific API sits in `ARBaseApp` behind `WEBAR_RENDERER_FILAMENT`. glTF goes through `gltfio`, so it is Filament-only for now (stage 4 note on D1).
- `node build-docker.js --test` and CI stay green after every task; tests first wherever there is pure logic.
- Changes to `ARNFTTracker` need a test in `tests/nft_tests.cpp`.
- No changes to GitHub repository settings (template checkbox, Pages source): they are listed for the maintainer.
- Conventional Commits, `For #2` in bodies.

## Decisions for the maintainer (approve or change before Task 1)

- **E1 — `uberz` becomes a host tool.** `gltfio` links `uberarchive`, which Filament builds with `uberz` (and `matc`, `resgen`). `scripts/build-host-tools.sh` adds `uberz` to its list (5 tools instead of 4); the CI cache adds its path and the key gets a `-v2` suffix, so the first CI run rebuilds the tools once. This is the stage 1 discrepancy (#6 listed 7 tools) resolved for the one extra tool glTF needs.
- **E2 — glTF is linked by default, behind a CMake option.** `WEBAR_CANVAS_GLTF` (default `ON`) links `gltfio_core` and `uberarchive` and enables `loadModel()`. The PR reports the `.wasm` size with and without it; with `OFF` the build is as today.
- **E3 — Models load from the embedded file system** (`/assets/models/...`, `--embed-file`), synchronously, in `setup()`. Loading from a URL (fetch) is left for later. The demo embeds `AnimatedMorphCube.glb` (6.7 KB, CC0, from the Filament submodule's `third_party/models/`), not copied into the repository.
- **E4 — `ARAnchor` smoothing is off by default.** The tracker already filters the pose; `ARAnchor` adds optional exponential smoothing (`smoothing` in [0, 1), 0 = none) so the default keeps today's motion.
- **E5 — Input coordinates are canvas pixels.** The canvas is shown with `object-fit: contain` (letterboxed); events are mapped from client coordinates to the drawing buffer (0..640 × 0..480 in the demo) and events on the letterbox bars are not delivered.
- **E6 — The demo app shows every feature.** The cube is built with `createBox()` (six face colours, as today) and bound with `attachToMarker()` (about 150 lines of manual Filament code go away, same look); `AnimatedMorphCube.glb` sits on the marker too, hidden; a tap on the canvas switches between cube and model. Visible change: the tap switch.
- **E7 — Pages deploys from `master` only.** `.github/workflows/build.yml` uploads the built app as a Pages artifact on every run and a `deploy` job publishes it only on a push to `master` (releases). The maintainer enables Pages with source "GitHub Actions" and ticks "Template repository" in the repository settings.
- **Still deferred:** `IARRenderer::setPose` (stage 4, D2) — `ARAnchor` computes matrices, and the Filament binding applies them; the sokol backend decides in stage 6 how it consumes them.

## Review Focus

- Marker lost while bound: the anchored entity hides (scale 0, as the demo does today) and reappears at the new pose, with no smoothing across the gap → `ARAnchor` tests in Task 1.
- Touch with several fingers, and pointer events on the letterbox bars: one callback per changed touch with its id, nothing outside the image → `clientToCanvas` tests in Task 2, harness events in Task 2.
- Resize (window, device rotation): `windowResized(width, height)` fires once per change and the mapping uses the new rectangle → Task 2 harness check.
- A missing or corrupt `.glb`: `loadModel()` returns an invalid entity and prints an error; the app keeps running → Task 4 browser check with a missing path.
- A sphere with very few segments (1 or 2): the helper clamps to a minimum (3 × 2) instead of producing degenerate geometry → Task 3 test.

## File Structure

| Path | Responsibility | Task |
|---|---|---|
| `src/core/ARAnchor.h` (new) | marker centre offset, anchored model matrix, optional smoothing (pure C++) | 1 |
| `src/core/ARBaseEngine.h/.cpp` | per-index marker pose (`getMarkerPose`) | 1 |
| `src/core/ARBaseApp.h/.cpp` | `attachToMarker`, input hooks, `createCube/Plane/Sphere`, `loadModel` (Filament parts guarded) | 1–4 |
| `src/core/ARInput.h` (new) | `clientToCanvas` mapping (pure) | 2 |
| `src/core/ARInput.cpp` (new) | Emscripten HTML5 callbacks → `ARBaseApp` hooks | 2 |
| `src/core/ARPrimitives.h` (new) | cube / plane / sphere vertices and indices with colours (pure) | 3 |
| `src/core/ARModel.h/.cpp` (new) | `gltfio` asset loading for `ARBaseApp::loadModel` | 4 |
| `src/app/App.h/.cpp` | demo: primitives, anchor, model, tap switch | 1, 3, 4 |
| `tests/core_tests.cpp` | tests for the pure parts | 1–3 |
| `CMakeLists.txt` | `ARInput.cpp`, `ARModel.cpp`, `WEBAR_CANVAS_GLTF`, model embed | 2, 4 |
| `scripts/build-host-tools.sh`, `.github/workflows/build.yml` | `uberz`; Pages artifact and deploy | 4, 5 |
| `README.md`, `AGENTS.md`, `CLAUDE.md`, `GEMINI.md` | app API, template use, maintainer settings | 1–5 |

Browser checks reuse the stage 3/4 harness (`WebAR-Canvas-runs/stage5/`, outside the repository), run in the maintainer's Chrome if the built-in browser cannot load the build, and compare with `dev` in the same browser. Tasks whose app binary is unchanged need no browser run (checked by `videoTest.wasm` SHA-256).

---

### Task 1: `ARAnchor` and per-index marker pose (#2 phase 3)

**Files:** Create `src/core/ARAnchor.h`; modify `src/core/ARBaseEngine.h/.cpp`, `src/core/ARBaseApp.h/.cpp`, `src/app/App.h/.cpp`, `tests/core_tests.cpp`, `AGENTS.md`, `README.md`.

**Interfaces:**
- Produces:
  ```cpp
  struct ARAnchorOptions {
    bool centerOnMarker = true;   // put the origin at the marker's centre (from width, height, dpi)
    float smoothing = 0.0f;       // 0 = none; else weight of the previous pose, in [0, 1)
  };
  // Millimetres from the marker's corner to its centre: (w / dpi * 25.4 / 2, h / dpi * 25.4 / 2).
  void markerCenterMm(int widthPx, int heightPx, int dpi, float &x, float &y);
  class ARAnchor {                // pure C++, column-major 4x4 float matrices
  public:
    explicit ARAnchor(ARAnchorOptions options = {});
    // pose: the marker's model-view matrix; returns the anchored matrix in out.
    void update(bool tracked, const float pose[16], float centerX, float centerY, float out[16]);
    bool visible() const;         // false while the marker is not tracked
  };
  // ARBaseEngine (renderer-independent): marker i's model-view matrix on the last camera frame.
  bool getMarkerPose(int markerIndex, float out[16]) const; // false (and zeros) if not tracked
  // ARBaseApp, Filament only: bind an entity; applied every tick before update().
  void attachToMarker(utils::Entity entity, int markerIndex, ARAnchorOptions options = {});
  void detachFromMarker(utils::Entity entity);
  ```
  Smoothing interpolates translation linearly and rotation by normalised quaternion slerp; a marker that was lost resets the smoothing (no blend across the gap).

- [ ] **Step 1: Write the failing tests** in `tests/core_tests.cpp`:
  - `marker_center_pinball`: `markerCenterMm(893, 1117, 120)` → (94.51, 118.22) within 0.01 mm.
  - `anchor_identity_pose_centres`: identity pose, centre (94.51, 118.22) → `out` translation (94.51, 118.22, 0), rotation identity.
  - `anchor_hidden_when_lost`: `update(false, ...)` → `visible() == false`.
  - `anchor_smoothing_halfway`: smoothing 0.5, pose translation 0 then 10 (x) → second `out` x == 5 (identity rotation, no centring).
  - `anchor_no_smoothing_across_loss`: smoothing 0.5, tracked at x=0, lost, tracked at x=10 → `out` x == 10.
- [ ] **Step 2:** `node build-docker.js --test` → build error (`ARAnchor` not declared).
- [ ] **Step 3:** Implement `ARAnchor.h`; run the tests → pass.
- [ ] **Step 4:** `getMarkerPose`: store every marker's pose on processed camera frames (`ARNFTTracker::getTransformMatrix(i, ...)`), next to `markerTracked`. `attachToMarker`: keep (entity, index, `ARAnchor`) bindings; before `update()` each tick, set the entity's transform from the anchor, or scale 0 when not visible. Port the demo's manual pose code to `attachToMarker(cubeEntity, 0)` plus its spin as a child transform in `update()` (same look).
- [ ] **Step 5:** `node build-docker.js --test` green; harness comparison with `dev` (3 runs, Chrome): tracking, pose lines, 0 GL errors, screenshot with the cube centred on the marker as before.
- [ ] **Step 6:** Docs: `ARAnchor`, `getMarkerPose`, `attachToMarker` in `AGENTS.md` section 2C and README "Developing your own AR application".
- [ ] **Step 7:** Commit `feat(core): add ARAnchor and bind entities to markers` (For #2).

### Task 2: Emscripten input bridge (#2 phase 4)

**Files:** Create `src/core/ARInput.h`, `src/core/ARInput.cpp`; modify `src/core/ARBaseApp.h/.cpp` (hooks, registration in `runApp`), `CMakeLists.txt`, `tests/core_tests.cpp`, `src/app/App.*`, docs.

**Interfaces:**
- Produces:
  ```cpp
  // Client (CSS) coordinates to drawing-buffer pixels for a canvas shown with object-fit: contain.
  // False (x, y untouched) when the point falls on a letterbox bar or outside the element.
  bool clientToCanvas(double clientX, double clientY, double rectLeft, double rectTop,
                      double rectWidth, double rectHeight, int canvasWidth, int canvasHeight,
                      float &x, float &y);
  // ARBaseApp hooks (no-ops by default), coordinates in canvas pixels:
  virtual void touchDown(float x, float y, int touchId) {}
  virtual void touchMoved(float x, float y, int touchId) {}
  virtual void touchUp(float x, float y, int touchId) {}
  virtual void mouseDown(float x, float y, int button) {}
  virtual void mouseMoved(float x, float y) {}
  virtual void mouseUp(float x, float y, int button) {}
  virtual void windowResized(int width, int height) {}
  ```
  `ARInput.cpp` registers `emscripten_set_{touchstart,touchmove,touchend,touchcancel}_callback`, `emscripten_set_{mousedown,mousemove,mouseup}_callback` on the config's canvas selector and `emscripten_set_resize_callback` on the window, after a successful `init()`; it reads the canvas rectangle with `getBoundingClientRect` through EM_JS on each event. Touch callbacks report only changed touches (`isChanged`); `touchcancel` reports `touchUp`.

- [ ] **Step 1: Write the failing tests:** `client_to_canvas_letterbox_wide` (element 1280×480, canvas 640×480: image spans x 320..960; client (640, 240) → (320, 240); client (100, 240) → false), `client_to_canvas_letterbox_tall` (element 640×960: image spans y 240..720), `client_to_canvas_corners` (top-left and bottom-right pixels of the image map to 0 and just under 640/480), `client_to_canvas_with_offset` (rectLeft/rectTop non-zero).
- [ ] **Step 2:** Run → build error. **Step 3:** Implement `clientToCanvas`; tests pass.
- [ ] **Step 4:** Implement the hooks and `ARInput.cpp`; the demo's `touchDown` / `mouseDown` print `tap at (x, y)`.
- [ ] **Step 5:** Harness: dispatch synthetic `MouseEvent`s and `TouchEvent`s on the canvas at known client points (inside the image, on a bar) and a window resize; expected console lines with the mapped coordinates, none for the bar, one `windowResized` per resize. Plus the `dev` comparison (unchanged tracking and frames).
- [ ] **Step 6:** Docs; commit `feat(core): route touch, mouse and resize events to ARBaseApp` (For #2).

### Task 3: Primitive geometry helpers (#2 section 3)

**Files:** Create `src/core/ARPrimitives.h`; modify `src/core/ARBaseApp.h/.cpp`, `src/app/App.*`, `tests/core_tests.cpp`, docs.

**Interfaces:**
- Produces:
  ```cpp
  struct ARVertex { float position[3]; float color[4]; };
  struct ARMesh { std::vector<ARVertex> vertices; std::vector<uint16_t> indices; };
  ARMesh makeBox(float sizeX, float sizeY, float sizeZ, const float faceColors[6][4]); // base on z = 0
  ARMesh makePlane(float sizeX, float sizeY, const float color[4]);                    // z = 0, facing +z
  ARMesh makeSphere(float radius, int segments, int rings, const float color[4]);      // min 3 x 2
  // ARBaseApp, Filament only: build the mesh with the unlit vertex-colour material (ar_cube.mat).
  utils::Entity createBox(float sx, float sy, float sz, const float faceColors[6][4]);
  utils::Entity createCube(float size, const float color[4]);
  utils::Entity createPlane(float sx, float sy, const float color[4]);
  utils::Entity createSphere(float radius, const float color[4], int segments = 24, int rings = 16);
  ```
- [ ] **Step 1: Failing tests:** `box_counts` (24 vertices, 36 indices, base at z = 0, top at z = sizeZ), `plane_counts` (4 / 6, all z = 0), `sphere_counts_and_radius` (segments 8, rings 4: vertex count and every vertex at distance `radius` within 1e-5), `sphere_minimum_segments` (segments 1, rings 1 → same as 3 × 2), `indices_in_range` (every index < vertex count, for all three).
- [ ] **Step 2–3:** Run → build error; implement; tests pass.
- [ ] **Step 4:** `createBox` / `createCube` / `createPlane` / `createSphere` in `ARBaseApp` (own the material instance and buffers; released in `~ARBaseApp`); the demo cube becomes `createBox(80, 80, 80, faceColors)` with today's six colours.
- [ ] **Step 5:** `node build-docker.js --test`; harness comparison with `dev` and a screenshot: same cube, same colours, same position.
- [ ] **Step 6:** Docs; commit `feat(core): add box, plane and sphere helpers` (For #2).

### Task 4: glTF loading through `gltfio` (#2 phase 5)

**Files:** Create `src/core/ARModel.h`, `src/core/ARModel.cpp`; modify `scripts/build-host-tools.sh` (add `uberz`), `.github/workflows/build.yml` (cache path for `uberz`, key `...-v2`), `CMakeLists.txt` (`WEBAR_CANVAS_GLTF`, link `gltfio_core` + `uberarchive`, embed `third_party/filament/third_party/models/AnimatedMorphCube/AnimatedMorphCube.glb@/assets/models/AnimatedMorphCube.glb`), `src/core/ARBaseApp.h/.cpp`, `src/app/App.*`, docs (README "Prerequisites": five host tools; AGENTS section 4).

**Interfaces:**
- Produces (Filament only, and only with `WEBAR_CANVAS_GLTF`):
  ```cpp
  // Loads a .glb / .gltf from the embedded file system, adds it to the scene, returns its root
  // entity (invalid on failure, with an error printed). Units: the model's own (metres in glTF);
  // scale it with the root transform.
  utils::Entity loadModel(const std::string &path);
  ```
  `ARModel` owns the `gltfio::AssetLoader`, `MaterialProvider` (ubershader archive), `ResourceLoader` and the loaded `FilamentAsset`s; destroyed before the renderer.

- [ ] **Step 1:** Add `uberz` to the host tools and run `node build-docker.js` in the worktree (the script builds the missing tool); confirm `out/host/tools/uberz/uberz` exists and the import file lists it.
- [ ] **Step 2:** Link `gltfio_core` and `uberarchive` behind `WEBAR_CANVAS_GLTF`, implement `ARModel` and `loadModel`; the demo loads the morph cube at 80 mm on the marker, hidden; a tap (Task 2) switches cube and model.
- [ ] **Step 3:** `node build-docker.js --test` green; `.wasm` size with `WEBAR_CANVAS_GLTF=ON` and `OFF` (two builds, recorded).
- [ ] **Step 4:** Browser: screenshot of the model on the marker after a synthetic tap; a run with a missing model path (temporary local change) prints the error and keeps tracking; `dev` comparison for frames and tracking.
- [ ] **Step 5:** Docs; commit `feat(core): load glTF models with gltfio` (For #2). The CI run of the PR shows the host-tools cache rebuilt once with `uberz`.

### Task 5: Template repository and GitHub Pages (#2 phase 6)

**Files:** Modify `.github/workflows/build.yml` (steps after the build: copy `videoTest.{html,js,wasm}` to `pages/` with `videoTest.html` also as `index.html`, `actions/upload-pages-artifact`; new job `deploy` with `needs: wasm`, `if: github.event_name == 'push' && github.ref == 'refs/heads/master'`, `permissions: pages: write, id-token: write`, `environment: github-pages`, `actions/deploy-pages`), `README.md` (section "Use this repository as a template": click "Use this template", write `src/app/App.cpp`, push, enable Pages), `AGENTS.md`, `CLAUDE.md`, `GEMINI.md`.

No behaviour to unit-test; the check is the PR's workflow run (the artifact step runs, the deploy job is skipped on a PR) and, after the next release, the maintainer's settings.

- [ ] **Step 1:** Workflow changes; `node --test scripts/build-docker.test.js` still passes (unchanged).
- [ ] **Step 2:** README and agent docs, including the two settings the maintainer must change.
- [ ] **Step 3:** Commit `ci: publish the app to GitHub Pages from master` and `doc: describe the repository as a template` (For #2).

### Task 6: Stage review and PR

- [ ] **Step 1:** `superpowers:requesting-code-review` on `origin/dev...HEAD` (read-only reviewer); fix or record findings.
- [ ] **Step 2:** `node build-docker.js --test` green; push without force; PR to `dev` titled `feat(core): anchors, input, primitives, glTF and the template workflow`, body `For #2`, with the browser checks, the `.wasm` sizes and the maintainer's settings; report the CI run.

## Execution and subsessions

Task 1 comes first (the others use `getMarkerPose` / `attachToMarker`). Tasks 2, 3 and 4 all touch `ARBaseApp.h/.cpp`, `src/app/App.*` and `tests/core_tests.cpp`, and each needs a Docker build; the builds must not run concurrently (shared Emscripten cache volume and one CPU), so parallel writers would mostly wait for each other and then conflict on the same files. They run in sequence in this session.

Task 5 touches only `.github/workflows/build.yml` and documentation and needs no local build: it can go to **one writing subsession** in its own worktree and branch `feat/2-anchor-input-gltf-template-pages`, created from the stage branch after Task 1, running in parallel with Tasks 2–4. Its `build.yml` edits (new steps after the build, new `deploy` job) are in different hunks from Task 4's (cache path and key); I merge its branch into the stage branch after Task 4, without force, and delete it. The code review in Task 6 is a read-only subsession.

## Not in this stage

`IARRenderer::setPose` and the sokol backend (stage 6); loading models from URLs; animation playback for glTF (`gltfio::Animator`) beyond the static first frame; changing repository settings; moving KPM to a worker (#4 follow-up); CHANGELOG entries (release time).
