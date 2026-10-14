# ARNFTTracker sopra ARToolKitNFTCore (WebARKitLib PR #94) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

*English version: [2026-10-11-pr94-artoolkit-nft-core.en.md](2026-10-11-pr94-artoolkit-nft-core.en.md). Le due versioni sono allineate; per l'esecuzione fa riferimento quella inglese.*

**Goal:** `ARNFTTracker` mantiene l'API che usano `ARBaseEngine` e le app, ma delega camera, marker, detection KPM, tracking AR2 e filtro della posa a `ARToolKitNFTCore` della PR #94 di WebARKitLib, con lo stesso comportamento di tracking che ha `dev` oggi.

**Architecture:** WebARKitLib passa da `4fd3034` (0.10.0) alla testa della PR #94 `6fd0a25` (fast-forward). Un test runner Node headless fissa prima il comportamento attuale di `ARNFTTracker` (test di caratterizzazione), poi la classe viene riscritta come adapter sottile: tiene il buffer RGBA che JS riempie e la conversione in luma (`WebARKitVideoLuma`), e inoltra tutto il resto a un `ARToolKitNFTCore`. Gli stessi test devono restare verdi attraverso la riscrittura, e la CI li esegue.

**Tech Stack:** C++17, Emscripten 4.0.17 (`emscripten/emsdk:4.0.17`), CMake ≥ 3.19, WebARKitLib `WebARKitNFT` @ `6fd0a25`, Node 22 (nell'immagine emsdk), GitHub Actions.

**Spec:** [webarkit/WebARKitLib#94](https://github.com/webarkit/WebARKitLib/pull/94): la sezione del README "Using `ARToolKitNFTCore` natively" e il commento di classe in `WebARKit/WebARKitTrackers/WebARKitNFT/include/WebARKitTrackers/WebARKitNFT/ARToolKitNFTCore.h`. Comportamento da preservare: `src/nft/ARNFTTracker.{h,cpp}` su `dev` @ `24db9a4`. Test runner: passo 2 di [#9](https://github.com/kalwalt/WebAR-Canvas/issues/9).

## Global Constraints

- Branch `feat/nft-core` da `dev` **dopo il merge della PR #10 (CI)**; la PR punta a `dev` (AGENTS.md §5). Build con `node build-docker.js`; commit in Conventional Commits.
- Submodule WebARKitLib: `6fd0a250c132b64d4d30e3e0708b66d85a1eaaac` (branch `feat/nft-core` di `webarkit/WebARKitLib`). La PR #94 non è mergiata upstream: questo lavoro può entrare in `dev`, ma **nessuna release su `master`** finché il core non è in una release di WebARKitLib; a quel punto il submodule passa a quel tag.
- Solo single-thread: `singleThreadPreset()`, `WEBARKIT_NFT_THREADS OFF` (come oggi).
- Comportamento mantenuto da `dev`: filtro **attivo** (`ARToolKitNFTCore(singleThreadPreset(), true)`), intervallo di detection **200.0 ms** (default del core 300), detection continua **attiva**, piani di proiezione near **10.0** / far **5000.0** (default del core 0.0001 / 1000). Impostazioni AR2 e politica di detection sono già identiche. Il filtro (dev 30/15 Hz, core 120/60 Hz) ha lo stesso rapporto campionamento/taglio, quindi lo stesso coefficiente passa-basso: nessuna modifica.
- Contratto del core: `loadCamera()` (statico) → `setup()` (poi verificare `cameraParamLT() != nullptr`) → `setupAR2()` **una volta** → `addNFTMarkers()`; per frame `setVideoFrame(rgba, luma)` + `detectNFTMarker()`.
- `ARToolKitNFTCore::getNFTData(i)` va in abort con un indice fuori range (`std::vector::at`): l'adapter controlla prima `0 <= i < markerCount()`.
- Niente Embind in `ARNFTTracker`; compila con `-fno-rtti` (AGENTS.md).
- `--embed-file` sempre nella forma `"--embed-file=<src>@<dst>"` (AGENTS.md).

## Review Focus

1. **Indice di marker fuori range** (`getMarkerData(5)`, `getMarkerData(-1)`, `isMarkerTracked(5)`, `getTransformMatrix(5, m)`): dati vuoti / `false` / matrice a zero, mai un abort → test `out_of_range_index_is_empty` (Task 1).
2. **Frame prima dei marker o prima del setup** (la webcam parte prima che `init()` finisca, o `init()` è fallita): `update()` restituisce 0 e niente va in crash → test `update_before_setup_is_safe`, `update_without_markers_is_safe` (Task 1).
3. **Asset mancanti** (`camera_para.dat`, file del marker): `-1`, nessun abort, niente caricato a metà → test `loads_camera_param`, `missing_marker_fails` (Task 1).
4. **Il marker esce dall'inquadratura**: `isMarkerTracked(0)` diventa false e la trasformazione va a zero (il cubo sparisce invece di congelarsi) → test `loses_tracking_on_blank_frame` (Task 1).
5. **Stesso frustum e stesso ritmo di `dev`**: near/far 10/5000, intervallo 200 ms, filtro attivo, quindi il cubo mantiene dimensione, clipping e fluidità → test `keeps_dev_tracking_settings` (Task 1).

---

### Task 1: WebARKitLib @ PR #94 + test runner Node che fissa l'ARNFTTracker di oggi

**Files:**
- Modify: `third_party/WebARKitLib` (submodule → `6fd0a25`)
- Modify: `CMakeLists.txt` (togliere `lib/SRC/WebARKit/WebARKitLog.cpp` da `WEBARKIT_EXTRA_SOURCES`; target `nft_tests`)
- Create: `tests/nft_tests.cpp`, `scripts/test-wasm.sh`
- Modify: `build-docker.js` (`--test`), `.github/workflows/build.yml` (step dei test), `AGENTS.md`, `CLAUDE.md`, `README.md`, `CHANGELOG.md`

**Interfaces:**
- Consumes: `ARNFTTracker` come su `dev` (invariato in questo task); `scripts/build-wasm.sh` e il job CI della #10.
- Produces: `option(WEBAR_CANVAS_BUILD_TESTS "Build the Node test runner (nft_tests)" OFF)`; target `nft_tests` (sorgente `tests/nft_tests.cpp`, link a `ARNFTTracker`, link options `-sENVIRONMENT=node -sEXIT_RUNTIME=1 -sASSERTIONS=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sUSE_LIBJPEG=1 -sUSE_ZLIB=1 "--embed-file=${WEBARKITLIB_DIR}/tests/data@/data"`), output `build-wasm/nft_tests.js`, exit code = check falliti, ultima riga `<N> checks, <F> failed`. `scripts/test-wasm.sh [jobs]`: configura `build-wasm` con `-DWEBAR_CANVAS_BUILD_TESTS=ON` (stessi flag di `build-wasm.sh`), compila `nft_tests`, esegue `node build-wasm/nft_tests.js`. `node build-docker.js --test` lo esegue dopo la build dell'app. Fixture in `/data`: `camera_para.dat`, `pinball.{iset,fset,fset3}`, `pinball-demo.jpg` (2000×1500).

- [ ] **Step 1: Spostare il submodule**

```bash
git -C third_party/WebARKitLib fetch origin feat/nft-core
git -C third_party/WebARKitLib checkout 6fd0a250c132b64d4d30e3e0708b66d85a1eaaac
```

- [ ] **Step 2: Scrivere i test di caratterizzazione** in `tests/nft_tests.cpp` (macro `CHECK(cond)` che conta i fallimenti, una tabella `kTests[]`, `main` restituisce il numero di fallimenti). Helper `bool fillFromJpeg(ARNFTTracker &t, const char *path)`: `ar2ReadJpegImage2` → RGB(A) espanso in RGBA in `t.getVideoFrame()`. Helper `bool trackPinball(ARNFTTracker &t)`: `loadCamera("/data/camera_para.dat")`, `setup(2000, 1500, cam)`, `setupAR2()`, `loadMarker("/data/pinball")`, `fillFromJpeg(t, "/data/pinball-demo.jpg")`, poi `update()` fino a 60 volte finché `isMarkerTracked(0)`.

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
  CHECK(m[14] < 0.0f);   // marker davanti alla camera (GL, destrorso)
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

- [ ] **Step 3: Verificare che fallisca** — `node build-docker.js --test`. Atteso: errore perché `--test`, `scripts/test-wasm.sh` o il target `nft_tests` non esistono ancora.

- [ ] **Step 4: Collegare tutto** — opzione e target CMake, `scripts/test-wasm.sh` (LF, eseguibile), `--test` in `build-docker.js` (dopo `build-wasm.sh`: `sh /src/scripts/test-wasm.sh ${jobs}`), uno step `Run the NFT tests` in `.github/workflows/build.yml` dopo `Build WebAR-Canvas` (`run: sh scripts/test-wasm.sh`). Togliere `${WEBARKITLIB_DIR}/lib/SRC/WebARKit/WebARKitLog.cpp` da `WEBARKIT_EXTRA_SOURCES`: a `6fd0a25` `WebARKitNFT` compila già `WebARKit/WebARKitLog.cpp`.

- [ ] **Step 5: Verificare che passi sull'ARNFTTracker di oggi** — `node build-docker.js --test`. Atteso: app compilata, ultima riga `N checks, 0 failed`. Un check che fallisce qui descrive `dev` in modo sbagliato: si corregge il test, non il tracker (ruling nel ledger).

- [ ] **Step 6: Verifica nel browser** — `python -m http.server 8000 --directory build-wasm`, `videoTest.html`, marker pinball. Atteso: come su `dev`: detection, tracking, cubo sul marker.

- [ ] **Step 7: Documentazione e commit** — AGENTS.md §4/§6, CLAUDE.md, README "Building": `node build-docker.js --test`. CHANGELOG `Unreleased`: Added "Node test runner `nft_tests`, run in CI (#9)"; Changed "WebARKitLib → PR #94 head `6fd0a25`".

```bash
git add third_party/WebARKitLib CMakeLists.txt tests/nft_tests.cpp scripts/test-wasm.sh build-docker.js .github/workflows/build.yml AGENTS.md CLAUDE.md README.md CHANGELOG.md
git commit -m "test(nft): pin ARNFTTracker behaviour with a Node test runner on WebARKitLib PR #94"
```

---

### Task 2: ARNFTTracker diventa un adapter sopra ARToolKitNFTCore

**Files:**
- Modify: `src/nft/ARNFTTracker.h`, `src/nft/ARNFTTracker.cpp`
- Modify: `AGENTS.md` (righe di console in §6, se il testo dei log cambia), `CHANGELOG.md`
- Test: `tests/nft_tests.cpp` (invariato: deve restare verde)

**Interfaces:**
- Consumes: i test del Task 1; `ARToolKitNFTCore`, `NFTTrackingConfig` (`singleThreadPreset()`), `NFTMarkerState`, `nftMarker`.
- Produces — API pubblica mantenuta, stesse firme: `loadCamera`, `setup`, `setCamera`, `setupAR2`, `teardown`, `loadMarker`, `loadMarkers`, `getVideoFrame` (const e non-const), `getVideoFrameSize`, `getVideoLuma`, `update`, `isMarkerTracked`, `anyMarkerTracked`, `allMarkersTracked`, `getTrackingError`, `getTransformMatrix`, `getPose`, `set/getContinuousDetection`, `set/getDetectionInterval`, `set/getFiltering`, `getMarkerCount`, `getMarkerData`, `getMarkerState(int) const`, `getCameraLens`, `setProjectionPlanes`, `getNearPlane`, `getFarPlane`, `recalculateCameraLens`, `getWidth`, `getHeight`, `getCameraParam`, `getCameraParamLT`, `set/getLogLevel`, `NFTMarkerData`, `NFT_PAGES_MAX`. **Rimossi** (interni al core, o senza equivalente nel core): `updateVideoLuma`, `detectNFTMarker`, `trackMarkers`, `getSurfaceSet`, `setFilterParameters`, `getMarkerState` non-const.

- [ ] **Step 1: Riscrivere `src/nft/ARNFTTracker.{h,cpp}`** — membri privati: `ARToolKitNFTCore core{singleThreadPreset(), true};`, `int coreId = -1;`, il buffer RGBA `std::unique_ptr<ARUint8[]> videoFrame` + `videoFrameSize`, `webarkit::WebARKitLumaInfo *lumaInfo`, `width`/`height`, e copie delle tre impostazioni per i getter (`continuousDetection = true`, `detectionIntervalMs = 200.0`, `withFiltering = true`) passate al core nel costruttore; il costruttore imposta anche near/far 10.0/5000.0 sul core. Corrispondenze:
  - `loadCamera` → `ARToolKitNFTCore::loadCamera`; `setup` → `core.setup()`, poi `-1` se `core.cameraParamLT() == nullptr`, altrimenti alloca il buffer RGBA e `lumaInfo` e restituisce 0; `setCamera(cam)` → `core.setCamera(coreId, cam)`; `setupAR2` → `core.setupAR2()`; `loadMarkers` → `core.addNFTMarkers()`; `loadMarker` → primo id o `-1`.
  - `update()`: `0` se non c'è il buffer RGBA; altrimenti luma con `webarkitVideoLuma(lumaInfo, frame)`, `core.setVideoFrame(frame, luma)`, `core.detectNFTMarker()`, restituisce `anyMarkerTracked() ? 1 : 0`.
  - Le query di tracking leggono `core.markerState(i)` (null → non tracciato, errore `-1.0f`, posa a zero); `getTransformMatrix` = `arglCameraViewRH(state->pose, gl, 1.0)` → `float`, zeri se non tracciato; `getMarkerData(i)` controlla `0 <= i < core.markerCount()` prima di `core.getNFTData(i)` e mappa `id_NFT/width_NFT/height_NFT/dpi_NFT`, altrimenti restituisce `NFTMarkerData{}`.
  - Camera: `getCameraLens` → `core.cameraLens()`; `setProjectionPlanes` → setter del core + `recalculateCameraLens()`; getter near/far → getter del core; `getCameraParam` → `&core.cameraParam()`; `getCameraParamLT` → `core.cameraParamLT()`.
  - `teardown()` → `core.teardown()`, poi libera il buffer RGBA e `lumaInfo`.
  - `ARNFTTracker.h` include `<WebARKitTrackers/WebARKitNFT/ARToolKitNFTCore.h>`, `<AR/paramGL.h>`, `<WebARKitVideoLuma.h>`; `trackingMod.h` e `markerDecompress.h` non servono più.

- [ ] **Step 2: Verificare che i test restino verdi** — `node build-docker.js --test`. Atteso: `N checks, 0 failed` con lo stesso N del Task 1.

- [ ] **Step 3: Verifica nel browser** — come Task 1 Step 6, più la console: camera, marker `893x1117 @ 120 DPI`, il frustum con gli stessi valori di `dev` (`-1.0040 -20.0401` nella terza riga), e nessun errore. Se il testo dei log è cambiato, aggiornare l'elenco in AGENTS.md §6.

- [ ] **Step 4: CHANGELOG e commit** — `Unreleased`: Changed "`ARNFTTracker` delegates camera, markers, KPM detection, AR2 tracking and filtering to `ARToolKitNFTCore` (WebARKitLib PR #94)"; Removed: i sei metodi elencati in Interfaces.

```bash
git add src/nft/ARNFTTracker.h src/nft/ARNFTTracker.cpp AGENTS.md CHANGELOG.md
git commit -m "refactor(nft): make ARNFTTracker an adapter over ARToolKitNFTCore"
```

---

## Fuori scopo

- Il preset threaded (`WEBARKIT_NFT_THREADS=ON`: `-pthread`, header COOP/COEP).
- La copia RGBA in più per frame (buffer dell'adapter → buffer del core, ~1,2 MB a 640×480): misurarla nell'ambito della #4 prima di ottimizzare.
- La build Debug con `-fno-rtti` globale (`ASSERT(dynamic_cast)` in KPM quando `NDEBUG` non è definito): è già così su `dev`.
- Spostare il submodule su un tag di release di WebARKitLib quando la PR #94 sarà rilasciata.
