#ifndef AR_BASE_APP_H
#define AR_BASE_APP_H

#include "core/ARAppConfig.h"
#include "core/ARBaseEngine.h"

class FilamentRenderer;

namespace filament {
class Camera;
class Engine;
class Scene;
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
  // Applies Module.webarConfig (if the page set one) over config, then init() and the main
  // loop. Returns 1 if init() fails ("Error during initialization" on stderr); otherwise the
  // loop runs and never returns.
  int runApp(ARAppConfig config);

  // Overwrites the fields that Module.webarConfig sets: canvasSelector, cameraParamPath
  // (strings), videoWidth, videoHeight (positive integers) and markers (array of strings).
  // Without Module.webarConfig nothing changes.
  static void applyPageConfig(ARAppConfig &config);

protected:
  ARNFTTracker *getTracker() const { return getNFTTracker(); }

  // The backend chosen at build time (WEBAR_RENDERER_FILAMENT).
  std::unique_ptr<IARRenderer> createRenderer() override;

#ifdef WEBAR_RENDERER_FILAMENT
  // The Filament objects behind getRenderer(): an app that uses them is a Filament app.
  filament::Engine *getEngine() const;
  filament::Scene *getScene() const;
  filament::Camera *getCamera() const;

private:
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
