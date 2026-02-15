#include <filament/TextureSampler.h>
#include <filament/RenderableManager.h>
#include <filament/IndexBuffer.h>
#include <filament/VertexBuffer.h>
#include <filament/SwapChain.h>
#include "ARBaseEngine.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <fstream>
#include <iostream>

#ifdef WEBGL
#undef WEBGL
#endif

// Helper JavaScript to update the WebGL texture from the <video> tag
EM_JS(void, update_webgl_texture, (int textureId), {
    const video = document.getElementById('ar-video'); // Assume it exists in the DOM
    const gl = Module.ctx; // The context created by Emscripten/Filament
    if (video && video.readyState >= video.HAVE_CURRENT_DATA) {
        gl.bindTexture(gl.TEXTURE_2D, GL.textures[textureId]);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGB, gl.RGB, gl.UNSIGNED_BYTE, video);
    }
});

ARBaseEngine::ARBaseEngine() : engine(nullptr), renderer(nullptr) {}

ARBaseEngine::~ARBaseEngine() {
    engine->destroy(renderer);
    engine->destroy(swapChain);
    engine->destroy(scene);
    engine->destroy(view);
    Engine::destroy(&engine);
}

bool ARBaseEngine::init(const char* canvasId, int width, int height, const std::string& markerPath) {
    // 1. Initialize Filament Engine
    filament::Engine::Config config = {};
    engine = filament::Engine::create(filament::Engine::Backend::DEFAULT, nullptr, nullptr, &config);
    if (engine == nullptr) {
        printf("ERROR: Unable to create Filament Engine!\n");
        return false;
    }
    swapChain = engine->createSwapChain((void*)canvasId);
    renderer = engine->createRenderer();
    scene = engine->createScene();
    view = engine->createView();
    camera = engine->createCamera(utils::EntityManager::get().create());

    view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});
    view->setScene(scene);
    view->setCamera(camera);

    // 2. Create the Video Background (Quad + Texture)
    createSkyboxQuad();

    // 3. Load the compiled video material (.fil)
    std::ifstream is("/assets/video_background.fil", std::ios::binary);
    if (is) {
        std::vector<char> buffer((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
        videoMaterial = Material::Builder().package(buffer.data(), buffer.size()).build(*engine);
        videoMaterialInst = videoMaterial->createInstance();
        
        // Create External Texture
        videoTexture = Texture::Builder()
            .width(width).height(height)
            .sampler(Texture::Sampler::SAMPLER_2D) // WebGL often uses SAMPLER_2D for video
            .format(Texture::InternalFormat::RGB8)
            .build(*engine);

        videoMaterialInst->setParameter("videoTexture", videoTexture, TextureSampler());
        
        // Assign the material to the background quad
        auto& tcm = engine->getRenderableManager();
        tcm.setMaterialInstanceAt(tcm.getInstance(skyboxQuad), 0, videoMaterialInst);
    }

    // 4. Developer hook
    this->setup(engine, scene);

    return true;
}

void ARBaseEngine::createSkyboxQuad() {
    struct Vertex { math::float2 position; math::float2 uv; };
    static const Vertex VERTICES[4] = {
        {{-1, -1}, {0, 0}}, {{ 1, -1}, {1, 0}},
        {{-1,  1}, {0, 1}}, {{ 1,  1}, {1, 1}}
    };
    static const uint16_t INDICES[6] = { 0, 1, 2, 2, 1, 3 };

    skyboxVb = VertexBuffer::Builder()
        .vertexCount(4)
        .bufferCount(1)
        .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT2, 0, sizeof(Vertex))
        .attribute(VertexAttribute::UV0, 0, VertexBuffer::AttributeType::FLOAT2, sizeof(math::float2), sizeof(Vertex))
        .build(*engine);
    skyboxVb->setBufferAt(*engine, 0, VertexBuffer::BufferDescriptor(VERTICES, sizeof(VERTICES)));

    skyboxIb = IndexBuffer::Builder().indexCount(6).bufferType(IndexBuffer::IndexType::USHORT).build(*engine);
    skyboxIb->setBuffer(*engine, IndexBuffer::BufferDescriptor(INDICES, sizeof(INDICES)));
    skyboxQuad = utils::EntityManager::get().create();
    RenderableManager::Builder(1)
        .boundingBox({ {0,0,0}, {1,1,1} })
        .material(0, videoMaterialInst)
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, skyboxVb, skyboxIb)
        .culling(false) // Essential for the background
        .build(*engine, skyboxQuad);
    
    scene->addEntity(skyboxQuad);
}

void ARBaseEngine::updateVideoTexture() {
    // Retrieve the internal WebGL texture ID and update it via JS
    if (videoTexture) {
        // Note: this requires access to Filament backend APIs
        // For simplicity, we use the ID managed by Emscripten
        update_webgl_texture(0); // Simplification: map the real ID
    }
}

void ARBaseEngine::render() {
    if (renderer->beginFrame(swapChain)) {
        renderer->render(view);
        renderer->endFrame();
    }
}

void ARBaseEngine::tick(void* arg) {
    auto* app = static_cast<ARBaseEngine*>(arg);
    app->updateVideoTexture();
    app->update(0.016f);
    app->render();
}

void ARBaseEngine::run() {
    emscripten_set_main_loop_arg(ARBaseEngine::tick, this, 0, 1);
}