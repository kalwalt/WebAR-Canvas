#ifndef FILAMENT_RENDERER_H
#define FILAMENT_RENDERER_H

#include "core/IARRenderer.h"

#include <utils/Entity.h>

#include <cstdint>
#include <vector>

namespace filament {
class Camera;
class Engine;
class IndexBuffer;
class Material;
class MaterialInstance;
class Renderer;
class Scene;
class SwapChain;
class Texture;
class VertexBuffer;
class View;
namespace backend {
class Platform;
}
} // namespace filament

/**
 * IARRenderer on Google Filament (WebGL 2): the camera video is a WebGL texture imported into
 * Filament and drawn on a device-space quad (video_background.mat); the app adds its own
 * entities to getScene().
 */
class FilamentRenderer final : public IARRenderer {
public:
  FilamentRenderer() = default;
  ~FilamentRenderer() override;

  FilamentRenderer(const FilamentRenderer &) = delete;
  FilamentRenderer &operator=(const FilamentRenderer &) = delete;

  bool init(const char *canvasSelector, int width, int height) override;
  void setProjection(const double projection[16], double nearPlane, double farPlane) override;
  void updateVideoTexture() override;
  void render() override;

  filament::Engine *getEngine() const { return engine; }
  filament::Scene *getScene() const { return scene; }
  filament::Camera *getCamera() const { return camera; }
  filament::View *getView() const { return view; }

  int addMesh(const ARMesh &mesh) override;
  void setMeshTransform(int id, const float transform[16]) override;
  void setMeshVisible(int id, bool visible) override;
  void removeMesh(int id) override;

  // A renderable entity for mesh with the unlit vertex-colour material (ar_cube.mat), added to
  // the scene, and its buffers in vertices / indices: the caller destroys all three. Shared by
  // addMesh() and ARBaseApp's Filament-only createBox & co. Invalid entity on failure.
  utils::Entity createVertexColorRenderable(const ARMesh &mesh, filament::VertexBuffer *&vertices,
                                            filament::IndexBuffer *&indices);

private:
  void createVideoBackground(int width, int height);
  bool loadVertexColorMaterial();

  struct Mesh {
    bool alive = false;
    bool visible = true;
    utils::Entity entity;
    filament::VertexBuffer *vertices = nullptr;
    filament::IndexBuffer *indices = nullptr;
  };
  Mesh *findMesh(int id);
  void destroyMesh(Mesh &mesh);

  filament::backend::Platform *platform = nullptr;
  filament::Engine *engine = nullptr;
  filament::SwapChain *swapChain = nullptr;
  filament::Renderer *renderer = nullptr;
  filament::Scene *scene = nullptr;
  filament::View *view = nullptr;
  filament::Camera *camera = nullptr;

  // Video background
  filament::VertexBuffer *videoQuadVb = nullptr;
  filament::IndexBuffer *videoQuadIb = nullptr;
  filament::Material *videoMaterial = nullptr;
  filament::MaterialInstance *videoMaterialInst = nullptr;
  filament::Texture *videoTexture = nullptr;
  uint32_t nativeTextureId = 0;
  utils::Entity videoQuad;

  // Meshes: the backend-neutral API (by id) and the vertex-colour material they share with
  // ARBaseApp's createBox & co.
  std::vector<Mesh> meshes;
  filament::Material *vertexColorMaterial = nullptr;
  filament::MaterialInstance *vertexColorInstance = nullptr;
};

#endif // FILAMENT_RENDERER_H
