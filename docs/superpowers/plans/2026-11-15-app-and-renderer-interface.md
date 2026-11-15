# Base Application and Renderer Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split framework from app code (#2 phase 1), give apps an openFrameworks-style `ARBaseApp` with marker events and a one-line entry point (#2 phase 2), and put rendering behind an `IARRenderer` interface with Filament as `FilamentRenderer`, behaviour unchanged (#13 step 1).

**Architecture:** `ARBaseEngine` (in `src/core/`) keeps the main loop, the CPU video path (frame pacing, capture, `ARNFTTracker`) and the profiling probe, and talks to rendering only through `IARRenderer`. Everything Filament-specific (WebGL context, Filament engine/scene/view/camera, video texture and background quad, the render call) moves into `FilamentRenderer`. `ARBaseApp` derives from `ARBaseEngine` and is what apps derive from: zero-argument hooks, getters, marker events, and `AR_APP_MAIN`.

**Tech Stack:** C++17, Emscripten 4.0.17 (Docker), Google Filament (WebGL 2), WebARKitLib `ARToolKitNFTCore`, CMake, Node test runners.

**Spec:** issue #2 (phases 1 and 2), issue #13 ("Proposed plan" step 1 and the first acceptance criterion). There is no separate design document; the issues are the spec.

## Global Constraints

- No Embind (`<emscripten/bind.h>`, `emscripten::val`) in engine classes; everything compiles with `-fno-rtti` (no `dynamic_cast`, no `typeid`).
- `--embed-file` in `CMakeLists.txt` is always written `"--embed-file=<src>@<dst>"`.
- Build only with `node build-docker.js` (Docker image `emscripten/emsdk:4.0.17`); never in the main checkout.
- The default Filament build stays as it is: same output names (`videoTest.{html,js,wasm}`), same assets, same behaviour in the browser; `node build-docker.js --test` and the CI workflow stay green.
- No sokol backend and no `WEBAR_RENDERER` CMake option in this stage (stage 6); no `ARAnchor`, input handling, glTF or primitive helpers (stage 5).
- Changes to `ARNFTTracker` need a test in `tests/nft_tests.cpp` (none are planned).
- Conventional Commits, `For #2` / `For #13` in bodies, one commit per task at least.

## Decisions for the maintainer (approve or change before Task 1)

- **D1 — Filament types in the app API.** #2 asks for `getEngine()`, `getScene()`, `getCamera()` on `ARBaseApp`; #13 asks that `ARBaseEngine` stop exposing Filament types. Proposal: `ARBaseEngine` depends only on `IARRenderer`; `ARBaseApp` additionally offers `getRenderer()` (backend independent) and the Filament getters, which return `filament::Engine*` / `Scene*` / `Camera*` from `FilamentRenderer`. In stage 6 those three getters will exist only in the Filament build, so an app that uses them is a Filament app. The getters keep today's demo (cube built with raw Filament calls) working without new helpers.
- **D2 — `setPose(id, m[16])` from #13's interface is not added yet.** In the Filament build the app positions its own entities (today's cube); a renderer-owned pose only makes sense once `ARAnchor` (stage 5) or the sokol cube (stage 6) exists. The interface in this stage: `init`, `setProjection`, `updateVideoTexture`, `render`.
- **D3 — `update(float deltaTime)` gets the real elapsed time in seconds** (from `emscripten_get_now()`, 0 on the first frame, clamped to 0.1 s after a pause), not today's constant 0.016. Visible effect: the demo cube spins at its intended 1.5 rad/s on 120–144 Hz displays, where it now spins about 2.4× faster. Keeping 0.016 is possible if you prefer zero visible change.
- **D4 — Marker events for every marker.** `onMarkerFound(int)` / `onMarkerLost(int)` fire for each loaded marker index on the camera frame where its tracked state changes, from the same per-frame state that `isTracking` uses today. `isTracking` / `getModelViewMatrix()` keep describing marker 0, as now.

## Review Focus

