#ifndef AR_MODEL_H
#define AR_MODEL_H

#include "core/ARModelError.h"

#include <string>
#include <vector>

#include <utils/Entity.h>

namespace filament {
class Engine;
class IndirectLight;
class Scene;
namespace gltfio {
class AssetLoader;
class FilamentAsset;
class MaterialProvider;
class ResourceLoader;
class TextureProvider;
} // namespace gltfio
} // namespace filament

/**
 * glTF loading with Filament's gltfio (Filament only; built with WEBAR_CANVAS_GLTF): the
 * ubershader material archive, the asset loader and the resource loader, created on the first
 * load, and the loaded assets, released in the destructor (before the Filament engine).
 */
class ARModelLoader {
public:
  ARModelLoader(filament::Engine *engine, filament::Scene *scene);
  ~ARModelLoader();

  ARModelLoader(const ARModelLoader &) = delete;
  ARModelLoader &operator=(const ARModelLoader &) = delete;

  // Loads path (a .glb or a .gltf with embedded or relative resources) and adds the model to the
  // scene. Returns a handle >= 0 or an ARModelError.
  int load(const std::string &path);

  // The model's root entity (no parent), or an invalid entity for an unknown handle.
  utils::Entity root(int handle) const;

  // The model's bounding box in its own units (glTF: metres, +Y up); false for an unknown handle.
  bool bounds(int handle, float min[3], float max[3]) const;

  // Adds the model's entities to the scene, or removes them.
  void setVisible(int handle, bool visible);

private:
  void addDefaultLighting(); // on the first load: a sun and, if none is set, an ambient light

  filament::Engine *engine;
  filament::Scene *scene;
  filament::gltfio::MaterialProvider *materials = nullptr;
  filament::gltfio::AssetLoader *loader = nullptr;
  filament::gltfio::TextureProvider *imageDecoder = nullptr;
  std::vector<filament::gltfio::FilamentAsset *> assets;
  utils::Entity sun;
  filament::IndirectLight *ambient = nullptr;
};

#endif // AR_MODEL_H
