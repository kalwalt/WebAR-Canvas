// Node test runner for the pure C++ parts of the framework core (target core_tests, built with
// -DWEBAR_CANVAS_BUILD_TESTS=ON). Run it with scripts/test-wasm.sh or `node build-docker.js --test`.
// Covers the pure headers of src/core/ (AREvents, ARAppConfig, ARAnchor, ARInput): no Filament,
// no tracker, no browser. The exit code is the number of failed checks.

#include "core/ARAnchor.h"
#include "core/ARAppConfig.h"
#include "core/AREvents.h"
#include "core/ARInput.h"
#include "core/ARMeshAnchors.h"
#include "core/ARPrimitives.h"

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

static void client_to_canvas_clamped_on_bars() {
  // Up events are delivered even off the image: the point is clamped to its edge.
  float x = -1.0f, y = -1.0f;
  CHECK(clientToCanvasClamped(100.0, 240.0, 0.0, 0.0, 1280.0, 480.0, 640, 480, x, y));
  CHECK(near(x, 0.0f) && near(y, 240.0f)); // left bar
  CHECK(clientToCanvasClamped(2000.0, -50.0, 0.0, 0.0, 1280.0, 480.0, 640, 480, x, y));
  CHECK(near(x, 640.0f) && near(y, 0.0f)); // outside the element
  CHECK(clientToCanvasClamped(640.0, 240.0, 0.0, 0.0, 1280.0, 480.0, 640, 480, x, y));
  CHECK(near(x, 320.0f) && near(y, 240.0f)); // inside: same as clientToCanvas
  CHECK(!clientToCanvasClamped(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 640, 480, x, y)); // no size yet
}

// --- ARMeshAnchors: mesh ids bound to markers (the backend-neutral mesh API, #13) ---

// A pose source for ARMeshAnchors::update(): one marker, counting the reads.
struct FakePoses {
  bool tracked = true;
  float pose[16];
  float centerX = 0.0f, centerY = 0.0f;
  int reads = 0;
  int lastMarker = -1;
  FakePoses() { setIdentity(pose); }
  bool operator()(int marker, float out[16], float &cx, float &cy) {
    ++reads;
    lastMarker = marker;
    for (int i = 0; i < 16; ++i) out[i] = pose[i];
    cx = centerX;
    cy = centerY;
    return tracked;
  }
};

// The world matrices update() applied, by mesh id.
struct Applied {
  std::vector<std::vector<float>> world = std::vector<std::vector<float>>(8);
  std::vector<int> count = std::vector<int>(8, 0);
  void operator()(int id, const float m[16]) {
    world[id].assign(m, m + 16);
    ++count[id];
  }
};

static void translation(float m[16], float x, float y, float z) {
  setIdentity(m);
  m[12] = x;
  m[13] = y;
  m[14] = z;
}

static void mesh_unbound_world_is_local() {
  ARMeshAnchors meshes;
  float local[16];
  translation(local, 1.0f, 2.0f, 3.0f);
  meshes.setLocal(0, local);
  FakePoses poses;
  Applied applied;
  meshes.update(true, poses, applied);
  CHECK(applied.count[0] == 1 && poses.reads == 0);
  CHECK(near(applied.world[0][12], 1.0f) && near(applied.world[0][13], 2.0f) &&
        near(applied.world[0][14], 3.0f) && near(applied.world[0][15], 1.0f));
}

static void mesh_bound_identity_pose_centres() {
  ARMeshAnchors meshes;
  meshes.attach(0, 0);
  FakePoses poses;
  poses.centerX = 10.0f;
  poses.centerY = 20.0f;
  Applied applied;
  meshes.update(true, poses, applied);
  CHECK(near(applied.world[0][12], 10.0f) && near(applied.world[0][13], 20.0f) &&
        near(applied.world[0][14], 0.0f) && near(applied.world[0][0], 1.0f));
}