- Events at start-up: no `onMarkerLost` before a marker was ever found, and no event at all on ticks without a new camera frame (frame pacing) → `MarkerEventTracker` tests in Task 3.
- First frame and resumed tab: `deltaTime` is 0 on the first tick and at most 0.1 s after a pause (background tab, debugger) → `computeDeltaSeconds` tests in Task 4.
- Two markers changing state in the same frame: both events fire, in index order → Task 3 test.
- Initialisation failure (no WebGL 2, missing camera file or marker): `AR_APP_MAIN` returns 1, no main loop starts, no `setup()` call → Task 4 step 6 checks it by loading the app with a missing marker path in the stage 3 harness.
- Teardown order: the app's own Filament entities are destroyed before `FilamentRenderer` destroys the Filament `Engine` (C++ destroys the derived `App` first) → Task 2 and Task 4 keep entity cleanup in the app's destructor; covered by reading, there is no browser teardown path today.

## File Structure

| Path | Responsibility | Task |
|---|---|---|
| `src/core/ARBaseEngine.h/.cpp` (moved from `src/`) | main loop, frame pacing, CPU capture, tracker, probe, hook dispatch | 1, 2, 4 |
| `src/core/IARRenderer.h` (new) | backend-independent renderer interface | 2 |
| `src/core/FilamentRenderer.h/.cpp` (new) | WebGL 2 context, Filament objects, video texture + background quad, render | 2 |
| `src/core/AREvents.h` (new) | `MarkerEventTracker`, `computeDeltaSeconds` (pure C++, header only) | 3, 4 |
| `src/core/ARBaseApp.h/.cpp` (new) | app base class, getters, `AR_APP_MAIN` | 4 |
| `src/app/App.h/.cpp` (new, from `src/main.cpp`) | the demo app: cube on the pinball marker | 1, 4 |
| `src/main.cpp` | entry point only | 1, 4 |
| `tests/core_tests.cpp` (new) | Node runner for `src/core/AREvents.h` | 3 |
| `CMakeLists.txt`, `scripts/test-wasm.sh` | new paths, `core_tests` target | 1, 2, 3 |
| `AGENTS.md`, `README.md`, `CLAUDE.md`, `GEMINI.md` | layout, architecture, app API | 1, 2, 4 |

Browser checks reuse the stage 3 harness (`D:/kalwalt-github/WebAR-Canvas-runs/stage3/bench.html`, outside the repository): marker source, profile off, 3 runs, compared with the `dev` build `1829525`. "Unchanged" means: every tick tracked, pose z within ±3 mm of dev's median, 0 GL errors, long frames per 10 s within dev's [min..max], and a screenshot showing the video background and the cube on the marker.

---

### Task 1: Directory restructure (#2 phase 1)

**Files:**
- Move: `src/ARBaseEngine.h` → `src/core/ARBaseEngine.h`, `src/ARBaseEngine.cpp` → `src/core/ARBaseEngine.cpp` (`git mv`)
- Create: `src/app/App.h`, `src/app/App.cpp` (the `WebARApp` class from `src/main.cpp`, renamed `App`, unchanged otherwise)
- Modify: `src/main.cpp` (only `main()`, using `App`), `CMakeLists.txt` (`SOURCES`, include dirs `src/`, `src/core/`, `src/nft/`, `src/app/`), `AGENTS.md` section 3, `README.md` project tree

**Interfaces:**
- Produces: `class App : public ARBaseEngine` in `src/app/App.h`; include paths `"core/ARBaseEngine.h"`, `"nft/ARNFTTracker.h"`, `"app/App.h"` relative to `src/`.

