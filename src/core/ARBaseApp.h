#ifndef AR_BASE_APP_H
#define AR_BASE_APP_H

#include "core/ARAnchor.h"
#include "core/ARAppConfig.h"
#include "core/ARBaseEngine.h"
#include "core/ARModelError.h"
#include "core/ARPrimitives.h"

#include <memory>
#include <string>
#include <vector>

#ifdef WEBAR_RENDERER_FILAMENT
#include <utils/Entity.h>
#endif

class FilamentRenderer;
class ARModelLoader;

namespace filament {
class Camera;
class Engine;
class IndexBuffer;
class Material;
class MaterialInstance;
class Scene;
class VertexBuffer;
} // namespace filament

/**
 * The base class of a WebAR-Canvas app, in the openFrameworks / Processing style (#2):
 *
 *   class App : public ARBaseApp {
 *     void setup() override;                 // once, when the renderer and tracker are ready
 *     void update(float deltaTime) override; // every frame
 *     void draw() override;                  // every frame, before the render
 *     void onMarkerFound(int markerIndex) override;
 *     void onMarkerLost(int markerIndex) override;
 *   };
 *   AR_APP_MAIN(App, 640, 480, "/assets/markers/pinball")
 *
 * Marker 0's pose is in getModelViewMatrix() / getIsTracking(); found / lost events follow the
 * raw tracking state of every marker, with no debounce.
 */
class ARBaseApp : public ARBaseEngine {
public:
  ARBaseApp();           // defined in ARBaseApp.cpp, where ARModelLoader is complete
  ~ARBaseApp() override; // releases the entities created with the create* helpers

  // Applies Module.webarConfig (if the page set one) over config, then init() and the main
  // loop. Returns 1 if init() fails ("Error during initialization" on stderr); otherwise the
  // loop runs and never returns.
  int runApp(ARAppConfig config);

  // Overwrites the fields that Module.webarConfig sets: canvasSelector, cameraParamPath
  // (strings), videoWidth, videoHeight (positive integers) and markers (array of strings).
  // Without Module.webarConfig nothing changes.
  static void applyPageConfig(ARAppConfig &config);

  // Loads a .glb / .gltf from the Emscripten file system and adds it to the scene, at any time
  // after init() (a page can write the file first, e.g. with Module.FS_createDataFile). Returns
  // a handle >= 0 or an ARModelError (core/ARModelError.h). Glb loading uses Filament's gltfio.
  int loadModel(const std::string &path);

#if defined(WEBAR_RENDERER_FILAMENT)
  // The model's root entity (invalid for an unknown handle).
  utils::Entity getModelEntity(int handle) const;
  // Stands the model on marker markerIndex's centre: glTF +Y up becomes the marker's +Z, and the
  // largest extent of its bounding box becomes sizeMm. Hidden while the marker is lost.
  // False for an unknown handle or marker.
  bool placeModelOnMarker(int handle, int markerIndex, float sizeMm);
  // Adds the model to the scene, or removes it.
  void setModelVisible(int handle, bool visible);
#endif

  // The app running in this page (set by runApp()), for the functions exported to the page.
  static ARBaseApp *current();

private:
  friend struct ARInputBridge;
  // Routes the canvas's touch and mouse events and the window's resize events to the input
  // hooks (src/core/ARInput.cpp); runApp() calls it after a successful init().
  void installInputCallbacks(const std::string &canvasSelector);

protected:
  ARNFTTracker *getTracker() const { return getNFTTracker(); }

  // Input hooks (no-ops by default). x, y are canvas drawing-buffer pixels (0..videoWidth,
  // 0..videoHeight). Down and move events on the letterbox bars of `object-fit: contain` are
  // not delivered; every touch or button whose down event was gets its up event, clamped to the
  // image's edge if it ends off the image. pointerId is the browser's touch identifier for
  // touches (multi-touch) and 0 for the mouse.
  virtual void touchDown(float x, float y, int pointerId) {}
  virtual void touchMoved(float x, float y, int pointerId) {}
  virtual void touchUp(float x, float y, int pointerId) {} // also on touchcancel
  virtual void mouseDown(float x, float y, int button, int pointerId) {}
  virtual void mouseMoved(float x, float y, int pointerId) {}
  virtual void mouseUp(float x, float y, int button, int pointerId) {}
  virtual void windowResized(int width, int height) {} // window inner size, once per change

  // The backend chosen at build time (WEBAR_RENDERER_FILAMENT).
  std::unique_ptr<IARRenderer> createRenderer() override;

#ifdef WEBAR_RENDERER_FILAMENT
  // The Filament objects behind getRenderer(): an app that uses them is a Filament app.
  filament::Engine *getEngine() const;
  filament::Scene *getScene() const;
  filament::Camera *getCamera() const;

  // Binds an entity to marker markerIndex: before every update() its transform becomes the
  // marker's pose (centred on the marker by default, see ARAnchorOptions), or scale 0 while the
  // marker is not tracked. The entity gets a TransformManager component if it has none.
  void attachToMarker(utils::Entity entity, int markerIndex, ARAnchorOptions options = {});
  void detachFromMarker(utils::Entity entity);

  void beforeUpdate() override;

  // Primitive geometry (ARPrimitives.h) as renderable entities with the unlit vertex-colour
  // material, added to the scene, in millimetres. ARBaseApp owns and releases them; the
  // returned entity has no transform component (attach it, or parent it, as needed).
  // Invalid entity if called before init() or if the material cannot be loaded.
  utils::Entity createBox(float sizeX, float sizeY, float sizeZ, const float faceColors[6][4]);
  utils::Entity createCube(float size, const float color[4]);
  utils::Entity createPlane(float sizeX, float sizeY, const float color[4]);
  utils::Entity createSphere(float radius, const float color[4], int segments = 24,
                             int rings = 16);

private:
  struct MarkerBinding {
    utils::Entity entity;
    int markerIndex;
    ARAnchor anchor;
    bool updated = false; // false until the first update after binding
  };
  struct OwnedMesh {
    utils::Entity entity;
    filament::VertexBuffer *vertices;
    filament::IndexBuffer *indices;
  };
  utils::Entity createMeshEntity(const ARMesh &mesh);

  std::vector<MarkerBinding> markerBindings;
  std::vector<OwnedMesh> ownedMeshes;
  filament::Material *vertexColorMaterial = nullptr;
  filament::MaterialInstance *vertexColorInstance = nullptr;
#ifdef WEBAR_CANVAS_GLTF
  std::unique_ptr<ARModelLoader> models;        // created on the first loadModel()
#endif
  std::vector<utils::Entity> modelAnchors;      // per model handle, from placeModelOnMarker()
  FilamentRenderer *filamentBackend = nullptr; // the object behind getRenderer()
#endif
};

// An app's main(): only sugar over ARAppConfig, which can also be filled in code.
#define AR_APP_MAIN(AppClass, width, height, markerPath) \
  int main() {                                           \
    ARAppConfig config;                                  \
    config.videoWidth = (width);                         \
    config.videoHeight = (height);                       \
    config.markerPaths = {(markerPath)};                 \
    static AppClass app;                                 \
    return app.runApp(config);                           \
  }

#endif // AR_BASE_APP_H
