#include "core/ARBaseApp.h"
#ifdef WEBAR_RENDERER_FILAMENT
#include "core/FilamentRenderer.h"
#endif

#include <emscripten.h>

#include <cstdlib>
#include <iostream>
#include <string>

// Module.webarConfig, read without Embind: the page sets it before the module starts.
EM_JS_DEPS(webar_config, "$stringToNewUTF8,$UTF8ToString");

// A positive integer field (a JS number up to 16384), or fallback otherwise.
EM_JS(int, webar_config_int, (const char *key, int fallback), {
  var config = Module.webarConfig;
  if (!config || typeof config !== 'object') return fallback;
  var value = config[UTF8ToString(key)];
  return (typeof value === 'number' && Number.isInteger(value) && value > 0 && value <= 16384)
      ? value : fallback;
});

// A string field as a malloc'd UTF-8 copy, or null when it is missing or not a string.
EM_JS(char *, webar_config_string, (const char *key), {
  var config = Module.webarConfig;
  if (!config || typeof config !== 'object') return 0;
  var value = config[UTF8ToString(key)];
  return (typeof value === 'string') ? stringToNewUTF8(value) : 0;
});

// The markers array joined with newlines (strings only), or null when there is no array.
EM_JS(char *, webar_config_markers, (), {
  var config = Module.webarConfig;
  if (!config || typeof config !== 'object' || !Array.isArray(config.markers)) return 0;
  var list = config.markers.filter(function(m) { return typeof m === 'string'; });
  return stringToNewUTF8(list.join('\n'));
});

static void applyString(const char *key, std::string &field) {
  if (char *value = webar_config_string(key)) {
    field = value;
    std::free(value);
  }
}

void ARBaseApp::applyPageConfig(ARAppConfig &config) {
  applyString("canvasSelector", config.canvasSelector);
  applyString("cameraParamPath", config.cameraParamPath);
  config.videoWidth = webar_config_int("videoWidth", config.videoWidth);
  config.videoHeight = webar_config_int("videoHeight", config.videoHeight);
  if (char *markers = webar_config_markers()) {
    config.markerPaths = parseMarkerList(markers);
    std::free(markers);
    if (config.markerPaths.empty()) {
      std::cerr << "Module.webarConfig.markers has no marker path: no marker will be tracked"
                << std::endl;
    }
  }
}

int ARBaseApp::runApp(ARAppConfig config) {
  applyPageConfig(config);
  std::cout << "Initializing WebAR-Canvas with NFT..." << std::endl;
  if (!init(config)) {
    std::cerr << "Error during initialization" << std::endl;
    return 1;
  }
  run();
  return 0;
}

#ifdef WEBAR_RENDERER_FILAMENT
std::unique_ptr<IARRenderer> ARBaseApp::createRenderer() {
  auto backend = std::make_unique<FilamentRenderer>();
  filamentBackend = backend.get();
  return backend;
}

filament::Engine *ARBaseApp::getEngine() const { return filamentBackend ? filamentBackend->getEngine() : nullptr; }

filament::Scene *ARBaseApp::getScene() const { return filamentBackend ? filamentBackend->getScene() : nullptr; }

filament::Camera *ARBaseApp::getCamera() const { return filamentBackend ? filamentBackend->getCamera() : nullptr; }
#else
#error "No rendering backend: build with WEBAR_RENDERER_FILAMENT (set by CMakeLists.txt)."
#endif