- [ ] **Step 1:** Move the files and split `main.cpp` as above; no code changes beyond includes and the class name.
- [ ] **Step 2:** Run `node build-docker.js --test` in the worktree. Expected: build succeeds, last line `46 checks, 0 failed`.
- [ ] **Step 3:** Copy `build-wasm/videoTest.{html,js,wasm}` to `WebAR-Canvas-runs/stage4/<sha>/` and run the harness comparison with `dev` (3 runs each, interleaved). Expected: "unchanged" as defined above.
- [ ] **Step 4:** Update the directory layout in `AGENTS.md` section 3 and the tree in `README.md`.
- [ ] **Step 5:** Commit: `refactor(core): move the engine to src/core and the demo app to src/app` (For #2).

### Task 2: `IARRenderer` and `FilamentRenderer` (#13 step 1)

**Files:**
- Create: `src/core/IARRenderer.h`, `src/core/FilamentRenderer.h`, `src/core/FilamentRenderer.cpp`
- Modify: `src/core/ARBaseEngine.h/.cpp`, `src/app/App.cpp`, `CMakeLists.txt` (add `src/core/FilamentRenderer.cpp`), `AGENTS.md` section 2

**Interfaces:**
- Produces:
  ```cpp
  class IARRenderer {
  public:
    virtual ~IARRenderer() = default;
    // Creates the WebGL context on canvasSelector ("#canvas") and the backend; false on failure.
    virtual bool init(const char *canvasSelector, int width, int height) = 0;
    // Column-major OpenGL projection (ARNFTTracker::getCameraLens()) and its planes.
    virtual void setProjection(const double projection[16], double nearPlane, double farPlane) = 0;
    // Uploads the current frame of <video id="ar-video"> to the background texture.
    virtual void updateVideoTexture() = 0;
    virtual void render() = 0;
  };
  class FilamentRenderer final : public IARRenderer {
  public:
    filament::Engine *getEngine() const;
    filament::Scene *getScene() const;
    filament::Camera *getCamera() const;
    filament::View *getView() const;
    // ...IARRenderer overrides
  };
  ```
- `ARBaseEngine` owns `std::unique_ptr<FilamentRenderer>` in this stage and uses it only through `IARRenderer&` (`getRenderer()`), plus a protected `FilamentRenderer *filamentRenderer()` for the app hook below. No Filament header is included by `ARBaseEngine.h`.
- Until Task 4, the developer hook stays `virtual void setup(filament::Engine*, filament::Scene*)`; `ARBaseEngine.cpp` passes `filamentRenderer()->getEngine()` / `getScene()`.

- [ ] **Step 1:** Move into `FilamentRenderer`: the WebGL 2 context attributes and creation, `CustomWebGLPlatform`, the Filament engine/swap chain/renderer/scene/view/camera, the clear colour, the video material + imported texture + background quad, `update_webgl_texture` (EM_JS) and the body of `render()` including `engine->execute()`, and their destruction (reverse creation order, as in today's destructor). `setProjection` does what `init()` does today with `customProj` and `lookAt`.
- [ ] **Step 2:** `ARBaseEngine::init()` keeps its order: renderer `init`, tracker `loadCamera`/`setup`/`setupAR2`/`loadMarker`, projection print, `renderer->setProjection(getCameraLens(), near, far)`, developer `setup`. `updateVideoFrame()` / `profiledTick()` call `renderer->updateVideoTexture()`; `render()` calls `renderer->render()`. The probe's `upload` column keeps measuring `updateVideoTexture()`.
- [ ] **Step 3:** Run `node build-docker.js --test`. Expected: `46 checks, 0 failed`.
- [ ] **Step 4:** Harness comparison with `dev` (3 runs, profile off) and one profiled run. Expected: "unchanged"; the profiled run's `upload` and `render` means within dev's spread from stage 3 (marker: upload ≈ 0.6 ms, render ≈ 0.3 ms per tick with frame pacing).
- [ ] **Step 5:** `grep -n "filament" src/core/ARBaseEngine.h` → no matches. Update `AGENTS.md` section 2 (renderer interface; dual pipeline now split between `FilamentRenderer` and `ARBaseEngine`).
- [ ] **Step 6:** Commit: `refactor(render): put Filament behind an IARRenderer interface` (For #13).

### Task 3: Marker events and a Node runner for the core (#2 phase 2, part 1)

**Files:**
- Create: `src/core/AREvents.h`, `tests/core_tests.cpp`
- Modify: `CMakeLists.txt` (`core_tests` target inside `WEBAR_CANVAS_BUILD_TESTS`, header-only, no Filament, no tracker), `scripts/test-wasm.sh` (build and run `core_tests` after `nft_tests`; exit status = failures of both)

**Interfaces:**
- Produces:
  ```cpp
  // Turns per-frame tracked flags into found/lost events, in index order.
  class MarkerEventTracker {
  public:
    template <class Found, class Lost>
    void update(const std::vector<bool> &tracked, Found onFound, Lost onLost);
    void reset(); // forget all states (after markers are reloaded)
  };
  ```
  A marker that was never seen counts as lost; a marker index beyond the previous vector counts as previously lost.

- [ ] **Step 1: Write the failing tests** in `tests/core_tests.cpp` (same `CHECK` macro and output format as `nft_tests.cpp`, last line `N checks, M failed`):
  - `no_events_while_never_tracked`: `update({false})` three times → no found, no lost.
  - `found_once_then_lost_once`: `{false},{true},{true},{false},{false}` → found `[0]` on call 2, lost `[0]` on call 4, nothing else.
  - `two_markers_same_frame`: `{false,false}` then `{true,true}` → found `[0,1]` in that order; then `{false,false}` → lost `[0,1]`.
  - `reset_forgets_state`: `{true}` (found 0), `reset()`, `{true}` → found `[0]` again.
- [ ] **Step 2:** Run `node build-docker.js --test`. Expected: build error, `MarkerEventTracker` not declared.
- [ ] **Step 3:** Implement `MarkerEventTracker` in `src/core/AREvents.h`.
- [ ] **Step 4:** Run `node build-docker.js --test`. Expected: `46 checks, 0 failed` (nft) and the `core_tests` line with 0 failed; the script's exit code is 0.
- [ ] **Step 5:** Commit: `feat(core): add marker found/lost event tracking with a Node test runner` (For #2).

### Task 4: `ARBaseApp`, real `deltaTime`, `AR_APP_MAIN` (#2 phase 2, part 2)

**Files:**
- Create: `src/core/ARBaseApp.h`, `src/core/ARBaseApp.cpp`
- Modify: `src/core/AREvents.h` (add `computeDeltaSeconds`), `tests/core_tests.cpp`, `src/core/ARBaseEngine.h/.cpp`, `src/app/App.h/.cpp`, `src/main.cpp`, `CMakeLists.txt`, `AGENTS.md`, `README.md`, `CLAUDE.md`, `GEMINI.md`

**Interfaces:**
- Consumes: `IARRenderer`, `FilamentRenderer` getters (Task 2), `MarkerEventTracker` (Task 3).
- Produces:
  ```cpp
  // Seconds since the previous tick: 0 when previousMs < 0 (first tick), at most maxSeconds.
  inline double computeDeltaSeconds(double previousMs, double nowMs, double maxSeconds = 0.1);

  class ARBaseEngine {                       // hooks: protected, no-op by default
    virtual void setup() {}
    virtual void update(float deltaTime) {}  // seconds
    virtual void draw() {}                   // after update(), before the frame is rendered
    virtual void onMarkerFound(int markerIndex) {}
    virtual void onMarkerLost(int markerIndex) {}
  };
  class ARBaseApp : public ARBaseEngine {
  public:
    int runApp(const char *canvasSelector, int width, int height, const std::string &markerPath);
  protected:
    IARRenderer &getRenderer();
    ARNFTTracker *getTracker() const;
    filament::Engine *getEngine() const;     // D1
    filament::Scene *getScene() const;
    filament::Camera *getCamera() const;
  };
  #define AR_APP_MAIN(AppClass, width, height, markerPath) \
    int main() { static AppClass app; return app.runApp("#canvas", width, height, markerPath); }
  ```
- Tick order: `updateVideoFrame()` (camera frames only; marker events dispatched there, from `MarkerEventTracker` over all marker indices) → `update(dt)` → `draw()` → `renderer.render()`. `setup()` runs once, at the end of a successful `init()`. When `init()` fails, `runApp` prints `Error during initialization` to stderr and returns 1 without starting the loop; otherwise it starts the loop (which never returns, `simulate_infinite_loop`).

- [ ] **Step 1: Write the failing tests** in `tests/core_tests.cpp`:
  - `delta_is_zero_on_first_tick`: `computeDeltaSeconds(-1.0, 1000.0) == 0.0`.
  - `delta_in_seconds`: `computeDeltaSeconds(1000.0, 1016.0)` within 1e-9 of 0.016.
  - `delta_is_clamped`: `computeDeltaSeconds(1000.0, 6000.0) == 0.1`.
  - `delta_never_negative`: `computeDeltaSeconds(1000.0, 999.0) == 0.0`.
- [ ] **Step 2:** Run `node build-docker.js --test`. Expected: build error, `computeDeltaSeconds` not declared.
- [ ] **Step 3:** Implement `computeDeltaSeconds`; run the tests. Expected: all `core_tests` pass.
- [ ] **Step 4:** Implement the hooks, `ARBaseApp`, `AR_APP_MAIN` and the dispatch described above; port `App` to `setup()` / `update(float)` with the Filament getters (cube code unchanged; per D3 the spin now uses real time); add `onMarkerFound` / `onMarkerLost` overrides in `App` that print `>>> NFT Marker %d found <<<` / `lost`; `src/main.cpp` becomes `#include "app/App.h"` and `AR_APP_MAIN(App, 640, 480, "/assets/markers/pinball")`. Remove the old `RECOGNIZED` / `LOST!` prints from `ARBaseEngine` (the app's prints replace them) and update the stage 3 harness's parser to count the new lines.
- [ ] **Step 5:** Run `node build-docker.js --test` (all green) and the harness comparison with `dev` (3 runs). Expected: "unchanged", with `found` counted once per run that starts untracked, and pose z as before.
- [ ] **Step 6:** Failure path: a harness run of a build whose `App` uses a missing marker path (temporary local edit, not committed) shows `Error during initialization` once in the console, no `setup` output, no main loop (`Rendered frame` never printed).
- [ ] **Step 7:** Docs: `AGENTS.md` (app API, hook order, `deltaTime`, events, D1 getters; replace "`setup(Engine*, Scene*)` is the user API"), `README.md` (a short "Writing an app" section with the `ARBaseApp` example), `CLAUDE.md` / `GEMINI.md` (one line pointing to `src/app/App.cpp`).
- [ ] **Step 8:** Commit: `feat(core): add ARBaseApp with lifecycle hooks, marker events and AR_APP_MAIN` (For #2).

### Task 5: Stage review and PR

- [ ] **Step 1:** `superpowers:requesting-code-review` on `origin/dev...HEAD`; fix findings or record why not.
- [ ] **Step 2:** `node build-docker.js --test` green; push without force; PR to `dev` titled `feat(core): ARBaseApp and an IARRenderer interface with Filament behind it`, body `For #2` / `For #13`, with the harness comparison table; report the CI run.

## Subsessions

All four implementation tasks touch `ARBaseEngine` or `CMakeLists.txt`, and each needs the previous one's result, so they run one after the other in one worktree. The only candidate for a separate writer is Task 3 (`AREvents.h`, `tests/core_tests.cpp`); it still shares `CMakeLists.txt` and `scripts/test-wasm.sh` with Tasks 1 and 2, and it is small, so handing it out saves nothing. Read-only subsessions: the code review in Task 5.

## Not in this stage

`setPose` and the sokol backend (#13, stage 6, see D2); `ARAnchor`, input bridge, `gltfio`, primitive helpers, template repository and Pages workflow (#2 phases 3–6, stage 5); moving KPM to a worker (#4 follow-up); CHANGELOG entries (added at release time).
