#ifndef AR_MODEL_ERROR_H
#define AR_MODEL_ERROR_H

/** Error codes of ARBaseApp::loadModel() (#2 phase 5); handles are >= 0. */
enum ARModelError {
  AR_MODEL_NOT_FOUND = -1,   // the file cannot be opened in the Emscripten file system
  AR_MODEL_INVALID = -2,     // not a glTF / GLB that gltfio can load
  AR_MODEL_UNSUPPORTED = -3, // built without WEBAR_CANVAS_GLTF or without the Filament backend
  AR_MODEL_NOT_READY = -4,   // called before init()
};

#endif // AR_MODEL_ERROR_H
