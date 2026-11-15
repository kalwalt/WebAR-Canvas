#include "core/ARBaseApp.h"
#include "nft/ARNFTTracker.h"
#ifdef WEBAR_RENDERER_FILAMENT
#include "core/FilamentRenderer.h"
#include <filament/Box.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>
#endif

#include <emscripten.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
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
  installInputCallbacks(config.canvasSelector);
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

void ARBaseApp::attachToMarker(utils::Entity entity, int markerIndex, ARAnchorOptions options) {
  detachFromMarker(entity);
  if (filament::Engine *engine = getEngine()) {
    auto &tcm = engine->getTransformManager();
    if (!tcm.hasComponent(entity)) tcm.create(entity);
    tcm.setTransform(tcm.getInstance(entity), filament::math::mat4f::scaling(0.0f)); // hidden
  }
  markerBindings.push_back({entity, markerIndex, ARAnchor(options)});
}

void ARBaseApp::detachFromMarker(utils::Entity entity) {
  markerBindings.erase(std::remove_if(markerBindings.begin(), markerBindings.end(),
                                      [entity](const MarkerBinding &b) { return b.entity == entity; }),
                       markerBindings.end());
}

void ARBaseApp::beforeUpdate() {
  filament::Engine *engine = getEngine();
  if (!engine || markerBindings.empty()) return;
  auto &tcm = engine->getTransformManager();
  for (MarkerBinding &binding : markerBindings) {
    float pose[16], anchored[16];
    const bool tracked = getMarkerPose(binding.markerIndex, pose);
    float centerX = 0.0f, centerY = 0.0f;
    if (tracked) {
      const NFTMarkerData marker = getNFTTracker()->getMarkerData(binding.markerIndex);
      markerCenterMm(marker.width, marker.height, marker.dpi, centerX, centerY);
    }
    binding.anchor.update(tracked, pose, centerX, centerY, anchored);
    auto instance = tcm.getInstance(binding.entity);
    if (!instance.isValid()) continue;
    if (!binding.anchor.visible()) {
      tcm.setTransform(instance, filament::math::mat4f::scaling(0.0f));
      continue;
    }
    filament::math::mat4f transform;
    for (int c = 0; c < 4; ++c) {
      for (int r = 0; r < 4; ++r) transform[c][r] = anchored[c * 4 + r];
    }
    tcm.setTransform(instance, transform);
  }
}

utils::Entity ARBaseApp::createMeshEntity(const ARMesh &mesh) {
  filament::Engine *engine = getEngine();
  if (!engine || mesh.vertices.empty() || mesh.indices.empty()) return utils::Entity();

  // The unlit vertex-colour material of the demo cube, loaded once.
  if (!vertexColorMaterial) {
    std::ifstream is("/assets/ar_cube.fil", std::ios::binary);
    if (!is) {
      printf("ERROR: Failed to open /assets/ar_cube.fil for the primitive helpers\n");
      return utils::Entity();
    }
    std::vector<char> package((std::istreambuf_iterator<char>(is)),
                              std::istreambuf_iterator<char>());
    vertexColorMaterial =
        filament::Material::Builder().package(package.data(), package.size()).build(*engine);
    vertexColorInstance = vertexColorMaterial->createInstance();
  }

  using filament::VertexBuffer;
  using filament::IndexBuffer;
  const size_t vertexBytes = mesh.vertices.size() * sizeof(ARVertex);
  const size_t indexBytes = mesh.indices.size() * sizeof(uint16_t);
  // Filament uploads asynchronously: hand it copies that it frees when done.
  void *vertexData = std::malloc(vertexBytes);
  void *indexData = std::malloc(indexBytes);
  std::memcpy(vertexData, mesh.vertices.data(), vertexBytes);
  std::memcpy(indexData, mesh.indices.data(), indexBytes);
  auto release = [](void *buffer, size_t, void *) { std::free(buffer); };

  VertexBuffer *vb = VertexBuffer::Builder()
                         .vertexCount(static_cast<uint32_t>(mesh.vertices.size()))
                         .bufferCount(1)
                         .attribute(filament::VertexAttribute::POSITION, 0,
                                    VertexBuffer::AttributeType::FLOAT3,
                                    offsetof(ARVertex, position), sizeof(ARVertex))
                         .attribute(filament::VertexAttribute::COLOR, 0,
                                    VertexBuffer::AttributeType::FLOAT4,
                                    offsetof(ARVertex, color), sizeof(ARVertex))
                         .build(*engine);
  vb->setBufferAt(*engine, 0, VertexBuffer::BufferDescriptor(vertexData, vertexBytes, release));
  IndexBuffer *ib = IndexBuffer::Builder()
                        .indexCount(static_cast<uint32_t>(mesh.indices.size()))
                        .bufferType(IndexBuffer::IndexType::USHORT)
                        .build(*engine);
  ib->setBuffer(*engine, IndexBuffer::BufferDescriptor(indexData, indexBytes, release));

  filament::math::float3 lo = {1e30f, 1e30f, 1e30f}, hi = {-1e30f, -1e30f, -1e30f};
  for (const ARVertex &v : mesh.vertices) {
    for (int i = 0; i < 3; ++i) {
      lo[i] = std::min(lo[i], v.position[i]);
      hi[i] = std::max(hi[i], v.position[i]);
    }
  }
  utils::Entity entity = utils::EntityManager::get().create();
  filament::RenderableManager::Builder(1)
      .boundingBox({lo, hi})
      .material(0, vertexColorInstance)
      .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES, vb, ib)
      .culling(false)
      .receiveShadows(false)
      .castShadows(false)
      .build(*engine, entity);
  getScene()->addEntity(entity);
  ownedMeshes.push_back({entity, vb, ib});
  return entity;
}

utils::Entity ARBaseApp::createBox(float sizeX, float sizeY, float sizeZ,
                                   const float faceColors[6][4]) {
  return createMeshEntity(makeBox(sizeX, sizeY, sizeZ, faceColors));
}

utils::Entity ARBaseApp::createCube(float size, const float color[4]) {
  float faces[6][4];
  for (auto &face : faces) {
    for (int i = 0; i < 4; ++i) face[i] = color[i];
  }
  return createMeshEntity(makeBox(size, size, size, faces));
}

utils::Entity ARBaseApp::createPlane(float sizeX, float sizeY, const float color[4]) {
  return createMeshEntity(makePlane(sizeX, sizeY, color));
}

utils::Entity ARBaseApp::createSphere(float radius, const float color[4], int segments,
                                      int rings) {
  return createMeshEntity(makeSphere(radius, segments, rings, color));
}

ARBaseApp::~ARBaseApp() {
  filament::Engine *engine = getEngine();
  if (!engine) return;
  auto &tcm = engine->getTransformManager();
  for (OwnedMesh &mesh : ownedMeshes) {
    if (filament::Scene *scene = getScene()) scene->remove(mesh.entity);
    engine->destroy(mesh.entity); // the renderable component
    if (tcm.hasComponent(mesh.entity)) tcm.destroy(mesh.entity);
    utils::EntityManager::get().destroy(mesh.entity);
    engine->destroy(mesh.vertices);
    engine->destroy(mesh.indices);
  }
  if (vertexColorInstance) engine->destroy(vertexColorInstance);
  if (vertexColorMaterial) engine->destroy(vertexColorMaterial);
}
#else
#error "No rendering backend: build with WEBAR_RENDERER_FILAMENT (set by CMakeLists.txt)."
#endif
