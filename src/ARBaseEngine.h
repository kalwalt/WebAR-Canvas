#ifndef AR_BASE_ENGINE_H
#define AR_BASE_ENGINE_H

#include <backend/DriverEnums.h>
#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/SwapChain.h>
#include <filament/Texture.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <utils/Entity.h>
#include <utils/EntityManager.h>

#include <string>
#include <vector>
#include <memory>

class ARNFTTracker;

namespace filament {
namespace backend {
class Platform;
}
} // namespace filament

using namespace filament;

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
  virtual void setup(Engine *engine, Scene *scene) = 0;
  virtual void update(float deltaTime) = 0;

protected:
  // Filament Core
  Engine *engine;
  Scene *scene;
  View *view;
  Renderer *renderer;
  Camera *camera;

  // Video Background Resources
  VertexBuffer *skyboxVb = nullptr;
  IndexBuffer *skyboxIb = nullptr;
  Material *videoMaterial = nullptr;
  MaterialInstance *videoMaterialInst = nullptr;
  Texture *videoTexture = nullptr;
  uint32_t nativeTextureId = 0;
  utils::Entity skyboxQuad;

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

private:
  static void tick(void *arg);
  void render();
  void updateVideoTexture();
  void createSkyboxQuad();
  filament::SwapChain *swapChain = nullptr;
  filament::backend::Platform *platform = nullptr;
  long glContext = 0;
};

#endif