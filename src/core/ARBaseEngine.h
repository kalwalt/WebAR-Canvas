#ifndef AR_BASE_ENGINE_H
#define AR_BASE_ENGINE_H

#include "core/ARAppConfig.h"
#include "core/AREvents.h"
#include "core/IARRenderer.h"

#include <array>
#include <memory>
#include <vector>

class ARNFTTracker;

/**
 * The engine of WebAR-Canvas: the main loop, the CPU video path (frame pacing, frame capture,
 * NFT tracking with ARNFTTracker) and the frame timing probe. It draws only through
 * IARRenderer. Apps derive from ARBaseApp, which adds the getters and runApp().
 *
 * Each tick: on a new camera frame, the video texture upload, the capture and the tracking
 * (with onMarkerFound / onMarkerLost); then update(), draw() and the render.
 */
class ARBaseEngine {
public:
  ARBaseEngine();
  virtual ~ARBaseEngine();

  ARBaseEngine(const ARBaseEngine &) = delete;
  ARBaseEngine &operator=(const ARBaseEngine &) = delete;

  // Creates the renderer and the tracker, loads the camera and the markers, then calls setup().
  // False if any step fails (setup() is not called then).
  bool init(const ARAppConfig &config);

  // Start the main Emscripten loop (does not return)
  void run();

protected:
  // App hooks, no-ops by default.
  virtual void setup() {}                     // once, at the end of a successful init()
  virtual void update(float deltaTime) {}     // every tick; seconds since the previous tick (0 first, at most 0.1)
  virtual void draw() {}                      // every tick, after update(), before the render
  virtual void onMarkerFound(int markerIndex) {}
  virtual void onMarkerLost(int markerIndex) {}

  // Marker 0's state and pose (column-major OpenGL model-view), updated on camera frames.
  float projectionMatrix[16] = {0};
  float modelViewMatrix[16] = {0};
  bool isTracking = false;

  const float *getProjectionMatrix() const { return projectionMatrix; }
  const float *getModelViewMatrix() const { return modelViewMatrix; }
  bool getIsTracking() const { return isTracking; }
  ARNFTTracker *getNFTTracker() const { return nftTracker.get(); }

  // Marker markerIndex's model-view matrix (column-major) on the last processed camera frame.
  // False, and zeros in out, when the marker is not tracked or does not exist.
  bool getMarkerPose(int markerIndex, float out[16]) const;
  // True on a tick that captured and tracked a new camera frame (poses change only then).
  bool isNewCameraFrame() const { return frameCaptured; }

  // Internal step just before update() every tick (ARBaseApp applies the marker anchors here).
  virtual void beforeUpdate() {}

  // The rendering backend; valid after a successful init().
  IARRenderer &getRenderer() { return *renderer; }
  const IARRenderer &getRenderer() const { return *renderer; }

  // Creates the rendering backend; called once by init(). ARBaseApp picks it at build time.
  virtual std::unique_ptr<IARRenderer> createRenderer() = 0;

  void updateTracking();

private:
  static void tick(void *arg);
  void profiledTick();
  void updateVideoFrame();
  void dispatchMarkerEvents();
  float nextDeltaTime(); // seconds since the previous tick (computeDeltaSeconds)

  std::unique_ptr<ARNFTTracker> nftTracker;
  bool frameCaptured = false;
  double lastTickMs = -1.0;
  MarkerEventTracker markerEvents;
  std::vector<bool> markerTracked;
  std::vector<std::array<float, 16>> markerPoses; // per marker, from the last processed frame

  std::unique_ptr<IARRenderer> renderer;
};

#endif
