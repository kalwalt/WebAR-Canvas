#ifndef AR_MESH_ANCHORS_H
#define AR_MESH_ANCHORS_H

#include "core/ARAnchor.h"

#include <vector>

/**
 * The transforms of the backend-neutral meshes (#13; ARBaseApp::addBox & co.), without a
 * renderer: pure C++, tested in tests/core_tests.cpp.
 *
 * Each mesh id has a local transform (identity by default, set by the app, e.g. a spin) and
 * may be bound to a marker through an ARAnchor. Every tick, update() gives each known mesh its
 * world matrix (camera space, column-major):
 *   - bound and tracked: anchored * local (the local transform is relative to the marker);
 *   - bound and lost: all zeros (nothing is drawn, as Filament's scale-0 transform);
 *   - not bound: local.
 * A bound mesh reads its marker's pose only on a new camera frame (and on the first update
 * after binding), so smoothing does not depend on the display's frame rate.
 */
class ARMeshAnchors {
public:
  // Binds mesh id to marker markerIndex, replacing an earlier binding of the same id.
  void attach(int id, int markerIndex, ARAnchorOptions options = {}) {
    Entry &e = entry(id);
    e.bound = true;
    e.markerIndex = markerIndex;
    e.anchor = ARAnchor(options);
    e.updated = false;
  }

  // Unbinds mesh id: its world matrix becomes its local transform.
  void detach(int id) {
    if (Entry *e = find(id)) e->bound = false;
  }

  void setLocal(int id, const float local[16]) {
    Entry &e = entry(id);
    for (int i = 0; i < 16; ++i) e.local[i] = local[i];
  }

  // Forgets mesh id (the mesh was removed).
  void remove(int id) {
    if (Entry *e = find(id)) *e = Entry();
  }

  // getPose(markerIndex, float pose[16], float &centerX, float &centerY) -> bool tracked;
  // apply(id, const float world[16]) is called for every known mesh.
  template <class PoseFn, class ApplyFn>
  void update(bool newCameraFrame, PoseFn &&getPose, ApplyFn &&apply) {
    for (int id = 0; id < static_cast<int>(entries.size()); ++id) {
      Entry &e = entries[id];
      if (!e.known) continue;
      float world[16];
      if (e.bound) {
        if (newCameraFrame || !e.updated) {
          float pose[16];
          float centerX = 0.0f, centerY = 0.0f;
          const bool tracked = getPose(e.markerIndex, pose, centerX, centerY);
          e.anchor.update(tracked, pose, centerX, centerY, e.anchored);
          e.updated = true;
        }
        if (e.anchor.visible()) {
          multiply(e.anchored, e.local, world);
        } else {
          for (float &v : world) v = 0.0f;
        }
      } else {
        for (int i = 0; i < 16; ++i) world[i] = e.local[i];
      }
      apply(id, world);
    }
  }

private:
  struct Entry {
    bool known = false;
    bool bound = false;
    int markerIndex = 0;
    ARAnchor anchor;
    bool updated = false; // the anchor has read a pose since binding
    float anchored[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float local[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  };

  Entry &entry(int id) {
    if (id >= static_cast<int>(entries.size())) entries.resize(id + 1);
    entries[id].known = true;
    return entries[id];
  }

  Entry *find(int id) {
    return id >= 0 && id < static_cast<int>(entries.size()) && entries[id].known ? &entries[id]
                                                                                 : nullptr;
  }

  // out = a * b, column-major.
  static void multiply(const float a[16], const float b[16], float out[16]) {
    for (int c = 0; c < 4; ++c) {
      for (int r = 0; r < 4; ++r) {
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k) sum += a[k * 4 + r] * b[c * 4 + k];
        out[c * 4 + r] = sum;
      }
    }
  }

  std::vector<Entry> entries; // by mesh id
};

#endif // AR_MESH_ANCHORS_H
