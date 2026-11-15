// Node test runner for the pure C++ parts of the framework core (target core_tests, built with
// -DWEBAR_CANVAS_BUILD_TESTS=ON). Run it with scripts/test-wasm.sh or `node build-docker.js --test`.
// Covers the pure headers of src/core/ (AREvents, ARAppConfig, ARAnchor, ARInput): no Filament,
// no tracker, no browser. The exit code is the number of failed checks.

#include "core/ARAnchor.h"
#include "core/ARAppConfig.h"
#include "core/AREvents.h"
#include "core/ARInput.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

/** Runs one MarkerEventTracker::update() and records the events it reports. */
struct Events {
  std::vector<int> found;
  std::vector<int> lost;
};

static Events step(MarkerEventTracker &tracker, const std::vector<bool> &tracked) {
  Events e;
  tracker.update(tracked, [&](int i) { e.found.push_back(i); },
                 [&](int i) { e.lost.push_back(i); });
  return e;
}

static void no_events_while_never_tracked() {
  MarkerEventTracker t;
  for (int i = 0; i < 3; i++) {
    const Events e = step(t, {false});
    CHECK(e.found.empty() && e.lost.empty());
  }
}

static void found_once_then_lost_once() {
  MarkerEventTracker t;
  CHECK(step(t, {false}).found.empty());
  const Events found = step(t, {true});
  CHECK(found.found == std::vector<int>{0} && found.lost.empty());
  const Events still = step(t, {true});
  CHECK(still.found.empty() && still.lost.empty());
  const Events lost = step(t, {false});
  CHECK(lost.lost == std::vector<int>{0} && lost.found.empty());
  const Events after = step(t, {false});
  CHECK(after.found.empty() && after.lost.empty());
}

static void two_markers_same_frame() {
  MarkerEventTracker t;
  step(t, {false, false});
  const Events found = step(t, {true, true});
  CHECK((found.found == std::vector<int>{0, 1}) && found.lost.empty());
  const Events lost = step(t, {false, false});
  CHECK((lost.lost == std::vector<int>{0, 1}) && lost.found.empty());
}

static void reset_forgets_state() {
  MarkerEventTracker t;
  CHECK(step(t, {true}).found == std::vector<int>{0});
  t.reset();
  CHECK(step(t, {true}).found == std::vector<int>{0});
}

static void config_defaults() {
  const ARAppConfig c;
  CHECK(c.canvasSelector == "#canvas");
  CHECK(c.videoWidth == 640 && c.videoHeight == 480);
  CHECK(c.cameraParamPath == "/assets/camera_para.dat");
  CHECK(c.markerPaths.empty());
}

static void marker_list_splits_lines() {
  CHECK((parseMarkerList("/a\n/b\n") == std::vector<std::string>{"/a", "/b"}));
  CHECK((parseMarkerList("/a") == std::vector<std::string>{"/a"}));
}

static void marker_list_drops_empty_lines() {
  CHECK((parseMarkerList("\n\n/a\n") == std::vector<std::string>{"/a"}));
  CHECK(parseMarkerList("").empty());
}

static void delta_is_zero_on_first_tick() { CHECK(computeDeltaSeconds(-1.0, 1000.0) == 0.0); }

static void delta_in_seconds() {
  CHECK(std::fabs(computeDeltaSeconds(1000.0, 1016.0) - 0.016) < 1e-9);
}

static void delta_is_clamped() { CHECK(computeDeltaSeconds(1000.0, 6000.0) == 0.1); }

static void delta_never_negative() { CHECK(computeDeltaSeconds(1000.0, 999.0) == 0.0); }

// --- ARAnchor (column-major 4x4: translation in [12], [13], [14]) ---

