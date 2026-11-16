#include "core/FilamentRenderer.h"
#include "core/ARVideoTexture.h"

#include <GLES3/gl3.h>
#include <backend/Platform.h>
#include <backend/platforms/PlatformWebGL.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/SwapChain.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#ifdef WEBGL
#undef WEBGL
#endif

using namespace filament;

namespace {

// Custom Platform to handle WebGL context switching correctly
class CustomWebGLPlatform : public filament::backend::PlatformWebGL {
public:
  bool makeCurrent(ContextType type, SwapChain *drawSwapChain,
                   SwapChain *readSwapChain) override {
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
        (EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)drawSwapChain;
    if (ctx > 0) {
      EMSCRIPTEN_RESULT res = emscripten_webgl_make_context_current(ctx);
      if (res != EMSCRIPTEN_RESULT_SUCCESS) {
        printf("ERROR: makeCurrent failed with code %d\n", res);
        return false;
      }
      return true;
    }
    return true;
  }
};

} // namespace

FilamentRenderer::~FilamentRenderer() {
  // Same teardown as before the renderer interface (#13): the engine releases what is left.
  if (!engine) {
    delete platform;
    return;
  }
  engine->destroy(renderer);
  engine->destroy(swapChain);
  engine->destroy(scene);
  engine->destroy(view);
  if (camera) engine->destroy(camera->getEntity());
  Engine::destroy(&engine);
  delete platform;
}

bool FilamentRenderer::init(const char *canvasSelector, int width, int height) {
  // --- Manual configuration of the WebGL context ---
  EmscriptenWebGLContextAttributes attr;
  emscripten_webgl_init_context_attributes(&attr);

  // Filament strictly requires WebGL 2.0
  attr.majorVersion = 2;
  attr.minorVersion = 0;
  attr.depth = true;
  attr.stencil = false;
  attr.antialias = false; // MSAA handled by Filament
  attr.alpha = false;
  attr.preserveDrawingBuffer = false;
  attr.failIfMajorPerformanceCaveat = false;

  emscripten_set_canvas_element_size(canvasSelector, width, height);

  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
      emscripten_webgl_create_context(canvasSelector, &attr);

  if (ctx <= 0) {
    printf("CRITICAL ERROR: Failed to create WebGL 2 context!\n");
    return false;
  }

  // Make current immediately
  if (emscripten_webgl_make_context_current(ctx) != EMSCRIPTEN_RESULT_SUCCESS) {
    printf("CRITICAL ERROR: Failed to make WebGL context current!\n");
    return false;
  }

  printf("WebGL 2 Context created and active. Initializing Filament...\n");

  // Initialize Filament Engine with Custom Platform
  platform = new CustomWebGLPlatform();
  filament::Engine::Config config = {};
  engine = filament::Engine::create(filament::Engine::Backend::OPENGL, platform,
                                    nullptr, &config);

  if (engine == nullptr) {
    printf("ERROR: Unable to create Filament Engine!\n");
    return false;
  }
  printf("Filament Engine created successfully!\n");

  // Pass context handle to createSwapChain
  swapChain = engine->createSwapChain((void *)ctx);

  renderer = engine->createRenderer();
  // Internal frame pacing for Web
  renderer->setDisplayInfo({.refreshRate = 60.0f});

  scene = engine->createScene();
  view = engine->createView();
  camera = engine->createCamera(utils::EntityManager::get().create());

  view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});
  view->setScene(scene);
  view->setCamera(camera);
  view->setPostProcessingEnabled(false); // Keep disabled for now to start safe

  filament::Renderer::ClearOptions clearOptions;
  clearOptions.clearColor = {0.0f, 0.0f, 0.0f, 1.0f}; // Black
  clearOptions.clear = true;
  renderer->setClearOptions(clearOptions);

  createVideoBackground(width, height);
  return true;
}