static void mesh_anchor_then_local() {
  // world = anchored * local: a local offset along x follows the marker's rotation.
  ARMeshAnchors meshes;
  ARAnchorOptions options;
  options.centerOnMarker = false;
  meshes.attach(0, 0, options);
  float local[16];
  translation(local, 1.0f, 0.0f, 0.0f);
  meshes.setLocal(0, local);
  FakePoses poses; // rotation of 90 degrees about z: x -> y
  poses.pose[0] = 0.0f;
  poses.pose[1] = 1.0f;
  poses.pose[4] = -1.0f;
  poses.pose[5] = 0.0f;
  Applied applied;
  meshes.update(true, poses, applied);
  CHECK(near(applied.world[0][12], 0.0f) && near(applied.world[0][13], 1.0f));
}

static void mesh_hidden_while_lost() {
  ARMeshAnchors meshes;
  meshes.attach(0, 0);
  FakePoses poses;
  poses.tracked = false;
  Applied applied;
  meshes.update(true, poses, applied);
  bool allZero = applied.count[0] == 1;
  for (float v : applied.world[0]) allZero = allZero && v == 0.0f;
  CHECK(allZero);
}

static void mesh_pose_read_once_per_camera_frame() {
  ARMeshAnchors meshes;
  meshes.attach(0, 0);
  FakePoses poses;
  Applied applied;
  meshes.update(false, poses, applied); // first update after binding: reads the pose anyway
  CHECK(poses.reads == 1);
  meshes.update(false, poses, applied); // same camera frame: no read, the world is reapplied
  CHECK(poses.reads == 1 && applied.count[0] == 2);
  meshes.update(true, poses, applied);
  CHECK(poses.reads == 2 && applied.count[0] == 3);
}

static void mesh_rebind_and_detach() {
  ARMeshAnchors meshes;
  meshes.attach(0, 0);
  meshes.attach(0, 1); // re-binding replaces the first binding
  FakePoses poses;
  Applied applied;
  meshes.update(true, poses, applied);
  CHECK(poses.lastMarker == 1 && poses.reads == 1);
  meshes.detach(0);
  meshes.update(true, poses, applied);
  CHECK(poses.reads == 1 && near(applied.world[0][12], 0.0f) && near(applied.world[0][15], 1.0f));
}

static void mesh_world_now_uses_the_new_local() {
  // setMeshLocalTransform() in update() must reach this frame's render: world() composes the
  // last anchor with the new local transform, without reading the pose again.
  ARMeshAnchors meshes;
  meshes.attach(0, 0);
  FakePoses poses;
  poses.centerX = 10.0f;
  Applied applied;
  meshes.update(true, poses, applied);
  float local[16];
  translation(local, 0.0f, 0.0f, 5.0f);
  meshes.setLocal(0, local);
  float world[16];
  CHECK(meshes.world(0, world));
  CHECK(near(world[12], 10.0f) && near(world[14], 5.0f) && poses.reads == 1);
  CHECK(!meshes.world(3, world)); // unknown id
}

static void mesh_remove_forgets() {
  ARMeshAnchors meshes;
  meshes.attach(2, 0);
  meshes.remove(2);
  FakePoses poses;
  Applied applied;
  meshes.update(true, poses, applied);
  CHECK(applied.count[2] == 0 && poses.reads == 0);
}

// --- ARPrimitives: geometry for createBox / createPlane / createSphere ---

static const float kColors[6][4] = {{1, 0, 0, 1}, {0, 1, 1, 1}, {1, 1, 0, 1},
                                    {0, 0, 1, 1}, {1, 0, 1, 1}, {0, 1, 0, 1}};

static bool indicesInRange(const ARMesh &mesh) {
  for (uint16_t i : mesh.indices) {
    if (i >= mesh.vertices.size()) return false;
  }
  return mesh.indices.size() % 3 == 0;
}

