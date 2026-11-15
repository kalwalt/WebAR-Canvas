#include "core/ARBaseEngine.h"
#include "WebARCanvasVersion.h"
#include "core/FilamentRenderer.h"
#include "nft/ARNFTTracker.h"
#include <emscripten.h>

#include <algorithm>
#include <cstdio>

// Helper JavaScript to capture CPU frame from <video> element into WASM memory
EM_JS(int, capture_video_frame, (uint8_t *dest, int width, int height), {
  const video = document.getElementById('ar-video');
  if (!video || video.readyState < video.HAVE_CURRENT_DATA) {
    return 0;
  }

  if (!Module._captureCanvas) {
    Module._captureCanvas = document.createElement('canvas');
    Module._captureCanvas.width = width;
    Module._captureCanvas.height = height;
    Module._captureCtx = Module._captureCanvas.getContext('2d', { willReadFrequently: true });
  }

  var ctx = Module._captureCtx;
  var profile = Module.webarProfile && Module.webarProfile.enabled;
  var t0 = profile ? performance.now() : 0;
  ctx.drawImage(video, 0, 0, width, height);
  var t1 = profile ? performance.now() : 0;
  var imgData = ctx.getImageData(0, 0, width, height);
  var t2 = profile ? performance.now() : 0;
  HEAPU8.set(imgData.data, dest);
  if (profile) {
    Module.webarProfile.capture = [t1 - t0, t2 - t1, performance.now() - t2];
  }
  return 1;
});

// Returns 1 when the <video> has presented a frame since the last call, so the texture
// upload, the capture and the tracking run once per camera frame instead of once per display
// frame (a 30 fps camera on a 60-144 Hz display). Without requestVideoFrameCallback it always
// returns 1, as before. If the video plays but no callback has arrived for 250 ms (an engine
// that does not fire it for this hidden 1 px element), it also returns 1 on every tick until
// callbacks arrive again, so the texture and the tracking never freeze.
EM_JS(int, video_frame_due, (), {
  var video = document.getElementById('ar-video');
  if (!video || !('requestVideoFrameCallback' in video)) {
    return 1;
  }
  var watch = Module._videoFrameWatch;
  if (!watch || watch.video !== video) {
    watch = Module._videoFrameWatch = { video: video, pending: true, last: performance.now() };
    var onFrame = function() {
      if (Module._videoFrameWatch !== watch) {
        return; // the element was replaced: stop watching the old one
      }
      watch.pending = true;
      watch.last = performance.now();
      video.requestVideoFrameCallback(onFrame);
    };
    video.requestVideoFrameCallback(onFrame);
  }
  if (watch.pending) {
    watch.pending = false;
    return 1;
  }
  var playing = !video.paused && video.readyState >= video.HAVE_CURRENT_DATA;
  return (playing && performance.now() - watch.last > 250) ? 1 : 0;
});

// Opt-in frame timing probe (#4): with Module.webarProfile = { enabled: true }, every tick
// appends one row (milliseconds, wall time of the main thread) to Module.webarProfile.samples.
EM_JS(int, webar_profile_enabled, (), {
  return (Module.webarProfile && Module.webarProfile.enabled) ? 1 : 0;
});

EM_JS(void, webar_profile_record,
      (double upload, double capture, double luma, double track, double appUpdate,
       double render, double tick, int captured, int tracked), {
  var p = Module.webarProfile;
  if (!p.samples) p.samples = [];
  if (!p.columns || !p.columns.length) {
    p.columns = ['upload', 'capture', 'draw', 'getImageData', 'heapSet', 'luma', 'track',
                 'appUpdate', 'render', 'tick', 'captured', 'tracked'];
  }
  var c = (captured && p.capture) || [0, 0, 0];
  p.samples.push([upload, capture, c[0], c[1], c[2], luma, track, appUpdate, render, tick,
                  captured, tracked]);
  p.capture = null;
});

ARBaseEngine::ARBaseEngine() = default;

ARBaseEngine::~ARBaseEngine() {
  if (nftTracker) {
    nftTracker->teardown();
  }
  // renderer is released after this body: the app's own entities are gone by then.
}

IARRenderer &ARBaseEngine::getRenderer() const { return *renderer; }

