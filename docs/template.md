# Using WebAR-Canvas as a template

> **License**
>
> WebAR-Canvas is licensed under the **GNU Lesser General Public License v3.0** (LGPL-3.0) with a special exception permitting static linking — see the [LICENSE](../LICENSE) file for the full terms. A project made from this template contains WebAR-Canvas and its submodules, so their licenses apply to it too.
>
> Third-party licenses (as listed in the [README](../README.md)):
> - **[Google Filament](https://github.com/google/filament)**: Apache 2.0 License.
> - **[WebARKitLib](https://github.com/webarkit/WebARKitLib)** / **[jsartoolkitNFT](https://github.com/webarkit/jsartoolkitNFT)**: LGPL-3.0 / GPL-3.0 License.
> - **[ARToolKit](https://github.com/artoolkitx/artoolkit5)**: GPL-3.0 License.

WebAR-Canvas is a GitHub template repository: you can start your own WebAR app from it, write the app in C++, and publish it on GitHub Pages with the workflow that comes with it.

## Create your project

1. On the WebAR-Canvas page on GitHub, click **Use this template** → **Create a new repository**.
2. Clone your new repository with its submodules (`third_party/filament`, `third_party/WebARKitLib`):
   ```bash
   git clone --recursive https://github.com/<you>/<your-repo>.git
   ```
   **On Windows**, check out Filament with LF line endings, or its host tools fail to build: follow the steps in the README, "Prerequisites & Setup".
3. Build it (Docker and Node.js are required, see the README):
   ```bash
   node build-docker.js
   ```
   The app is written to `build-wasm/` (`videoTest.html`, `videoTest.js`, `videoTest.wasm`, and the KPM worker `videoTest.kpm.js` / `videoTest.kpm.wasm`); serve that folder over HTTP to try it, as described in the README, "Running & Testing".

## Write your app

The app is a class derived from `ARBaseApp` (`src/core/ARBaseApp.h`). The demo, a cube on the pinball marker, is in `src/app/App.h` and `src/app/App.cpp`: edit or replace it with your own.

- Override the hooks you need: `setup()` (once, when the renderer and the tracker are ready), `update(float deltaTime)` and `draw()` (every frame), `onMarkerFound(int)` and `onMarkerLost(int)`.
- Marker 0's pose is `getModelViewMatrix()` while `getIsTracking()` is true; `getMarkerPose(i, m)` gives any marker's.
- **Meshes, on every renderer:** `addBox`, `addCube`, `addPlane` and `addSphere` make unlit, vertex-coloured primitives (millimetres) and return an id (or -1). `attachMeshToMarker(id, i)` keeps a mesh on marker `i`, centred, hidden while the marker is lost; `setMeshLocalTransform(id, m)` moves it relative to the marker; `setMeshVisible` and `removeMesh`. This is the API the demo uses, so your app builds with both renderers. It has no materials, textures or lighting.
- **Filament only** (the default renderer): `getEngine()`, `getScene()` and `getCamera()` give the Filament objects; `createBox` / `createCube` / `createPlane` / `createSphere` return Filament entities, and `attachToMarker(entity, i)` binds an entity to a marker. `loadModel(path)` loads a glTF / GLB model and `placeModelOnMarker(handle, i, sizeMm)` stands it on a marker; put your models under `/assets/models/` with `--embed-file` in `CMakeLists.txt`, or let the page write them at runtime with `Module.FS_createDataFile` and load them with `Module.ccall('webar_load_model', 'number', ['string'], [path])` and `webar_place_model(handle, marker, sizeMm)`.
- **The renderer** is chosen at build time: `node build-docker.js` (Filament) or `node build-docker.js --renderer sokol` (sokol_gfx: a lighter build that needs neither the Filament submodule nor its host tools; output in `build-wasm-sokol/`). With sokol, the glTF calls report failure:

  | Call (sokol build) | Returns |
  |---|---|
  | `loadModel(path)` | -3 (`AR_MODEL_UNSUPPORTED`) |
  | `webar_load_model(path)` (page) | -3 (`AR_MODEL_UNSUPPORTED`) |
  | `webar_place_model(handle, marker, sizeMm)` (page) | 0 (failure) |

  The Pages workflow below publishes the Filament build.
- Input: override `touchDown`, `touchMoved`, `touchUp`, `mouseDown`, `mouseMoved`, `mouseUp` (canvas pixels, with a pointer id) and `windowResized`.
- `AR_APP_MAIN(App, 640, 480, "/assets/markers/pinball")` defines `main()`: it fills an `ARAppConfig` (canvas selector, camera frame size, camera parameter file, marker paths) and calls `runApp()`. You can fill `ARAppConfig` in code instead, and the page can override it at runtime with `Module.webarConfig`.
- Files the app reads at runtime (markers, materials, camera parameters) are embedded in the `.wasm` by the `--embed-file` options in `CMakeLists.txt`; add yours there.

The README section "Developing Your Own AR Application" has a complete example, and `src/app/App.cpp` uses each of the features above.

## Publish on GitHub Pages

The workflow `.github/workflows/pages.yml` builds the app in the same container as the CI build (`emscripten/emsdk:4.0.17`), publishes `videoTest.html`, `videoTest.js`, `videoTest.wasm` and the KPM worker's `videoTest.kpm.js` and `videoTest.kpm.wasm` (with `videoTest.html` also as `index.html`, so the site root opens the app), and deploys them to GitHub Pages. Hosting the app elsewhere, copy the worker's two files next to `videoTest.js` too: without them the app detects markers on the main thread (it logs `KPM worker unavailable` once). Publish the default build, not one made with `node build-docker.js --debug`: that one carries debug information and is several times larger.

**Repository settings.** The repository owner changes two settings once:

- **Settings → Pages → Build and deployment → Source: GitHub Actions.** Without it the deploy job fails.
- **Settings → General → Template repository** (tick the checkbox). This is needed only in the original WebAR-Canvas repository, to show the **Use this template** button; a project made from it does not need it.

**HTTPS.** GitHub Pages serves the site over HTTPS, so browsers allow camera access (`getUserMedia`) and the app works on phones too, not only on `localhost`.

**Which branch deploys.** The workflow starts on every push, but its jobs run only on the repository's **default branch**: `master` in WebAR-Canvas (where releases go), `main` or `master` in a project made from the template. On other branches the jobs are skipped. It can also be started by hand from the **Actions** tab (`workflow_dispatch`); it deploys only when started on the default branch. To deploy from another branch, replace the `if:` of both jobs (`build` and `deploy`) in `pages.yml` with a fixed branch, for example:

```yaml
if: github.ref == 'refs/heads/release'
```

The host-tools cache step in `pages.yml` is the same as in `.github/workflows/build.yml`: if you change one, change the other.
