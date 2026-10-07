# Piano: issue #3, usare gli helper NFT direttamente da WebARKitLib

Issue: https://github.com/kalwalt/WebAR-Canvas/issues/3
Upstream: webarkit/WebARKitLib PR #76 (branch `feat/issue-75-webarkit-nft`, head `480fdb9`), non ancora mergiata.

## Stato attuale (branch `dev`)

- `third_party/WebARKitLib` è fissato al commit `fe4b069`.
- `src/nft/` contiene copie locali: `trackingMod.c/.h`, `trackingMod2d.c`, `markerDecompress.c/.h`, `NFTMarkerState.h`.
- `CMakeLists.txt` crea **a mano** un target `WebARKitNFT` con glob di AR, ARICP, ARUtil, AR2, KPM, più `NFT_CUSTOM_SOURCES` (le copie locali + `ARNFTTracker.cpp`) e `WEBARKIT_EXTRA_SOURCES` (`WebARKitLog.cpp`, `WebARKitVideoLuma.cpp`).

## Cosa porta la PR #76

- Nuovo target `WebARKitNFT` in `WebARKit/WebARKitTrackers/WebARKitNFT/CMakeLists.txt`: compila già AR, ARICP, AR2, KPM, ARUtil (+ minizip e `thread_sub.c`) e gli helper NFT, **senza OpenCV**. Su Emscripten aggiunge `-sUSE_LIBJPEG=1 -sUSE_ZLIB=1` e definisce `USE_FILE32API` per minizip.
- Header pubblici in `<WebARKitTrackers/WebARKitNFT/...>`. Il path suggerito nell'issue (`<AR2/trackingMod.h>`) **non** è quello reale.
- Differenze rispetto alle copie locali:
  - `ar2Tracking2dSub` → `ar2Tracking2dSubMod`
  - `markerDecompress` restituisce `-1` invece di chiamare `exit()`
  - `ar2CreateHandleSubMod` inizializza `icpHandle`/`cparamLT`
- Opzione `WEBARKIT_NFT_THREADS` (OFF): aggiunge `trackingSub.c` e `-pthread`.

## Il conflitto di nomi

Il nostro target locale si chiama già `WebARKitNFT`, quindi con `add_subdirectory` upstream la build si rompe. Il target locale va rinominato (es. `ARNFTTracker`) oppure sciolto dentro l'eseguibile.

## Passi

### 0. Branch
`git checkout dev && git pull && git checkout -b feat/issue-3-webarkitnft` (le PR vanno su `dev`).

### 1. Puntare il submodule al branch della PR (temporaneo)
```bash
cd third_party/WebARKitLib
git fetch origin pull/76/head:pr-76
git checkout pr-76   # 480fdb9
cd ../..
git add third_party/WebARKitLib
```
Non serve cambiare l'URL in `.gitmodules`: il commit è raggiungibile solo tramite il ref della PR, quindi **prima del merge di questa PR** va riallineato al commit mergiato su `dev`/`master` di WebARKitLib (passo 7). Fino ad allora un `git submodule update` su un clone pulito potrebbe non trovare il commit.

### 2. CMake: usare il target upstream
In `CMakeLists.txt`:
- Eliminare i glob/liste manuali di AR, ARICP, ARUtil, AR2, KPM e `NFT_CUSTOM_SOURCES`.
- Aggiungere:
  ```cmake
  set(WEBARKIT_NFT_THREADS OFF CACHE BOOL "" FORCE)
  add_subdirectory(${WEBARKITLIB_DIR}/WebARKit/WebARKitTrackers/WebARKitNFT
                   ${CMAKE_CURRENT_BINARY_DIR}/webarkitnft_build)
  ```
  Si usa direttamente la sottocartella NFT invece di `WebARKit/` per evitare `include(OpenCVEm.cmake)` e il fetch di OpenCV; il CMakeLists NFT ricava i path da solo (`../../..`), quindi funziona.
