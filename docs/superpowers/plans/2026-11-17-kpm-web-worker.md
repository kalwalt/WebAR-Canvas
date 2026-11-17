# KPM in a Web Worker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the KPM detection pass of `ARToolKitNFTCore` in a Web Worker with its own small WASM instance, so the main thread no longer stalls for ~45 ms per camera frame while no marker is tracked (#16). The app keeps working, on the main thread as today, wherever the worker cannot start or stops answering.

**Architecture:**
- **Detector:** `ARNFTTracker` builds its core with `Detector::External` and a factory that returns a `WorkerKpmDetector`. That detector implements WebARKitLib's `NFTDetector` and talks, through a small transport interface, to a dedicated worker module (`videoTest.kpm.js/.wasm`) that only does KPM matching.
- **Data:** the detector learns the camera and the accepted markers from the core's hooks (`cameraChanged`, `markersAdded`, webarkit/WebARKitLib#115). It sends the worker the bytes it already has in the main instance's file system, so nothing is downloaded twice.
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

The submodule pins `c3ede5d`, which has `NFTDetector` but not `Detector::External`, the factory or the hooks. So:

- **What can merge-ready without #115** (Tasks 1–3):
  - the worker module and its tests;
  - `WorkerKpmDetector` against the existing `NFTDetector` interface, tested with a fake transport;
  - the JS transport and the build integration.
  - These compile and pass CI at the pinned commit. The hook overrides are written behind a feature macro (`WEBARKIT_NFT_HAS_EXTERNAL_DETECTOR`) that #115 defines and this repository only reads, so the code builds both before and after #115.
- **What needs #115** (Task 4): selecting `Detector::External` in `ARNFTTracker` and receiving the hooks.
  - **Development:** a local WebARKitLib branch implementing #115, checked out in this worktree's `third_party/WebARKitLib` only, never pushed from here and never committed as the submodule pointer. Local builds use it; the branch's commits keep the pinned `c3ede5d`.
  - Implementing #115 itself is the maintainer's work upstream (or a separate upstream PR the maintainer requests). This plan only consumes it.
- **Before the PR can merge:**
  1. #115 is merged upstream, ideally in a WebARKitLib release (the #11 note applies: nothing reaches `master` before the core is released).
  2. A commit on this branch bumps the submodule to that merged commit or tag.
  3. With the bump, `WEBARKIT_NFT_HAS_EXTERNAL_DETECTOR` is defined and Task 4's code path is compiled and tested in both CI jobs.
  4. If #115's final signatures differ from the ones assumed here (the `cameraChanged` argument is still open), Task 4 is adapted before the bump commit.
- **Until then:** the PR can be opened as a draft with Tasks 1–3, CI green at the pinned commit, and the worker path compiled out, so nothing changes at run time. It is marked ready only after the bump.

## Assumed shape of #115 (to confirm when it lands)

- `NFTTrackingConfig::Detector::External`, `std::unique_ptr<NFTDetector> (*makeDetector)(void *user)`, `void *makeDetectorUser`.
- `virtual void markersAdded(const std::vector<int> &pages, const std::vector<std::string> &datasetPathnames)`.
- `virtual void cameraChanged(const ARParam &param)`. This repository asks #115 for an `ARParam` by value, already resized to the frame (the core calls `arParamChangeSize()` before it builds `paramLT`). A plain struct of a few hundred bytes, with no pointer into the core.
- **One requirement to add to #115:** the core creates the detector lazily in the first `addNFTMarkers()`, after the camera is set. So `cameraChanged()` must also be called once on a newly created detector when a camera exists; otherwise a worker never learns the first camera. I'll raise this on #115 (a comment, the maintainer's call).

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
- On `markersAdded(pages, paths)`, the detector reads `<path>.fset3` from the main instance's file system (`FS.readFile`) and sends the bytes. The worker writes them into its own in-memory FS, or hands them straight to KPM, and loads them with the core's page numbers.
- The camera goes the same way: `cameraChanged(param)` gives the already resized `ARParam` by value, the detector serialises its fields, and the worker builds `ARParamLT` and the KPM handle from them.
- The worker never opens a URL except its own `.wasm`.
- **Order:** `init` → `ready` → `markers`, then passes. A `markers` message during a pass is queued behind the pass. A camera change restarts the sequence: `init` again, then all markers again.

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
- The worker is terminated, and the detector creates its own in-process KPM handle from the last `ARParam` and the `.fset3` files of the accepted markers (read from the main FS).
- `start()` then runs `kpmMatching()` synchronously and returns true, exactly like `SyncKpmDetector`: today's behaviour, with today's stall.
- The switch is logged once: `KPM worker unavailable (<reason>): running KPM on the main thread`.
- `Fallback` is final for the session (no retry loop).

**`Starting`:** passes are refused. `idle()` stays true, so the core keeps calling `start()` every frame and the first pass runs as soon as `ready` arrives (or in fallback after the 5 s timeout). Tracking of already tracked markers is unaffected, as today.

