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
   The app is written to `build-wasm/` (`videoTest.html`, `videoTest.js`, `videoTest.wasm`); serve that folder over HTTP to try it, as described in the README, "Running & Testing".

## Write your app

The app is a class derived from `ARBaseApp` (`src/core/ARBaseApp.h`). The demo, a cube on the pinball marker, is in `src/app/App.h` and `src/app/App.cpp`: edit or replace it with your own.

- Override the hooks you need: `setup()` (once, when the renderer and the tracker are ready), `update(float deltaTime)` and `draw()` (every frame), `onMarkerFound(int)` and `onMarkerLost(int)`.
- Marker 0's pose is `getModelViewMatrix()` while `getIsTracking()` is true. In the Filament build, `getEngine()`, `getScene()` and `getCamera()` give the Filament objects to create your entities with.
- `AR_APP_MAIN(App, 640, 480, "/assets/markers/pinball")` defines `main()`: it fills an `ARAppConfig` (canvas selector, camera frame size, camera parameter file, marker paths) and calls `runApp()`. You can fill `ARAppConfig` in code instead, and the page can override it at runtime with `Module.webarConfig`.
- Files the app reads at runtime (markers, materials, camera parameters) are embedded in the `.wasm` by the `--embed-file` options in `CMakeLists.txt`; add yours there.

The README section "Developing Your Own AR Application" has a complete example. Anchors, input, primitives and glTF models are being added to `ARBaseApp` in the current stage; this page will describe them once they are in.

## Publish on GitHub Pages

The workflow `.github/workflows/pages.yml` builds the app in the same container as the CI build (`emscripten/emsdk:4.0.17`), publishes `videoTest.html`, `videoTest.js` and `videoTest.wasm` (with `videoTest.html` also as `index.html`, so the site root opens the app), and deploys them to GitHub Pages.

**Repository settings.** The repository owner changes two settings once:

- **Settings → Pages → Build and deployment → Source: GitHub Actions.** Without it the deploy job fails.
- **Settings → General → Template repository** (tick the checkbox). This is needed only in the original WebAR-Canvas repository, to show the **Use this template** button; a project made from it does not need it.

**HTTPS.** GitHub Pages serves the site over HTTPS, so browsers allow camera access (`getUserMedia`) and the app works on phones too, not only on `localhost`.

**Which branch deploys.** The workflow starts on every push, but its jobs run only on the repository's **default branch**: `master` in WebAR-Canvas (where releases go), `main` or `master` in a project made from the template. On other branches the jobs are skipped. It can also be started by hand from the **Actions** tab (`workflow_dispatch`); it deploys only when started on the default branch. To deploy from another branch, replace the `if:` of both jobs (`build` and `deploy`) in `pages.yml` with a fixed branch, for example:

```yaml
if: github.ref == 'refs/heads/release'
```

The host-tools cache step in `pages.yml` is the same as in `.github/workflows/build.yml`: if you change one, change the other.
