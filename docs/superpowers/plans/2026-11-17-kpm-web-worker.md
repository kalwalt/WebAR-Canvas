# KPM in a Web Worker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the KPM detection pass of `ARToolKitNFTCore` in a Web Worker with its own small WASM instance, so the main thread no longer stalls for ~45 ms per camera frame while no marker is tracked (#16). The app keeps working, on the main thread as today, wherever the worker cannot start or stops answering.

**Architecture:**
- **Detector:** `ARNFTTracker` builds its core with `Detector::External` and a factory that returns a `WorkerKpmDetector`. That detector implements WebARKitLib's `NFTDetector` and talks, through a small transport interface, to a dedicated worker module (`videoTest.kpm.js/.wasm`) that only does KPM matching.
- **Data:** the detector receives the camera (by value) and the core's KPM handle from the factory when the core creates it, and the accepted markers from the `markersAdded` hook (webarkit/WebARKitLib#115). It sends the worker the bytes it already has in the main instance's file system, so nothing is downloaded twice.
- **Fallback:** on failure, the detector switches itself to an in-process synchronous KPM pass, so tracking continues.

**Tech Stack:** C++17, Emscripten 4.0.17 (Docker), WebARKitLib `ARToolKitNFTCore` (+ #115), plain Web Workers and `postMessage` with transferable `ArrayBuffer`s (no `SharedArrayBuffer`, no pthreads), CMake, GitHub Actions.

**Spec:**
- Issue #16, and its decision comment of 2026-11-17 (the Web Worker option, with the threaded option kept as the proven fallback).
- webarkit/WebARKitLib#115.
- Stage A and the start of Stage B (spike results in `D:/kalwalt-github/WebAR-Canvas-runs/16-worker/`, outside the repository).

## Global Constraints

- No Embind in engine classes; `-fno-rtti`; `--embed-file=<src>@<dst>` syntax (AGENTS.md sections 2 and 4).
- `ARNFTTracker` keeps its public API. The engine keeps `update()` and `render()` on the main thread. `ARBaseEngine` does not change.
- The tracking behaviour stays today's: `singleThreadPreset()` settings (single-thread AR2, pose filtering on, 200 ms detection interval). Only the detector changes. `threadedPreset()` is not used.
- Both renderers build (`filament`, `sokol`), and both CI jobs (`wasm`, `wasm-sokol`) stay green after every task.
- Tests first wherever there is behaviour (`nft_tests`, `core_tests`, and the new `kpm_worker_tests`).
- Never commit a submodule pointer to an unmerged WebARKitLib commit (see "Developing while #115 is open").
- Conventional Commits, `For #16` in bodies, signed, PR to `dev`.

## Developing while webarkit/WebARKitLib#115 is open

> **Status (2026-11-17):** webarkit/WebARKitLib#116 (the amended #115) is merged. The submodule now sits on a **provisional** `dev` pin, `687f329`, because no WebARKitLib release contains #116 yet; a final commit moves it to that release before the PR merges. Tasks 1–4 are done. What follows describes the work while #115 was open.

The submodule pinned `c3ede5d`, which had `NFTDetector` but not `Detector::External`, the factory or the hooks. So:

- **What can merge-ready without #115** (Tasks 1–3):
  - the worker module and its tests;
  - `WorkerKpmDetector` against the existing `NFTDetector` interface, tested with a fake transport;
  - the JS transport and the build integration.
  - These compile and pass CI at the pinned commit. The #115-dependent code is written behind a feature macro (`WEBARKIT_NFT_HAS_EXTERNAL_DETECTOR`) that #115 defines and this repository only reads, so the code builds both before and after #115.
- **What needs #115** (Task 4): selecting `Detector::External` in `ARNFTTracker`, the factory and the `markersAdded` hook.
  - **Task 4 does not start until #115 is updated to the shape below and merged.** The core creates the detector lazily, in the first `addNFTMarkers()` after `setupAR2()`, so the camera has to reach the detector at creation.
  - **Development:** a local WebARKitLib branch implementing #115, checked out in this worktree's `third_party/WebARKitLib` only, never pushed from here and never committed as the submodule pointer. Local builds use it; the branch's commits keep the pinned `c3ede5d`.
  - Implementing #115 itself is the maintainer's work upstream (or a separate upstream PR the maintainer requests). This plan only consumes it.
- **Before the PR can merge:**
  1. #115 is merged upstream, ideally in a WebARKitLib release (the #11 note applies: nothing reaches `master` before the core is released).
  2. A commit on this branch bumps the submodule to that merged commit or tag.
  3. With the bump, `WEBARKIT_NFT_HAS_EXTERNAL_DETECTOR` is defined and Task 4's code path is compiled and tested in both CI jobs.
  4. If #115's final signatures differ from the ones below, Task 4 is adapted before the bump commit.
- **Until then:** the PR can be opened as a draft with Tasks 1–3, CI green at the pinned commit, and the worker path compiled out, so nothing changes at run time. It is marked ready only after the bump.

## The shape of #115 that Task 4 consumes

The maintainer is amending #115 to this shape (2026-11-17), from the findings below. To confirm against the merged code before Task 4 starts:

- `NFTTrackingConfig::Detector::External`, with a factory and a context pointer in the config.
- **The factory receives, at creation:** the context (`makeDetectorUser`), the core's KPM handle (for the in-process fallback), and the current camera as an `ARParam` by value, already resized to the frame (the core calls `arParamChangeSize()` before it builds `paramLT`). No pointer into the core's heap other than the KPM handle, which only the main-thread fallback uses.
- **`markersAdded(pages, datasetPathnames)`:** called once at creation with every marker loaded so far, then with each new batch only.
- **No `cameraChanged` hook.**

**Why this shape** (checked against the pinned core, `ARToolKitNFTCore.cpp` at `c3ede5d`):
- The detector is destroyed in `setupAR2()`, `deleteCamera()` (which `setCamera()` calls) and `teardown()`. A camera change always goes through `setupAR2()`, so a live detector never sees one: a `cameraChanged` hook for later changes would never fire, and the camera is delivered at creation instead.
- `kpmHandle` is private, and the fallback runs on it.
- After `setupAR2()` the core hands its new KPM handle every marker loaded so far, so a recreated detector needs all of them too, not only the new batch.

## Design

### The worker module (`kpm_worker`)

- **Target:** a separate CMake target, `kpm_worker`, output `videoTest.kpm.js/.wasm` next to `videoTest.*`. It links `WebARKitNFT` and uses only the KPM, AR (camera) and ICP code.
- **Link flags:** `-O3`, **no `-g`**, `-sENVIRONMENT=worker,node` (`node` only so `kpm_worker_tests` can load the same module), `-sINITIAL_MEMORY=32MB` with growth, no embedded files.
- **Size:** the Stage B spike measured 160,497 B `.wasm` + 62,683 B `.js`, 86 KB gzipped. With `-g` it would be 637 KB gzipped, which is why no `-g`.
- **C entry points:** `kw_init(ARParam*)` (creates `ARParamLT` and the KPM handle), `kw_set_markers(...)` (loads the `.fset3` bytes the main instance sent, with the core's page numbers), `kw_match(luma, skipPages, skipNum)`, and result accessors.
- **The worker's JS** (`src/worker/kpm_worker_main.js`, as `--pre-js`) owns `onmessage` and calls those entry points.

### Who sends what, and who owns what (message protocol)

All messages are plain objects. Every binary payload is an `ArrayBuffer` in the transfer list, so it changes owner instead of being copied twice.

| direction | message | payload | ownership |
|---|---|---|---|
| main → worker | `init` | `camera`: the resized `ARParam` serialised as numbers (xsize, ysize, mat[3][4], dist_factor[9], dist_function_version) | copied (small) |
| worker → main | `ready` | none | none |
| main → worker | `markers` | `pages: int[]`, `datasets: ArrayBuffer[]` (each marker's `.fset3` bytes) | transferred; the worker owns and frees them after loading |
| main → worker | `match` | `gen`, `skipPages: int[]`, `luma: ArrayBuffer` (width × height bytes) | `luma` transferred to the worker |
| worker → main | `result` | `gen`, `resultNum`, `detections: [{page, trans[12]}]`, `luma` (the same buffer) | `luma` transferred back to the main side's pool |
| worker → main | `error` | `message` | none |

**Marker and camera bytes come from the main instance, never from the network:**
- `pinball.fset3` is embedded in the main build (`CMakeLists.txt`, the `--embed-file` for `assets/pinball.fset3`).
- On `markersAdded(pages, paths)` (at creation with every marker, later with new batches), the detector reads `<path>.fset3` from the main instance's file system (`FS.readFile`) and sends the bytes. The worker writes them into its own in-memory FS, or hands them straight to KPM, and loads them with the core's page numbers.
- The camera goes the same way: the factory gives the already resized `ARParam` by value, the detector serialises its fields, and the worker builds `ARParamLT` and the KPM handle from them.
- The worker never opens a URL except its own `.wasm`.
- **Order:** `init` → `ready` → `markers`, then passes. A `markers` message during a pass is queued behind the pass.
- **Detector recreation:** the core destroys the detector on every `setCamera()` / `setupAR2()` and creates a new one in the next `addNFTMarkers()`. The worker, its transport and the fallback state are owned by `ARNFTTracker` (passed as `makeDetectorUser`) and outlive detectors, so a recreation does not respawn the worker or rerun the 5 s start-up. A new detector sends `init` again (the camera may have changed) and then all markers again, which the worker loads into a fresh KPM handle.

**Luma buffer ownership:**
- The core's `videoLuma` stays the core's. `start(luma, ...)` copies it into a pooled `ArrayBuffer` (two buffers, ping-pong) and transfers that buffer.
- From the transfer until `result` brings it back, the main side cannot touch it (it is detached).
- An abandoned pass's buffer comes back with its stale result and returns to the pool. A buffer lost with a dead worker is simply replaced on the next allocation.
- The copy is about 0.3 MB per pass at 640×480, once per pass (not per frame).

### Generation counter (late results of abandoned passes)

- The detector holds `gen`. `start()` increments it and tags the `match` message.
- `waitIdle()` is called by the core's `dropRunningSearch()` before a new KPM handle, a camera change, or a new marker set. It does not block: it increments `gen`, marks the detector idle, and remembers nothing of the pass.
- A `result` whose `gen` differs from the current one is dropped: its buffer goes back to the pool and nothing reaches `collect()`. This is the case #115's reworded `waitIdle()` describes; its page numbers may refer to the old marker set, so it must never be applied.
- `collect()` returns a result only once, and only for the current `gen`.

### Worker failure and fallback

`WorkerKpmDetector` has three states: `Starting`, `Worker`, `Fallback`.

**It switches to `Fallback` when any of these happens:**
- `Worker` is unavailable, or construction throws;
- no `ready` within 5 s of `init`;
- an `error` message or `onerror` / `onmessageerror` event;
- no `result` within 2 s of a `match` (the watchdog; a pass is ~45 ms on the desktop, and phones get generous headroom);
- the page sets `Module.webarConfig.kpmWorker = false` (also used for A/B measurements).

**In `Fallback`:**
- The worker is terminated, and the detector runs its passes on **the core's own KPM handle**, which the core builds in `setupAR2()` whatever the detector type, and fills with every accepted marker in `addNFTMarkers()`. No second handle, no re-reading of `.fset3`, no page numbering of our own.
- `start()` then runs `kpmMatching()` on that handle synchronously and returns true, exactly like `SyncKpmDetector`: today's behaviour, with today's stall.
- The detector receives the handle as a nullable `KpmHandle *` (Task 2 tests pass it directly; in the app it comes from #115, requirement 2). With a null handle the fallback has nothing to run on: it logs it and detects nothing, rather than crashing.
- The switch is logged once: `KPM worker unavailable (<reason>): running KPM on the main thread`.
- `Fallback` is final for the session (no retry loop).

**`Starting`:** passes are refused. `idle()` stays true, so the core keeps calling `start()` every frame and the first pass runs as soon as `ready` arrives (or in fallback after the 5 s timeout). Tracking of already tracked markers is unaffected, as today.

### Memory

- **Worker:** 32 MB initial memory (grows if needed), its 160 KB of code, and the KPM reference data. For pinball, the `.fset3` is 640,324 B on disk; the loaded data is of the same order (to be measured in Task 5 from the worker heap). It grows linearly per marker (kuva's `.fset3` is 343 KB).
- **Main instance:** unchanged. The core still loads every marker's KPM data into its own KPM handle (`createKpmHandle()` in `setupAR2()` runs whatever the detector type), and while the worker runs, that handle is idle. **This is not a free upstream optimisation:** the idle handle is exactly what the in-process fallback runs on. If the core stopped building it for `External`, the fallback to a synchronous pass would no longer work without rebuilding a handle on the main side after a failure. The two ideas conflict, and this plan keeps the handle. The `.fset3` bytes are read and copied once per marker for the worker, then released.
- **In `Fallback`:** no extra memory on the main side; the worker's memory is released when it is terminated.

### Runtime switch and build option

- **CMake `WEBAR_KPM_WORKER`** (default `ON` once #115 is in; `OFF` builds exactly today's app, with no worker files). With `ON`, `WebAR-Canvas` depends on `kpm_worker`, and both builds' outputs include `videoTest.kpm.js/.wasm`.
- **`Module.webarConfig.kpmWorker`** (default `true`), and `?kpmWorker=0` in `src/shell.html` sets it. This forces the main-thread path without rebuilding, for A/B checks on a device.
- **Worker URL:** resolved next to `videoTest.js` (Emscripten's `scriptDirectory`), overridable with `Module.webarConfig.kpmWorkerUrl`.

## Measurements (Task 5)

Harness from Stage A (`D:/kalwalt-github/WebAR-Canvas-runs/16-worker/`, outside the repository), headed Chrome on the real GPU, window uncovered. **Builds:** `dev` vs this branch, both renderers. 6 alternating runs per configuration.

- **Searching (`lake` scene), with the #15 probe:** main-thread tick median, p95 and worst, long frames per 10 s, camera frames processed per second. Before vs after, both renderers.
- **Time to first pose:** a new harness scene, `enter`, that shows `lake` for 4 s and then switches to the marker image. It records the time from the switch to the first `found` event and to the first pose line; median over 6 runs, before vs after. A worker pass ends a frame or two after it starts, so this is expected to be equal or slightly longer than today. Whatever it is, it is stated.
- **Fallback:**
  - with `?kpmWorker=0`, and with the worker URL pointing at a missing file;
  - checked: the app tracks; the log line appears; searching shows today's stall (so the numbers match `dev`).
- **Sizes:** `videoTest.*` and `videoTest.kpm.*`, raw and gzip, both renderers, from the same commit.
- **Worker memory:** the worker's heap size after loading pinball.

## Tasks

### Task 1: The KPM worker module, tested in Node (no #115 needed)

- [x] `tests/kpm_worker_tests.cpp`, failing first. Loads `kpm_worker` the way the worker does (`kw_init` with the resized `camera_para.dat` param for 2000×1500, `kw_set_markers` with `/data/pinball.fset3`'s bytes at page 0) and checks:
  - `kw_match` on `pinball-demo.jpg`'s luma finds page 0 with a pose close to the one `nft_tests` gets (z < 0);
  - a blank frame finds nothing;
  - skipping page 0 finds nothing.
- [x] `src/worker/kpm_worker.cpp`, the `kpm_worker` target (flags above), wired into `scripts/test-wasm.sh`.
- [x] Both renderers: `node build-docker.js --renderer sokol --test` and `node build-docker.js --test` green.
- [x] Commit: `feat(nft): add a KPM-only worker module`.

### Task 2: `WorkerKpmDetector` against a fake transport (no #115 needed)

- [x] `nft_tests`, failing first, with a fake transport the test drives by hand:
  - start → result → collect;
  - a result never arrives twice;
  - `waitIdle()` then a late result: dropped, buffer back in the pool, next pass works (generation);
  - camera change and markers re-sent in order;
  - no `ready` within the timeout → fallback; `error` → fallback; watchdog → fallback;
  - in fallback, `start()` returns true and a pose is collected on `pinball-demo.jpg`;
  - `kpmWorker = false` → fallback at once.
- [x] `src/nft/WorkerKpmDetector.{h,cpp}` and `src/nft/KpmTransport.h` (the interface: `init`, `markers`, `match`, `terminate`, and an inbox polled at `collect()`/`idle()`). The timeouts read a clock injected like `NFTTrackingConfig::clock`, so the tests control time.
  - The detector borrows a `KpmWorkerLink` owned by the caller (the transport, the start-up / worker / fallback state, the generation counter and the buffer pool), so the state outlives a detector, and takes a nullable `KpmHandle *` for the fallback. The fallback tests pass a handle built in the test from `camera_para.dat` and `pinball.fset3`, so they need no #115.
- [x] The hook overrides behind `WEBARKIT_NFT_HAS_EXTERNAL_DETECTOR`.
- [x] Commit: `feat(nft): add a worker-backed KPM detector with a main-thread fallback`.

### Task 3: The browser transport and the build integration (no #115 needed)

- [x] `src/nft/WorkerTransport.cpp`: `EM_JS` glue that creates the `Worker`, posts messages with transfer lists, and delivers incoming messages into the detector's inbox through one exported C function. `src/worker/kpm_worker_main.js` on the worker side.
- [x] CMake: `WEBAR_KPM_WORKER`, the dependency, and the output names. `build-docker.js` needs no change: the worker is built with the main target.
- [x] `.github/workflows/build.yml`: the artifacts of both jobs include `videoTest.kpm.js/.wasm`. `.github/workflows/pages.yml`: the "Collect the site" step copies them.
- [x] `src/shell.html`: `?kpmWorker=0`.
- [x] A browser check with a test page that drives `WorkerTransport` directly against the real worker (init, markers, match on `pinball-demo.jpg`): a pose comes back; the buffer returns.
- [x] Commit: `feat(nft): run the KPM worker from the browser`.

### Task 4: Wire it into `ARNFTTracker` (needs #115; local WebARKitLib branch until it merges)

**Gate:** Task 4 starts only when #115 is updated to the shape above and merged. Until then the work stops after Task 3.

- [x] `nft_tests`, failing first:
  - with `WEBAR_KPM_WORKER` and a fake transport injected through a test hook, `ARNFTTracker` tracks `pinball-demo.jpg`; the factory delivers the 2000×1500 camera and the core's KPM handle, `markersAdded` delivers page 0, and the pose matches the sync path within the filter's tolerance;
  - after a second `setup()` / `setupAR2()` with a marker already loaded, the new detector gets the new camera and every loaded marker at creation, with no respawned worker;
  - with the option off, behaviour is byte-for-byte today's (the existing tests unchanged).
- [x] `WorkerKpmDetector`: the constructor takes the camera from the factory and sends it through the link; the `cameraChanged` override written behind `WEBARKIT_NFT_HAS_EXTERNAL_DETECTOR` in Task 2 is removed, and `markersAdded` stays.
- [x] `ARNFTTracker`: the config is `singleThreadPreset()` with `detector = External` and the factory, with the tracker (which owns the `KpmWorkerLink` and its `WorkerTransport`) as context, when the option is on. The app's `EXPORTED_FUNCTIONS` gains `_webar_kpm_deliver`, `_malloc` and `_free`, now that `WorkerTransport` is linked in. The public API is unchanged.
- [x] Commit: `perf(nft): run KPM in a Web Worker`. The submodule bump comes as its own commit, only once #115 is merged.

### Task 5: Measurements, docs, review, PR

- [ ] The measurements above. Raw results in `D:/kalwalt-github/WebAR-Canvas-runs/16-worker/`.
- [ ] Docs:
  - AGENTS.md: the KPM worker in section 2D, the option and outputs in section 4, the fallback and the protocol;
  - README: the option and the `?kpmWorker=0` switch;
  - `docs/template.md`: deploying the extra two files.
- [ ] `/verify-wasm` on both renderers.
- [ ] One independent read-only review and the `wasm-reviewer` agent; fix the findings.
- [ ] Push; PR to `dev`, "For #16". It contains:
  - the option taken and why;
  - the before/after tables;
  - the fallback behaviour;
  - the dependency on #115 and the bump;
  - the device procedure below;
  - what is not covered.

## Device procedure for the maintainer (real webcam and tablet)

I cannot test a real camera or a phone. The PR will carry this, refined with the final file names:

1. **Build:** `node build-docker.js` on the PR branch (with the #115 bump).
2. **Desktop webcam:**
   - Serve: `python -m http.server 8000 --directory build-wasm`.
   - Open `http://localhost:8000/videoTest.html`. Look for `KPM worker ready` in the console and no `KPM worker unavailable` line.
   - With the marker out of view, the scene must stay smooth (no ~50 ms hitches).
   - In DevTools, set `Module.webarProfile = { enabled: true }`, wait 10 s, run the snippet from the PR (it prints median, p95 and worst tick), and compare with `?kpmWorker=0`.
3. **Android tablet** (the camera needs a secure origin):
   - Connect over USB with Chrome remote debugging and run `adb reverse tcp:8000 tcp:8000`, so `http://localhost:8000/videoTest.html` on the tablet reaches the PC.
   - Same checks; also point the camera at the printed marker and note how long until the cube appears, with and without `?kpmWorker=0`.
4. **Report back:** the three numbers per run, the time to the cube, the device and browser version, and whether `KPM worker unavailable` ever appeared.

**Open questions for the device run:**
- **The 2 s pass watchdog.** A main-thread KPM pass took 0.6–0.7 s at 2000×1500 on the development desktop (Task 3's browser check). A phone running the worker at a large camera size could come close enough to 2 s to trip the fallback for no reason. Not changed now: measured on the device first (the console shows `KPM worker unavailable (a pass got no result within 2 s)` if it trips; the PR's snippet also prints the worker's pass times).

## Not in this plan

- The threaded (pthreads) option: kept as the proven fallback (spike diff outside the repository). It is used only if #115 stalls, after a new decision.
- Removing the core's KPM handle in `External` mode. It is not unused: it is the fallback's handle, so removing it conflicts with the fallback (see "Memory").
- `-g` on the main app: measured separately, its own decision.
- Moving AR2 tracking or the capture off the main thread.