void FilamentRenderer::setProjection(const double projection[16], double nearPlane,
                                     double farPlane) {
  math::mat4 customProj;
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      customProj[c][r] = projection[c * 4 + r];
    }
  }
  camera->setCustomProjection(customProj, nearPlane, farPlane);
  camera->lookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
}

void FilamentRenderer::createVideoBackground(int width, int height) {
  // Load the compiled video material (.fil)
  std::ifstream is("/assets/video_background.fil", std::ios::binary);
  if (!is) {
    printf("ERROR: Failed to open material file!\n");
    return;
  }
  std::vector<char> buffer((std::istreambuf_iterator<char>(is)),
                           std::istreambuf_iterator<char>());
  videoMaterial = Material::Builder()
                      .package(buffer.data(), buffer.size())
                      .build(*engine);
  videoMaterialInst = videoMaterial->createInstance();

  // The WebGL texture the camera video is copied into (ARVideoTexture, shared with sokol)
  nativeTextureId = createVideoTexture(width, height);

  // Create External Texture importing the native WebGL texture
  videoTexture = Texture::Builder()
                     .width(width)
                     .height(height)
                     .sampler(Texture::Sampler::SAMPLER_2D)
                     .format(Texture::InternalFormat::RGB8)
                     .import(nativeTextureId)
                     .build(*engine);

  videoMaterialInst->setParameter("videoTexture", videoTexture, TextureSampler());

  // The video background: a full-screen quad in device space
  struct Vertex {
    math::float4 position;
    math::float2 uv;
  };
  static const Vertex VERTICES[4] = {
      {{-1.0f, -1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
      {{ 1.0f, -1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
      {{-1.0f,  1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
      {{ 1.0f,  1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}};
  static const uint16_t INDICES[6] = {0, 1, 2,
                                      2, 1, 3};

  videoQuadVb =
      VertexBuffer::Builder()
          .vertexCount(4)
          .bufferCount(1)
          .attribute(VertexAttribute::POSITION, 0,
                     VertexBuffer::AttributeType::FLOAT4, 0, sizeof(Vertex))
          .attribute(VertexAttribute::UV0, 0,
                     VertexBuffer::AttributeType::FLOAT2, sizeof(math::float4),
                     sizeof(Vertex))
          .build(*engine);
  videoQuadVb->setBufferAt(
      *engine, 0, VertexBuffer::BufferDescriptor(VERTICES, sizeof(VERTICES)));

  videoQuadIb = IndexBuffer::Builder()
                    .indexCount(6)
                    .bufferType(IndexBuffer::IndexType::USHORT)
                    .build(*engine);
  videoQuadIb->setBuffer(*engine,
                         IndexBuffer::BufferDescriptor(INDICES, sizeof(INDICES)));
  videoQuad = utils::EntityManager::get().create();
  RenderableManager::Builder(1)
      .boundingBox({{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}})
      .material(0, videoMaterialInst)
      .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, videoQuadVb,
                videoQuadIb)
      .culling(false)
      .priority(0)
      .channel(0)
      .receiveShadows(false)
      .castShadows(false)
      .build(*engine, videoQuad);

  scene->addEntity(videoQuad);
}

void FilamentRenderer::updateVideoTexture() {
  // Retrieve the internal WebGL texture ID and update it via JS
  if (videoTexture) {
    uploadVideoFrame(nativeTextureId);
  }
}

void FilamentRenderer::render() {
  if (renderer->beginFrame(swapChain)) {
    renderer->render(view);
    renderer->endFrame();

    static int frameCount = 0;
    if (frameCount++ % 120 == 0)
      printf("Rendered frame %d (beginFrame=true)\n", frameCount);
  } else {
    static int skipCount = 0;
    if (skipCount++ % 120 == 0) {
      printf("Skipped frame %d (beginFrame=false)\n", skipCount);
      GLenum err = glGetError();
      if (err != GL_NO_ERROR)
        printf("GL Error during skip: 0x%x\n", err);
    }
  }
  engine->execute();
}
