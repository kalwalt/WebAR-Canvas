// Node test runner for ARNFTTracker (target nft_tests, built with -DWEBAR_CANVAS_BUILD_TESTS=ON).
// Run it with scripts/test-wasm.sh or `node build-docker.js --test`.
// WebARKitLib's tests/data is embedded at /data. The exit code is the number of failed checks.

#include "ARNFTTracker.h"

#include <AR2/imageFormat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    ++gChecks;                                                                 \
    if (!(cond)) {                                                             \
      ++gFailures;                                                             \
      std::printf("  check failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                          \
  } while (0)

/** Decodes a JPEG of the tracker's frame size into its RGBA frame buffer. */
static bool fillFromJpeg(ARNFTTracker &t, const char *path) {
  FILE *fp = std::fopen(path, "rb");
  if (!fp) return false;
  AR2JpegImageT *jpeg = ar2ReadJpegImage2(fp);
  std::fclose(fp);
  if (!jpeg) return false;
  ARUint8 *rgba = t.getVideoFrame();
  const bool fits = rgba && jpeg->xsize == t.getWidth() && jpeg->ysize == t.getHeight();
  if (fits) {
    const int pixels = jpeg->xsize * jpeg->ysize;
    for (int i = 0; i < pixels; i++) {
      const ARUint8 *src = jpeg->image + i * jpeg->nc;
      ARUint8 *dst = rgba + i * 4;
      dst[0] = src[0];
      dst[1] = jpeg->nc >= 3 ? src[1] : src[0];
      dst[2] = jpeg->nc >= 3 ? src[2] : src[0];
      dst[3] = 255;
    }
  }
  ar2FreeJpegImage(&jpeg);
  return fits;
}

/** Sets up a 2000x1500 tracker with pinball and feeds pinball-demo.jpg until it is tracked. */
static bool trackPinball(ARNFTTracker &t) {
  const int cam = t.loadCamera("/data/camera_para.dat");
  if (cam < 0 || t.setup(2000, 1500, cam) != 0 || t.setupAR2() != 0) return false;
  if (t.loadMarker("/data/pinball") != 0) return false;
  if (!fillFromJpeg(t, "/data/pinball-demo.jpg")) return false;
  for (int i = 0; i < 60 && !t.isMarkerTracked(0); i++) {
    t.update();
  }
  return t.isMarkerTracked(0);
}

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
  float m[16];
  std::fill(m, m + 16, 1.0f);
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
  CHECK(m[14] < 0.0f); // marker in front of the camera (GL, right-handed)
  CHECK(m[15] == 1.0f);
}

static void loses_tracking_on_blank_frame() {
  ARNFTTracker t;
  CHECK(trackPinball(t));
  std::memset(t.getVideoFrame(), 0, t.getVideoFrameSize());
  t.update();
  CHECK(!t.isMarkerTracked(0));
}

struct TestCase {
  const char *name;
  void (*run)();
};

static const TestCase kTests[] = {
    {"loads_camera_param", loads_camera_param},
    {"missing_marker_fails", missing_marker_fails},
    {"update_before_setup_is_safe", update_before_setup_is_safe},
    {"update_without_markers_is_safe", update_without_markers_is_safe},
    {"keeps_dev_tracking_settings", keeps_dev_tracking_settings},
    {"out_of_range_index_is_empty", out_of_range_index_is_empty},
    {"tracks_pinball_demo", tracks_pinball_demo},
    {"loses_tracking_on_blank_frame", loses_tracking_on_blank_frame},
};

int main() {
  for (const TestCase &test : kTests) {
    const int failuresBefore = gFailures;
    test.run();
    std::printf("%s %s\n", gFailures == failuresBefore ? "ok  " : "FAIL", test.name);
  }
  std::printf("%d checks, %d failed\n", gChecks, gFailures);
  return gFailures;
}
