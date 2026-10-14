# ARNFTTracker over ARToolKitNFTCore (WebARKitLib PR #94) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

*Italian version: [2026-10-11-pr94-artoolkit-nft-core.md](2026-10-11-pr94-artoolkit-nft-core.md). The two are kept in sync; this one is the reference for execution.*

**Goal:** `ARNFTTracker` keeps the API that `ARBaseEngine` and apps use, but delegates camera, markers, KPM detection, AR2 tracking and pose filtering to `ARToolKitNFTCore` from WebARKitLib PR #94, with the same tracking behaviour as `dev` today.

**Architecture:** WebARKitLib moves from `4fd3034` (0.10.0) to the PR #94 head `6fd0a25` (a fast-forward). A headless Node test runner first pins the current `ARNFTTracker` behaviour (characterization tests), then the class is rewritten as a thin adapter: it owns the RGBA buffer that JS fills and the luma conversion (`WebARKitVideoLuma`), and forwards everything else to one `ARToolKitNFTCore`. The same tests must stay green across the rewrite, and CI runs them.

**Tech Stack:** C++17, Emscripten 4.0.17 (`emscripten/emsdk:4.0.17`), CMake ≥ 3.19, WebARKitLib `WebARKitNFT` @ `6fd0a25`, Node 22 (in the emsdk image), GitHub Actions.

