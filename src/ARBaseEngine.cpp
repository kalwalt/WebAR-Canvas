#include "ARBaseEngine.h"
#include "nft/ARNFTTracker.h"
#include <GLES3/gl3.h>
#include <backend/Platform.h>
#include <backend/platforms/PlatformWebGL.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/SwapChain.h>
#include <filament/TextureSampler.h>
#include <filament/VertexBuffer.h>
#include <fstream>

#ifdef WEBGL
#undef WEBGL
#endif

// Helper JavaScript to update the WebGL texture from the <video> tag
EM_JS(void, update_webgl_texture, (int textureId), {
  const video = document.getElementById('ar-video');

  // Try to get the GL context from multiple sources
  var gl = (GL.currentContext && GL.currentContext.GLctx) || Module.ctx;
  if (!gl) {
    const canvas = document.getElementById('canvas');
    gl = canvas.getContext('webgl2') || canvas.getContext('webgl');
  }

  if (!gl) {
    console.error(
        "Critical: Could not find WebGL context in update_webgl_texture");
    return;
  }

  if (video && video.readyState >= video.HAVE_CURRENT_DATA) {
    const glTexture = GL.textures[textureId];
    if (!glTexture) {
      console.error("Critical: GL.textures[" + textureId +
                    "] is undefined. Native texture handle not found in " +
                    "Emscripten GL object.");
      return;
    }

    gl.bindTexture(gl.TEXTURE_2D, glTexture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGB, gl.RGB, gl.UNSIGNED_BYTE, video);
    // Optional: verify error
    const err = gl.getError();
    if (err != 0) {
      console.error("WebGL Error in update_webgl_texture:", err);
    }
  } else {
    // console.warn("Video not ready yet");
  }
});

ARBaseEngine::ARBaseEngine() : engine(nullptr), renderer(nullptr) {}

ARBaseEngine::~ARBaseEngine() {
  if (nftTracker) {
    nftTracker->teardown();
  }
  engine->destroy(renderer);
  engine->destroy(swapChain);
  engine->destroy(scene);
  engine->destroy(view);
  engine->destroy(camera->getEntity());
  Engine::destroy(&engine);
  delete platform;
}

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

bool ARBaseEngine::init(const char *canvasId, int width, int height,
                        const std::string &markerPath) {
  // --- STEP 1: Manual configuration of the WebGL context ---
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

  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
      emscripten_webgl_create_context("#canvas", &attr);

  if (ctx <= 0) {
    printf("CRITICAL ERROR: Failed to create WebGL 2 context!\n");
    return false;
  }
  glContext = ctx; // Store for backup

  // Make current immediately
  if (emscripten_webgl_make_context_current(ctx) != EMSCRIPTEN_RESULT_SUCCESS) {
    printf("CRITICAL ERROR: Failed to make WebGL context current!\n");
    return false;
  }

  printf("WebGL 2 Context created and active. Initializing Filament...\n");

  // 1. Initialize Filament Engine with Custom Platform
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

  // --- STEP 2: Initialize NFT Subsystem (Setup & setCamera) ---
  printf("Initializing ARNFTTracker...\n");
  nftTracker = std::make_unique<ARNFTTracker>();
  int cameraID = nftTracker->loadCamera("/assets/camera_para.dat");
  if (cameraID < 0) {
    printf("ERROR: Failed to load camera parameter file /assets/camera_para.dat\n");
    return false;
  }

  if (nftTracker->setup(width, height, cameraID) < 0) {
    printf("ERROR: Failed to setup ARNFTTracker with cameraID %d\n", cameraID);
    return false;
  }

  if (nftTracker->setupAR2() < 0) {
    printf("ERROR: Failed to setup AR2 / KPM in ARNFTTracker\n");
    return false;
  }

  // --- STEP 3: Load NFT Marker Dataset ---
  if (!markerPath.empty()) {
    printf("Loading NFT marker from: %s\n", markerPath.c_str());
    int markerId = nftTracker->loadMarker(markerPath);
    if (markerId < 0) {
      printf("ERROR: Failed to load NFT marker from: %s\n", markerPath.c_str());
      return false;
    }
    const auto &markerData = nftTracker->getMarkerData(markerId);
    printf("NFT Marker [%d] loaded successfully: %dx%d @ %d DPI\n",
           markerId, markerData.width, markerData.height, markerData.dpi);
  }

  // Retrieve calculated OpenGL camera frustum projection matrix
  const ARdouble *lens = nftTracker->getCameraLens();
  for (int i = 0; i < 16; ++i) {
    projectionMatrix[i] = static_cast<float>(lens[i]);
  }

  printf("NFT Camera Frustum Projection Matrix (Column-Major):\n");
  for (int r = 0; r < 4; ++r) {
    printf("  [%9.4f %9.4f %9.4f %9.4f]\n",
           projectionMatrix[0 * 4 + r],
           projectionMatrix[1 * 4 + r],
           projectionMatrix[2 * 4 + r],
           projectionMatrix[3 * 4 + r]);
  }

  math::mat4 customProj;
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      customProj[c][r] = lens[c * 4 + r];
    }
  }
  camera->setCustomProjection(customProj, nftTracker->getNearPlane(),
                             nftTracker->getFarPlane());
  camera->lookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});

  filament::Renderer::ClearOptions clearOptions;
  clearOptions.clearColor = {0.0f, 0.0f, 1.0f, 1.0f}; // Blue
  clearOptions.clear = true;
  renderer->setClearOptions(clearOptions);

  // 3. Load the compiled video material (.fil)
  std::ifstream is("/assets/video_background.fil", std::ios::binary);
  if (is) {
    std::vector<char> buffer((std::istreambuf_iterator<char>(is)),
                             std::istreambuf_iterator<char>());
    videoMaterial = Material::Builder()
                        .package(buffer.data(), buffer.size())
                        .build(*engine);
    videoMaterialInst = videoMaterial->createInstance();

    // Create explicit WebGL texture
    glGenTextures(1, &nativeTextureId);
    glBindTexture(GL_TEXTURE_2D, nativeTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, nullptr);

    // Create External Texture importing the native WebGL texture
    videoTexture = Texture::Builder()
                       .width(width)
                       .height(height)
                       .sampler(Texture::Sampler::SAMPLER_2D)
                       .format(Texture::InternalFormat::RGB8)
                       .import(nativeTextureId)
                       .build(*engine);

    videoMaterialInst->setParameter("videoTexture", videoTexture,
                                    TextureSampler());

    // 2. Create the Video Background (Quad + Texture)
    createSkyboxQuad(); // Restored

  } else {
    printf("ERROR: Failed to open material file!\n");
  }

  // 4. Developer hook
  this->setup(engine, scene);

  return true;
}

