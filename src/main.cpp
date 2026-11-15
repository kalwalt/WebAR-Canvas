#include "app/App.h"

// Entry point of the WASM application: the demo app, 640x480 camera frames, the pinball marker.
// The page can change these at runtime with Module.webarConfig (see ARBaseApp::applyPageConfig).
AR_APP_MAIN(App, 640, 480, "/assets/markers/pinball")