static void setIdentity(float m[16]) {
  for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

static void marker_center_pinball() {
  float x = 0.0f, y = 0.0f;
  markerCenterMm(893, 1117, 120, x, y);
  CHECK(near(x, 94.51f, 0.01f) && near(y, 118.22f, 0.01f));
}

static void anchor_identity_pose_centres() {
  ARAnchor anchor;
  float pose[16], out[16];
  setIdentity(pose);
  anchor.update(true, pose, 94.51f, 118.22f, out);
  CHECK(anchor.visible());
  CHECK(near(out[12], 94.51f) && near(out[13], 118.22f) && near(out[14], 0.0f));
  CHECK(near(out[0], 1.0f) && near(out[5], 1.0f) && near(out[10], 1.0f) && near(out[15], 1.0f));
  CHECK(near(out[1], 0.0f) && near(out[4], 0.0f) && near(out[8], 0.0f));
}

static void anchor_hidden_when_lost() {
  ARAnchor anchor;
  float pose[16], out[16];
  setIdentity(pose);
  anchor.update(true, pose, 0.0f, 0.0f, out);
  anchor.update(false, pose, 0.0f, 0.0f, out);
  CHECK(!anchor.visible());
}

static void anchor_smoothing_halfway() {
  ARAnchorOptions options;
  options.centerOnMarker = false;
  options.smoothing = 0.5f;
  ARAnchor anchor(options);
  float pose[16], out[16];
  setIdentity(pose);
  anchor.update(true, pose, 50.0f, 50.0f, out);
  pose[12] = 10.0f;
  anchor.update(true, pose, 50.0f, 50.0f, out);
  CHECK(near(out[12], 5.0f) && near(out[13], 0.0f));
  CHECK(near(out[0], 1.0f) && near(out[5], 1.0f) && near(out[10], 1.0f));
}

static void anchor_no_smoothing_across_loss() {
  ARAnchorOptions options;
  options.centerOnMarker = false;
  options.smoothing = 0.5f;
  ARAnchor anchor(options);
  float pose[16], out[16];
  setIdentity(pose);
  anchor.update(true, pose, 0.0f, 0.0f, out);
  anchor.update(false, pose, 0.0f, 0.0f, out);
  pose[12] = 10.0f;
  anchor.update(true, pose, 0.0f, 0.0f, out);
  CHECK(anchor.visible() && near(out[12], 10.0f));
}

// --- clientToCanvas: CSS client coordinates to drawing-buffer pixels (object-fit: contain) ---

static void client_to_canvas_letterbox_wide() {
  // Element 1280x480 showing a 640x480 canvas: the image spans x 320..960.
  float x = -1.0f, y = -1.0f;
  CHECK(clientToCanvas(640.0, 240.0, 0.0, 0.0, 1280.0, 480.0, 640, 480, x, y));
  CHECK(near(x, 320.0f) && near(y, 240.0f));
  x = y = -1.0f;
  CHECK(!clientToCanvas(100.0, 240.0, 0.0, 0.0, 1280.0, 480.0, 640, 480, x, y)); // left bar
  CHECK(x == -1.0f && y == -1.0f);                                               // untouched
}

static void client_to_canvas_letterbox_tall() {
  // Element 640x960 showing a 640x480 canvas: the image spans y 240..720.
  float x = 0.0f, y = 0.0f;
  CHECK(clientToCanvas(320.0, 480.0, 0.0, 0.0, 640.0, 960.0, 640, 480, x, y));
  CHECK(near(x, 320.0f) && near(y, 240.0f));
  CHECK(!clientToCanvas(320.0, 100.0, 0.0, 0.0, 640.0, 960.0, 640, 480, x, y)); // top bar
}

static void client_to_canvas_corners() {
  float x = 0.0f, y = 0.0f;
  CHECK(clientToCanvas(320.0, 0.0, 0.0, 0.0, 1280.0, 480.0, 640, 480, x, y));
  CHECK(near(x, 0.0f) && near(y, 0.0f));
  CHECK(clientToCanvas(959.9, 479.9, 0.0, 0.0, 1280.0, 480.0, 640, 480, x, y));
  CHECK(x < 640.0f && y < 480.0f && x > 639.0f && y > 479.0f);
  CHECK(!clientToCanvas(960.0, 240.0, 0.0, 0.0, 1280.0, 480.0, 640, 480, x, y)); // right edge
}

static void client_to_canvas_with_offset() {
  float x = 0.0f, y = 0.0f;
  CHECK(clientToCanvas(110.0, 220.0, 10.0, 20.0, 640.0, 480.0, 640, 480, x, y));
  CHECK(near(x, 100.0f) && near(y, 200.0f));
}

static void client_to_canvas_css_scale() {
  // Element 320x240 CSS pixels showing a 640x480 buffer (high-DPI or CSS scaling).
  float x = 0.0f, y = 0.0f;
  CHECK(clientToCanvas(160.0, 120.0, 0.0, 0.0, 320.0, 240.0, 640, 480, x, y));
  CHECK(near(x, 320.0f) && near(y, 240.0f));
}

struct TestCase {
  const char *name;
  void (*run)();
};

static const TestCase kTests[] = {
    {"no_events_while_never_tracked", no_events_while_never_tracked},
    {"found_once_then_lost_once", found_once_then_lost_once},
    {"two_markers_same_frame", two_markers_same_frame},
    {"reset_forgets_state", reset_forgets_state},
    {"config_defaults", config_defaults},
    {"marker_list_splits_lines", marker_list_splits_lines},
    {"marker_list_drops_empty_lines", marker_list_drops_empty_lines},
    {"delta_is_zero_on_first_tick", delta_is_zero_on_first_tick},
    {"delta_in_seconds", delta_in_seconds},
    {"delta_is_clamped", delta_is_clamped},
    {"delta_never_negative", delta_never_negative},
    {"marker_center_pinball", marker_center_pinball},
    {"anchor_identity_pose_centres", anchor_identity_pose_centres},
    {"anchor_hidden_when_lost", anchor_hidden_when_lost},
    {"anchor_smoothing_halfway", anchor_smoothing_halfway},
    {"anchor_no_smoothing_across_loss", anchor_no_smoothing_across_loss},
    {"client_to_canvas_letterbox_wide", client_to_canvas_letterbox_wide},
    {"client_to_canvas_letterbox_tall", client_to_canvas_letterbox_tall},
    {"client_to_canvas_corners", client_to_canvas_corners},
    {"client_to_canvas_with_offset", client_to_canvas_with_offset},
    {"client_to_canvas_css_scale", client_to_canvas_css_scale},
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