void ARBaseEngine::createSkyboxQuad() {
  struct Vertex {
    math::float2 position;
    math::float2 uv;
  };
  static const Vertex VERTICES[4] = {
      {{-1, -1}, {0, 0}},
      {{1, -1}, {1, 0}},
      {{-1, 1}, {0, 1}},
      {{1, 1}, {1, 1}}}; // Corrected to standard quad
  static const uint16_t INDICES[6] = {0, 1, 2,
                                      2, 1, 3}; // Standard quad indices

  skyboxVb =
      VertexBuffer::Builder()
          .vertexCount(4)
          .bufferCount(1)
          .attribute(VertexAttribute::POSITION, 0,
                     VertexBuffer::AttributeType::FLOAT2, 0, sizeof(Vertex))
          .attribute(VertexAttribute::UV0, 0,
                     VertexBuffer::AttributeType::FLOAT2, sizeof(math::float2),
                     sizeof(Vertex))
          .build(*engine);
  skyboxVb->setBufferAt(
      *engine, 0, VertexBuffer::BufferDescriptor(VERTICES, sizeof(VERTICES)));

  skyboxIb = IndexBuffer::Builder()
                 .indexCount(6)
                 .bufferType(IndexBuffer::IndexType::USHORT)
                 .build(*engine);
  skyboxIb->setBuffer(*engine,
                      IndexBuffer::BufferDescriptor(INDICES, sizeof(INDICES)));
  skyboxQuad = utils::EntityManager::get().create();
  RenderableManager::Builder(1)
      .boundingBox({{0, 0, 0}, {1, 1, 1}})
      .material(0, videoMaterialInst)
      .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, skyboxVb,
                skyboxIb)
      .culling(false) // Essential for the background
      .build(*engine, skyboxQuad);

  scene->addEntity(skyboxQuad);
}

void ARBaseEngine::updateVideoTexture() {
  // Retrieve the internal WebGL texture ID and update it via JS
  if (videoTexture) {
    update_webgl_texture(nativeTextureId); // Pass the real ID
  }
}

void ARBaseEngine::render() {
  if (renderer->beginFrame(swapChain)) {
    renderer->render(view);
    renderer->endFrame();

    // engine->execute();

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

void ARBaseEngine::tick(void *arg) {
  auto *app = static_cast<ARBaseEngine *>(arg);
  app->updateVideoTexture(); // Re-enable video update
  app->update(0.016f);
  app->render();
}

void ARBaseEngine::run() {
  emscripten_set_main_loop_arg(ARBaseEngine::tick, this, 0, 1);
}
