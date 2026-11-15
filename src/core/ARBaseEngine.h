#ifndef AR_BASE_ENGINE_H
#define AR_BASE_ENGINE_H

#include "core/IARRenderer.h"

#include <memory>
#include <string>

class ARNFTTracker;
class FilamentRenderer;

namespace filament {
class Engine;
class Scene;
} // namespace filament

/**
 * The engine of WebAR-Canvas: the main loop, the CPU video path (frame pacing, frame capture,
 * NFT tracking with ARNFTTracker) and the frame timing probe. It draws only through
 * IARRenderer; FilamentRenderer is the backend.
 */
class ARBaseEngine {
public:
  ARBaseEngine();
  virtual ~ARBaseEngine();

  // System initialization
  bool init(const char *canvasId, int width, int height,
            const std::string &markerPath);

  // Start the main Emscripten loop
  void run();

  // Methods to be implemented in the developer's derived class
  virtual void setup(filament::Engine *engine, filament::Scene *scene) = 0;
  virtual void update(float deltaTime) = 0;

protected:
  // NFT Tracker & Data
  std::unique_ptr<ARNFTTracker> nftTracker;
  float projectionMatrix[16] = {0};
  float modelViewMatrix[16] = {0};
  bool isTracking = false;

  void updateTracking();
  const float* getProjectionMatrix() const { return projectionMatrix; }
  const float* getModelViewMatrix() const { return modelViewMatrix; }
  bool getIsTracking() const { return isTracking; }
  ARNFTTracker* getNFTTracker() const { return nftTracker.get(); }

  // The rendering backend (valid after init() created it).
  IARRenderer &getRenderer() const;
  FilamentRenderer *filamentRenderer() const { return renderer.get(); }

private:
  static void tick(void *arg);
  void profiledTick();
  bool frameCaptured = false;
  void updateVideoFrame();

  std::unique_ptr<FilamentRenderer> renderer;
};

#endif