- Rinominare il target locale in `ARNFTTracker` (static) con `src/nft/ARNFTTracker.cpp` + `WEBARKIT_EXTRA_SOURCES` (`WebARKitLog.cpp`, `WebARKitVideoLuma.cpp`, non inclusi upstream), con `target_link_libraries(ARNFTTracker PUBLIC WebARKitNFT)`.
- Tenere le include di `WebARKit/include` (per `WebARKitLog.h`, `WebARKitVideoLuma.h`); togliere `src/nft` dalle include di `WebARKitNFT` e `lib/SRC/KPM/FreakMatcher` se non più necessaria (upstream la mette PRIVATE).
- Eseguibile: link a `ARNFTTracker` al posto di `WebARKitNFT`; aggiornare `add_dependencies`.
- `HAVE_NFT`: upstream lo definisce come PUBLIC (valore 1 implicito); togliere il duplicato `-DHAVE_NFT=1` da `add_definitions` oppure lasciarlo se identico (evitare warning di ridefinizione).
- Verificare che `-sUSE_LIBJPEG=1 -sUSE_ZLIB=1` non siano già aggiunti a mano in modo diverso.

### 3. Include
- `src/nft/ARNFTTracker.h`:
  ```cpp
  #include <WebARKitTrackers/WebARKitNFT/trackingMod.h>
  #include <WebARKitTrackers/WebARKitNFT/markerDecompress.h>
  #include <WebARKitTrackers/WebARKitNFT/NFTMarkerState.h>
  ```
- `src/nft/ARNFTTracker.cpp`: `grep` per `ar2Tracking2dSub` (→ `ar2Tracking2dSubMod`) e per altri simboli rinominati.
- Confrontare `NFTMarkerState.h` locale con quello upstream: se il nostro ha campi aggiunti per WebAR-Canvas, o li si propone upstream o si tiene una struct locale che li estende.

### 4. Gestione errori di `markerDecompress`
Ora restituisce `-1` invece di terminare. Controllare in `ARNFTTracker.cpp` che il valore di ritorno venga verificato e che il caricamento del marker fallisca in modo pulito (log + `return false`).

### 5. Rimuovere i file vendored
```bash
git rm src/nft/trackingMod.c src/nft/trackingMod.h src/nft/trackingMod2d.c \
       src/nft/markerDecompress.c src/nft/markerDecompress.h src/nft/NFTMarkerState.h
```
Aggiornare `AGENTS.md` e `README.md` dove citano `src/nft/` o il layout del target.

### 6. Verifica
1. `node build-docker.js --clean` poi `node build-docker.js -j 8`: nessun simbolo duplicato o mancante.
2. `python -m http.server 8000 --directory build-wasm` e aprire `http://localhost:8000/videoTest.html`:
   - il video viene mostrato
   - il marker `pinball` (`.iset/.fset/.fset3`) si carica
   - KPM trova il marker e il tracking AR2 lo segue (posa/cubo stabile)
   - nessun errore in console
3. Se possibile un test con un `.zft` per il percorso `markerDecompress`.
4. Confronto rapido di FPS e stabilità con la build di `dev` attuale (non deve peggiorare).

### 7. Dopo il merge di WebARKitLib #76
- Spostare il submodule sul commit mergiato (`master` o il tag di release).
- Ricompilare e ripetere il passo 6.
- Aprire la PR verso `dev` con `Closes #3`. Finché upstream non è mergiato, la PR resta in **draft**.

## Rischi e domande aperte
- **Commit del submodule non su un branch stabile**: per questo la PR resta in draft fino al merge upstream.
- **Flag di compilazione**: il target upstream non eredita `-fno-rtti` e simili se sono impostati solo sul vecchio target locale. Con `add_compile_options` globale (come ora sotto `if(EMSCRIPTEN)`) vengono applicati comunque, perché l'`add_subdirectory` sta dopo; va verificato.
- **Standard C++**: in quella sottocartella il progetto usa C++17 (il `set(CMAKE_CXX_STANDARD 14)` upstream sta in `WebARKit/CMakeLists.txt`, che non includiamo). Va bene.
- **Threads**: `WEBARKIT_NFT_THREADS` resta OFF. Attivarlo richiede COOP/COEP sul server e `-pthread` su tutto il link: è fuori scope, al massimo una issue separata.