bool ARBaseEngine::init(const char *canvasId, int width, int height,
                        const std::string &markerPath) {
  printf("WebAR-Canvas v%s\n", WEBAR_CANVAS_VERSION_STRING);

  // --- STEP 1: The rendering backend (WebGL 2 context, scene, video background) ---
  renderer = std::make_unique<FilamentRenderer>();
  if (!renderer->init("#canvas", width, height)) {
    return false;
  }

  // --- STEP 2: Initialize NFT Subsystem (Setup & setCamera) ---
  printf("Initializing ARNFTTracker...\n");
  nftTracker = std::make_unique<ARNFTTracker>();
  int cameraID = nftTracker->loadCamera("/assets/camera_para.dat");
  if (cameraID < 0) {
    printf("ERROR: Failed to load camera parameter file /assets/camera_para.dat\n");
    return false;
  }

  if (nftTracker->setup(width, height, cameraID) < 0) {
    printf("ERROR: Failed to setup ARNFTTracker with cameraID %d\n", cameraID);
    return false;
  }

  if (nftTracker->setupAR2() < 0) {
    printf("ERROR: Failed to setup AR2 / KPM in ARNFTTracker\n");
    return false;
  }

  // --- STEP 3: Load NFT Marker Dataset ---
  if (!markerPath.empty()) {
    printf("Loading NFT marker from: %s\n", markerPath.c_str());
    int markerId = nftTracker->loadMarker(markerPath);
    if (markerId < 0) {
      printf("ERROR: Failed to load NFT marker from: %s\n", markerPath.c_str());
      return false;
    }
    const auto &markerData = nftTracker->getMarkerData(markerId);
    printf("NFT Marker [%d] loaded successfully: %dx%d @ %d DPI\n",
           markerId, markerData.width, markerData.height, markerData.dpi);
  }

  // Retrieve calculated OpenGL camera frustum projection matrix
  const ARdouble *lens = nftTracker->getCameraLens();
  for (int i = 0; i < 16; ++i) {
    projectionMatrix[i] = static_cast<float>(lens[i]);
  }

  printf("NFT Camera Frustum Projection Matrix (Column-Major):\n");
  for (int r = 0; r < 4; ++r) {
    printf("  [%9.4f %9.4f %9.4f %9.4f]\n",
           projectionMatrix[0 * 4 + r],
           projectionMatrix[1 * 4 + r],
           projectionMatrix[2 * 4 + r],
           projectionMatrix[3 * 4 + r]);
  }
  renderer->setProjection(lens, nftTracker->getNearPlane(), nftTracker->getFarPlane());

  // 4. Developer hook
  this->setup(renderer->getEngine(), renderer->getScene());

  return true;
}

void ARBaseEngine::updateTracking() {
  if (!nftTracker) return;

  uint8_t *frameBuf = nftTracker->getVideoFrame();
  int w = nftTracker->getWidth();
  int h = nftTracker->getHeight();

  frameCaptured = frameBuf && capture_video_frame(frameBuf, w, h);
  if (frameCaptured) {
    nftTracker->update();

    if (nftTracker->getMarkerCount() > 0 && nftTracker->isMarkerTracked(0)) {
      if (!isTracking) {
        printf(">>> NFT Marker 0 RECOGNIZED & TRACKING! <<<\n");
      }
      isTracking = true;
      nftTracker->getTransformMatrix(0, modelViewMatrix);
    } else {
      if (isTracking) {
        printf(">>> NFT Marker 0 LOST! <<<\n");
      }
      isTracking = false;
      std::fill(modelViewMatrix, modelViewMatrix + 16, 0.0f);
    }
  }
}

void ARBaseEngine::tick(void *arg) {
  auto *app = static_cast<ARBaseEngine *>(arg);
  if (webar_profile_enabled()) {
    app->profiledTick();
    return;
  }
  app->updateVideoFrame();   // New camera frame only: texture upload, capture, NFT tracking
  app->update(0.016f);       // Developer hook: apply transforms to 3D entities
  app->getRenderer().render(); // Draw the video background and the scene
}

void ARBaseEngine::updateVideoFrame() {
  if (!video_frame_due()) {
    // Same camera frame as last tick: keep the texture, the pose and the tracking state.
    frameCaptured = false;
    return;
  }
  renderer->updateVideoTexture(); // GPU path: update the video texture
  updateTracking();     // CPU path: capture video frame & run NFT tracking
}

// The same steps as tick() and updateVideoFrame(), timed (milliseconds, wall time of the main
// thread; GPU work runs asynchronously and is not included). Keep it in step with them.
void ARBaseEngine::profiledTick() {
  const double t0 = emscripten_get_now();
  const bool due = video_frame_due();
  if (due) renderer->updateVideoTexture();
  const double t1 = emscripten_get_now();
  if (due) {
    updateTracking();
  } else {
    frameCaptured = false;
  }
  const double t2 = emscripten_get_now();
  update(0.016f);
  const double t3 = emscripten_get_now();
  renderer->render();
  const double t4 = emscripten_get_now();

  double luma = 0.0, track = 0.0;
  if (frameCaptured && nftTracker) {
    const NFTUpdateTimings timings = nftTracker->getLastUpdateTimings();
    luma = timings.lumaMs;
    track = timings.trackMs;
  }
  // capture = updateTracking() - luma - track: the JS capture plus the pose read-out.
  webar_profile_record(t1 - t0, (t2 - t1) - luma - track, luma, track, t3 - t2, t4 - t3,
                       t4 - t0, frameCaptured ? 1 : 0, isTracking ? 1 : 0);
}

void ARBaseEngine::run() {
  emscripten_set_main_loop_arg(ARBaseEngine::tick, this, 0, 1);
}