### Memory

- **Worker:** 32 MB initial memory (grows if needed), its 160 KB of code, and the KPM reference data. For pinball, the `.fset3` is 640,324 B on disk; the loaded data is of the same order (to be measured in Task 5 from the worker heap). It grows linearly per marker (kuva's `.fset3` is 343 KB).
- **Main instance:** unchanged. The core still loads every marker's KPM data into its own KPM handle, which the `External` detector does not use. That duplicate is the core's behaviour; avoiding it would be an upstream follow-up, not this issue. The `.fset3` bytes are read and copied once per marker at start-up, then released.
- **In `Fallback`:** the detector's own KPM handle adds one more copy of the reference data on the main side, only after a failure.

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

- [ ] `tests/kpm_worker_tests.cpp`, failing first. Loads `kpm_worker` the way the worker does (`kw_init` with the resized `camera_para.dat` param for 2000×1500, `kw_set_markers` with `/data/pinball.fset3`'s bytes at page 0) and checks:
  - `kw_match` on `pinball-demo.jpg`'s luma finds page 0 with a pose close to the one `nft_tests` gets (z < 0);
  - a blank frame finds nothing;
  - skipping page 0 finds nothing.
- [ ] `src/worker/kpm_worker.cpp`, the `kpm_worker` target (flags above), wired into `scripts/test-wasm.sh`.
- [ ] Both renderers: `node build-docker.js --renderer sokol --test` and `node build-docker.js --test` green.
- [ ] Commit: `feat(nft): add a KPM-only worker module`.

### Task 2: `WorkerKpmDetector` against a fake transport (no #115 needed)

- [ ] `nft_tests`, failing first, with a fake transport the test drives by hand:
  - start → result → collect;
  - a result never arrives twice;
  - `waitIdle()` then a late result: dropped, buffer back in the pool, next pass works (generation);
  - camera change and markers re-sent in order;
  - no `ready` within the timeout → fallback; `error` → fallback; watchdog → fallback;
  - in fallback, `start()` returns true and a pose is collected on `pinball-demo.jpg`;
  - `kpmWorker = false` → fallback at once.
- [ ] `src/nft/WorkerKpmDetector.{h,cpp}` and `src/nft/KpmTransport.h` (the interface: `init`, `markers`, `match`, `terminate`, and an inbox polled at `collect()`/`idle()`). The timeouts read a clock injected like `NFTTrackingConfig::clock`, so the tests control time.
- [ ] The hook overrides behind `WEBARKIT_NFT_HAS_EXTERNAL_DETECTOR`.
- [ ] Commit: `feat(nft): add a worker-backed KPM detector with a main-thread fallback`.

### Task 3: The browser transport and the build integration (no #115 needed)

- [ ] `src/nft/WorkerTransport.cpp`: `EM_JS` glue that creates the `Worker`, posts messages with transfer lists, and delivers incoming messages into the detector's inbox through one exported C function. `src/worker/kpm_worker_main.js` on the worker side.
- [ ] CMake: `WEBAR_KPM_WORKER`, the dependency, and the output names. `build-docker.js` needs no change: the worker is built with the main target.
- [ ] `.github/workflows/build.yml`: the artifacts of both jobs include `videoTest.kpm.js/.wasm`. `.github/workflows/pages.yml`: the "Collect the site" step copies them.
- [ ] `src/shell.html`: `?kpmWorker=0`.
- [ ] A browser check with a test page that drives `WorkerTransport` directly against the real worker (init, markers, match on `pinball-demo.jpg`): a pose comes back; the buffer returns.
- [ ] Commit: `feat(nft): run the KPM worker from the browser`.

### Task 4: Wire it into `ARNFTTracker` (needs #115; local WebARKitLib branch until it merges)

- [ ] `nft_tests`, failing first:
  - with `WEBAR_KPM_WORKER` and a fake transport injected through a test hook, `ARNFTTracker` tracks `pinball-demo.jpg`, the hooks deliver page 0 and the 2000×1500 camera, and the pose matches the sync path within the filter's tolerance;
  - with the option off, behaviour is byte-for-byte today's (the existing tests unchanged).
- [ ] `ARNFTTracker`: the config is `singleThreadPreset()` with `detector = External`, `makeDetector` and `makeDetectorUser = this` when the option is on. The public API is unchanged.
- [ ] Commit: `perf(nft): run KPM in a Web Worker`. The submodule bump comes as its own commit, only once #115 is merged.

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

## Not in this plan

- The threaded (pthreads) option: kept as the proven fallback (spike diff outside the repository). It is used only if #115 stalls, after a new decision.
- Removing the core's own unused KPM data in `External` mode (upstream follow-up).
- `-g` on the main app: measured separately, its own decision.
- Moving AR2 tracking or the capture off the main thread.