static void box_counts() {
  const ARMesh box = makeBox(80.0f, 60.0f, 40.0f, kColors);
  CHECK(box.vertices.size() == 24 && box.indices.size() == 36);
  float minZ = 1e9f, maxZ = -1e9f, maxX = -1e9f, maxY = -1e9f;
  for (const ARVertex &v : box.vertices) {
    minZ = std::fmin(minZ, v.position[2]);
    maxZ = std::fmax(maxZ, v.position[2]);
    maxX = std::fmax(maxX, v.position[0]);
    maxY = std::fmax(maxY, v.position[1]);
  }
  CHECK(near(minZ, 0.0f) && near(maxZ, 40.0f)); // base on z = 0
  CHECK(near(maxX, 40.0f) && near(maxY, 30.0f)); // centred in x and y
  // First face (+Z, the top, z = sizeZ) in the first colour, as the demo cube.
  CHECK(near(box.vertices[0].position[2], 40.0f) && box.vertices[0].color[0] == 1.0f &&
        box.vertices[0].color[1] == 0.0f);
}

static void plane_counts() {
  const float color[4] = {0.5f, 0.5f, 0.5f, 1.0f};
  const ARMesh plane = makePlane(100.0f, 50.0f, color);
  CHECK(plane.vertices.size() == 4 && plane.indices.size() == 6);
  bool flat = true;
  for (const ARVertex &v : plane.vertices) flat = flat && v.position[2] == 0.0f;
  CHECK(flat);
}

static void sphere_counts_and_radius() {
  const float color[4] = {1, 1, 1, 1};
  const ARMesh sphere = makeSphere(25.0f, 8, 4, color);
  CHECK(sphere.vertices.size() == (4 + 1) * (8 + 1));
  CHECK(sphere.indices.size() == 4 * 8 * 6);
  bool onSurface = true;
  for (const ARVertex &v : sphere.vertices) {
    const float r = std::sqrt(v.position[0] * v.position[0] + v.position[1] * v.position[1] +
                              v.position[2] * v.position[2]);
    onSurface = onSurface && std::fabs(r - 25.0f) < 1e-3f;
  }
  CHECK(onSurface);
}

static void sphere_minimum_segments() {
  const float color[4] = {1, 1, 1, 1};
  const ARMesh small = makeSphere(1.0f, 1, 1, color);
  const ARMesh minimum = makeSphere(1.0f, 3, 2, color);
  CHECK(small.vertices.size() == minimum.vertices.size());
  CHECK(small.indices.size() == minimum.indices.size());
}

static void sphere_fits_16_bit_indices() {
  const float color[4] = {1, 1, 1, 1};
  const ARMesh big = makeSphere(1.0f, 400, 400, color);
  CHECK(big.vertices.size() <= 65536);
  CHECK(indicesInRange(big));
}

static void primitive_indices_in_range() {
  const float color[4] = {1, 1, 1, 1};
  CHECK(indicesInRange(makeBox(1.0f, 1.0f, 1.0f, kColors)));
  CHECK(indicesInRange(makePlane(1.0f, 1.0f, color)));
  CHECK(indicesInRange(makeSphere(1.0f, 24, 16, color)));
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
    {"client_to_canvas_clamped_on_bars", client_to_canvas_clamped_on_bars},
    {"mesh_unbound_world_is_local", mesh_unbound_world_is_local},
    {"mesh_bound_identity_pose_centres", mesh_bound_identity_pose_centres},
    {"mesh_anchor_then_local", mesh_anchor_then_local},
    {"mesh_hidden_while_lost", mesh_hidden_while_lost},
    {"mesh_pose_read_once_per_camera_frame", mesh_pose_read_once_per_camera_frame},
    {"mesh_rebind_and_detach", mesh_rebind_and_detach},
    {"mesh_world_now_uses_the_new_local", mesh_world_now_uses_the_new_local},
    {"mesh_remove_forgets", mesh_remove_forgets},
    {"box_counts", box_counts},
    {"plane_counts", plane_counts},
    {"sphere_counts_and_radius", sphere_counts_and_radius},
    {"sphere_minimum_segments", sphere_minimum_segments},
    {"primitive_indices_in_range", primitive_indices_in_range},
    {"sphere_fits_16_bit_indices", sphere_fits_16_bit_indices},
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