**Spec:** [webarkit/WebARKitLib#94](https://github.com/webarkit/WebARKitLib/pull/94): the README section "Using `ARToolKitNFTCore` natively" and the class comment in `WebARKit/WebARKitTrackers/WebARKitNFT/include/WebARKitTrackers/WebARKitNFT/ARToolKitNFTCore.h`. Behaviour to preserve: `src/nft/ARNFTTracker.{h,cpp}` on `dev` @ `24db9a4`. Test runner: step 2 of [#9](https://github.com/kalwalt/WebAR-Canvas/issues/9).

## Global Constraints

- Branch `feat/nft-core` from `dev` **after PR #10 (CI) is merged**; the PR targets `dev` (AGENTS.md §5). Build with `node build-docker.js`; commits follow Conventional Commits.
- WebARKitLib submodule: `6fd0a250c132b64d4d30e3e0708b66d85a1eaaac` (branch `feat/nft-core` of `webarkit/WebARKitLib`). PR #94 is not merged upstream: this work may land in `dev`, but **no release to `master`** until the core is in a WebARKitLib release; then the submodule moves to that tag.
- Single-thread only: `singleThreadPreset()`, `WEBARKIT_NFT_THREADS OFF` (as today).
- Behaviour kept from `dev`: filtering **on** (`ARToolKitNFTCore(singleThreadPreset(), true)`), detection interval **200.0 ms** (core default 300), continuous detection **on**, projection planes near **10.0** / far **5000.0** (core default 0.0001 / 1000). AR2 settings and the detection policy are already identical. The filter (dev 30/15 Hz, core 120/60 Hz) has the same sample-rate/cutoff ratio, hence the same low-pass coefficient: no change needed.
- Core contract: `loadCamera()` (static) → `setup()` (then check `cameraParamLT() != nullptr`) → `setupAR2()` **once** → `addNFTMarkers()`; per frame `setVideoFrame(rgba, luma)` + `detectNFTMarker()`.
- `ARToolKitNFTCore::getNFTData(i)` aborts on an out-of-range index (`std::vector::at`): the adapter checks `0 <= i < markerCount()` first.
- No Embind in `ARNFTTracker`; it compiles with `-fno-rtti` (AGENTS.md).
- `--embed-file` always written as `"--embed-file=<src>@<dst>"` (AGENTS.md).

## Review Focus

1. **Out-of-range marker index** (`getMarkerData(5)`, `getMarkerData(-1)`, `isMarkerTracked(5)`, `getTransformMatrix(5, m)`): empty data / `false` / zero matrix, never an abort → test `out_of_range_index_is_empty` (Task 1).
2. **Frames before markers or before setup** (the webcam starts before `init()` finishes, or `init()` failed): `update()` returns 0 and nothing crashes → tests `update_before_setup_is_safe`, `update_without_markers_is_safe` (Task 1).
3. **Missing assets** (`camera_para.dat`, marker files): `-1`, no abort, nothing half-loaded → tests `loads_camera_param`, `missing_marker_fails` (Task 1).
4. **Marker leaves the frame**: `isMarkerTracked(0)` turns false and the transform is zeroed (the cube disappears instead of freezing) → test `loses_tracking_on_blank_frame` (Task 1).
5. **Same frustum and pacing as `dev`**: near/far 10/5000, interval 200 ms, filtering on, so the cube keeps its size, clipping and smoothness → test `keeps_dev_tracking_settings` (Task 1).

---

### Task 1: WebARKitLib @ PR #94 + Node test runner pinning today's ARNFTTracker

**Files:**
- Modify: `third_party/WebARKitLib` (submodule → `6fd0a25`)
- Modify: `CMakeLists.txt` (drop `lib/SRC/WebARKit/WebARKitLog.cpp` from `WEBARKIT_EXTRA_SOURCES`; `nft_tests` target)
- Create: `tests/nft_tests.cpp`, `scripts/test-wasm.sh`
- Modify: `build-docker.js` (`--test`), `.github/workflows/build.yml` (test step), `AGENTS.md`, `CLAUDE.md`, `README.md`, `CHANGELOG.md`

**Interfaces:**
- Consumes: `ARNFTTracker` as on `dev` (unchanged in this task); `scripts/build-wasm.sh` and the CI job from #10.
- Produces: `option(WEBAR_CANVAS_BUILD_TESTS "Build the Node test runner (nft_tests)" OFF)`; target `nft_tests` (sources `tests/nft_tests.cpp`, links `ARNFTTracker`, link options `-sENVIRONMENT=node -sEXIT_RUNTIME=1 -sASSERTIONS=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sUSE_LIBJPEG=1 -sUSE_ZLIB=1 "--embed-file=${WEBARKITLIB_DIR}/tests/data@/data"`), output `build-wasm/nft_tests.js`, exit code = failed checks, last line `<N> checks, <F> failed`. `scripts/test-wasm.sh [jobs]`: configures `build-wasm` with `-DWEBAR_CANVAS_BUILD_TESTS=ON` (same flags as `build-wasm.sh`), builds `nft_tests`, runs `node build-wasm/nft_tests.js`. `node build-docker.js --test` runs it after the app build. Fixtures at `/data`: `camera_para.dat`, `pinball.{iset,fset,fset3}`, `pinball-demo.jpg` (2000×1500).

- [ ] **Step 1: Move the submodule**

```bash
git -C third_party/WebARKitLib fetch origin feat/nft-core
git -C third_party/WebARKitLib checkout 6fd0a250c132b64d4d30e3e0708b66d85a1eaaac
```

- [ ] **Step 2: Write the characterization tests** in `tests/nft_tests.cpp` (`CHECK(cond)` macro counting failures, a `kTests[]` table, `main` returns the failure count). Helper `bool fillFromJpeg(ARNFTTracker &t, const char *path)`: `ar2ReadJpegImage2` → RGB(A) expanded to RGBA in `t.getVideoFrame()`. Helper `bool trackPinball(ARNFTTracker &t)`: `loadCamera("/data/camera_para.dat")`, `setup(2000, 1500, cam)`, `setupAR2()`, `loadMarker("/data/pinball")`, `fillFromJpeg(t, "/data/pinball-demo.jpg")`, then `update()` up to 60 times until `isMarkerTracked(0)`.

```cpp
static void loads_camera_param() {
  ARNFTTracker t;
  CHECK(t.loadCamera("/data/camera_para.dat") >= 0);
  CHECK(t.loadCamera("/data/does-not-exist.dat") == -1);
}
static void missing_marker_fails() {
  ARNFTTracker t;
  const int cam = t.loadCamera("/data/camera_para.dat");
  CHECK(t.setup(640, 480, cam) == 0 && t.setupAR2() == 0);
  CHECK(t.loadMarker("/data/does-not-exist") == -1);
  CHECK(t.getMarkerCount() == 0);
}
static void update_before_setup_is_safe() {
  ARNFTTracker t;
  CHECK(t.update() == 0);
  CHECK(!t.isMarkerTracked(0));
}
static void update_without_markers_is_safe() {
  ARNFTTracker t;
  const int cam = t.loadCamera("/data/camera_para.dat");
  CHECK(t.setup(640, 480, cam) == 0 && t.setupAR2() == 0);
  CHECK(t.update() == 0);
}
static void keeps_dev_tracking_settings() {
  ARNFTTracker t;
  const int cam = t.loadCamera("/data/camera_para.dat");
  CHECK(t.setup(640, 480, cam) == 0);
  CHECK(t.getNearPlane() == 10.0 && t.getFarPlane() == 5000.0);
  CHECK(t.getCameraLens()[15] == 0.0 && t.getCameraLens()[11] < 0.0);
  CHECK(t.getDetectionInterval() == 200.0);
  CHECK(t.getContinuousDetection() && t.getFiltering());
}
static void out_of_range_index_is_empty() {
  ARNFTTracker t;
  CHECK(trackPinball(t));
  CHECK(t.getMarkerData(5).id == -1 && t.getMarkerData(-1).width == 0);
  CHECK(!t.isMarkerTracked(5));
  float m[16]; std::fill(m, m + 16, 1.0f);
  t.getTransformMatrix(5, m);
  CHECK(std::all_of(m, m + 16, [](float v) { return v == 0.0f; }));
}
static void tracks_pinball_demo() {
  ARNFTTracker t;
  CHECK(trackPinball(t));
  const NFTMarkerData &d = t.getMarkerData(0);
  CHECK(d.id == 0 && d.width == 893 && d.height == 1117 && d.dpi == 120);
  float m[16];
  t.getTransformMatrix(0, m);
  CHECK(m[14] < 0.0f);   // marker in front of the camera (GL, right-handed)
  CHECK(m[15] == 1.0f);
}
static void loses_tracking_on_blank_frame() {
  ARNFTTracker t;
  CHECK(trackPinball(t));
  std::memset(t.getVideoFrame(), 0, t.getVideoFrameSize());
  t.update();
  CHECK(!t.isMarkerTracked(0));
}
```

- [ ] **Step 3: Verify it fails** — `node build-docker.js --test`. Expected: failure because `--test`, `scripts/test-wasm.sh` or the `nft_tests` target does not exist yet.

- [ ] **Step 4: Wire it** — the CMake option and target, `scripts/test-wasm.sh` (LF, executable), `--test` in `build-docker.js` (after `build-wasm.sh`: `sh /src/scripts/test-wasm.sh ${jobs}`), a `Run the NFT tests` step in `.github/workflows/build.yml` after `Build WebAR-Canvas` (`run: sh scripts/test-wasm.sh`). Remove `${WEBARKITLIB_DIR}/lib/SRC/WebARKit/WebARKitLog.cpp` from `WEBARKIT_EXTRA_SOURCES`: at `6fd0a25` `WebARKitNFT` already compiles `WebARKit/WebARKitLog.cpp`.

- [ ] **Step 5: Verify it passes on today's ARNFTTracker** — `node build-docker.js --test`. Expected: app built, last line `N checks, 0 failed`. A check that fails here describes `dev` wrongly: fix the test, not the tracker (rule it in the ledger).

- [ ] **Step 6: Browser check** — `python -m http.server 8000 --directory build-wasm`, `videoTest.html`, pinball marker. Expected: as on `dev`: detection, tracking, cube on the marker.

- [ ] **Step 7: Docs and commit** — AGENTS.md §4/§6, CLAUDE.md, README "Building": `node build-docker.js --test`. CHANGELOG `Unreleased`: Added "Node test runner `nft_tests`, run in CI (#9)"; Changed "WebARKitLib → PR #94 head `6fd0a25`".

```bash
git add third_party/WebARKitLib CMakeLists.txt tests/nft_tests.cpp scripts/test-wasm.sh build-docker.js .github/workflows/build.yml AGENTS.md CLAUDE.md README.md CHANGELOG.md
git commit -m "test(nft): pin ARNFTTracker behaviour with a Node test runner on WebARKitLib PR #94"
```

---

### Task 2: ARNFTTracker becomes an adapter over ARToolKitNFTCore

**Files:**
- Modify: `src/nft/ARNFTTracker.h`, `src/nft/ARNFTTracker.cpp`
- Modify: `AGENTS.md` (§6 console lines, if the log text changes), `CHANGELOG.md`
- Test: `tests/nft_tests.cpp` (unchanged: it must stay green)

**Interfaces:**
- Consumes: the Task 1 tests; `ARToolKitNFTCore`, `NFTTrackingConfig` (`singleThreadPreset()`), `NFTMarkerState`, `nftMarker`.
- Produces — public API kept, same signatures: `loadCamera`, `setup`, `setCamera`, `setupAR2`, `teardown`, `loadMarker`, `loadMarkers`, `getVideoFrame` (const and non-const), `getVideoFrameSize`, `getVideoLuma`, `update`, `isMarkerTracked`, `anyMarkerTracked`, `allMarkersTracked`, `getTrackingError`, `getTransformMatrix`, `getPose`, `set/getContinuousDetection`, `set/getDetectionInterval`, `set/getFiltering`, `getMarkerCount`, `getMarkerData`, `getMarkerState(int) const`, `getCameraLens`, `setProjectionPlanes`, `getNearPlane`, `getFarPlane`, `recalculateCameraLens`, `getWidth`, `getHeight`, `getCameraParam`, `getCameraParamLT`, `set/getLogLevel`, `NFTMarkerData`, `NFT_PAGES_MAX`. **Removed** (internal to the core, or no core equivalent): `updateVideoLuma`, `detectNFTMarker`, `trackMarkers`, `getSurfaceSet`, `setFilterParameters`, non-const `getMarkerState`.

- [ ] **Step 1: Rewrite `src/nft/ARNFTTracker.{h,cpp}`** — private members: `ARToolKitNFTCore core{singleThreadPreset(), true};`, `int coreId = -1;`, the RGBA buffer `std::unique_ptr<ARUint8[]> videoFrame` + `videoFrameSize`, `webarkit::WebARKitLumaInfo *lumaInfo`, `width`/`height`, and mirrors of the three settings for the getters (`continuousDetection = true`, `detectionIntervalMs = 200.0`, `withFiltering = true`) pushed to the core in the constructor; the constructor also sets near/far 10.0/5000.0 on the core. Mapping:
  - `loadCamera` → `ARToolKitNFTCore::loadCamera`; `setup` → `core.setup()`, then `-1` when `core.cameraParamLT() == nullptr`, else allocate the RGBA buffer and `lumaInfo` and return 0; `setCamera(cam)` → `core.setCamera(coreId, cam)`; `setupAR2` → `core.setupAR2()`; `loadMarkers` → `core.addNFTMarkers()`; `loadMarker` → first id or `-1`.
  - `update()`: `0` when there is no RGBA buffer; else luma via `webarkitVideoLuma(lumaInfo, frame)`, `core.setVideoFrame(frame, luma)`, `core.detectNFTMarker()`, return `anyMarkerTracked() ? 1 : 0`.
  - Tracking queries read `core.markerState(i)` (null → not tracked, error `-1.0f`, zero pose); `getTransformMatrix` = `arglCameraViewRH(state->pose, gl, 1.0)` → `float`, zeros when not tracked; `getMarkerData(i)` checks `0 <= i < core.markerCount()` before `core.getNFTData(i)` and maps `id_NFT/width_NFT/height_NFT/dpi_NFT`, otherwise returns `NFTMarkerData{}`.
  - Camera: `getCameraLens` → `core.cameraLens()`; `setProjectionPlanes` → core setters + `recalculateCameraLens()`; near/far getters → core getters; `getCameraParam` → `&core.cameraParam()`; `getCameraParamLT` → `core.cameraParamLT()`.
  - `teardown()` → `core.teardown()`, then free the RGBA buffer and `lumaInfo`.
  - `ARNFTTracker.h` includes `<WebARKitTrackers/WebARKitNFT/ARToolKitNFTCore.h>`, `<AR/paramGL.h>`, `<WebARKitVideoLuma.h>`; `trackingMod.h` and `markerDecompress.h` are no longer included.

- [ ] **Step 2: Verify the tests stay green** — `node build-docker.js --test`. Expected: `N checks, 0 failed` with the same N as Task 1.

- [ ] **Step 3: Browser check** — as Task 1 Step 6, plus the console: camera, marker `893x1117 @ 120 DPI`, the frustum with the same values as `dev` (`-1.0040 -20.0401` in the third row), and no errors. If the log text changed, update the AGENTS.md §6 list.

- [ ] **Step 4: CHANGELOG and commit** — `Unreleased`: Changed "`ARNFTTracker` delegates camera, markers, KPM detection, AR2 tracking and filtering to `ARToolKitNFTCore` (WebARKitLib PR #94)"; Removed: the six methods listed in Interfaces.

```bash
git add src/nft/ARNFTTracker.h src/nft/ARNFTTracker.cpp AGENTS.md CHANGELOG.md
git commit -m "refactor(nft): make ARNFTTracker an adapter over ARToolKitNFTCore"
```

---

## Out of scope

- The threaded preset (`WEBARKIT_NFT_THREADS=ON`: `-pthread`, COOP/COEP headers).
- The extra RGBA copy per frame (adapter buffer → core buffer, ~1.2 MB at 640×480): measure it under #4 before optimising.
- The Debug build with the global `-fno-rtti` (`ASSERT(dynamic_cast)` in KPM when `NDEBUG` is undefined): already true on `dev`.
- Moving the submodule to a WebARKitLib release tag once PR #94 is released.
