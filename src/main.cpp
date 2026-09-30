#include "ARBaseEngine.h"
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/Material.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <utils/EntityManager.h>
#include <fstream>
#include <vector>
#include <iostream>

// 1. Define the concrete implementation of your AR App
class WebARApp : public ARBaseEngine {
public:
    ~WebARApp() override {
        if (engine) {
            auto &tcm = engine->getTransformManager();
            if (tcm.hasComponent(cubeEntity)) {
                tcm.destroy(cubeEntity);
            }
            if (engine->getRenderableManager().hasComponent(cubeEntity)) {
                engine->destroy(cubeEntity);
            }
            utils::EntityManager::get().destroy(cubeEntity);
            if (cubeMaterialInst) engine->destroy(cubeMaterialInst);
            if (cubeMaterial) engine->destroy(cubeMaterial);
            if (cubeVb) engine->destroy(cubeVb);
            if (cubeIb) engine->destroy(cubeIb);
        }
    }

    // Asset loading: create a colorful 3D Cube sitting on the marker
    void setup(Engine* engine, Scene* scene) override {
        std::cout << "Setup Filament Scene with 3D AR Cube..." << std::endl;

        // Load compiled baked color material (.fil)
        std::ifstream is("/assets/ar_cube.fil", std::ios::binary);
        if (is) {
            std::vector<char> buffer((std::istreambuf_iterator<char>(is)),
                                     std::istreambuf_iterator<char>());
            cubeMaterial = Material::Builder()
                               .package(buffer.data(), buffer.size())
                               .build(*engine);
            cubeMaterialInst = cubeMaterial->createInstance();
            printf("AR Cube material loaded successfully!\n");
        } else {
            printf("ERROR: Failed to open /assets/ar_cube.fil!\n");
            return;
        }

        struct CubeVertex {
            math::float3 position;
            math::float4 color;
        };

        const float S = 80.0f;       // 80mm cube height
        const float H = S * 0.5f;   // 40mm half-extent in X and Y

        // Colors for each face
        const math::float4 RED     = {1.0f, 0.2f, 0.2f, 1.0f}; // Front (+Z)
        const math::float4 CYAN    = {0.2f, 1.0f, 1.0f, 1.0f}; // Back (-Z)
        const math::float4 YELLOW  = {1.0f, 1.0f, 0.2f, 1.0f}; // Left (-X)
        const math::float4 BLUE    = {0.2f, 0.4f, 1.0f, 1.0f}; // Right (+X)
        const math::float4 MAGENTA = {1.0f, 0.2f, 1.0f, 1.0f}; // Bottom (-Y)
        const math::float4 GREEN   = {0.2f, 1.0f, 0.2f, 1.0f}; // Top (+Y)

        // 24 vertices (4 per face)
        static const CubeVertex CUBE_VERTICES[24] = {
            // Front face (+Z)
            {{-H, -H, S}, RED},
            {{ H, -H, S}, RED},
            {{ H,  H, S}, RED},
            {{-H,  H, S}, RED},
            // Back face (-Z, marker surface Z=0)
            {{-H, -H, 0.0f}, CYAN},
            {{-H,  H, 0.0f}, CYAN},
            {{ H,  H, 0.0f}, CYAN},
            {{ H, -H, 0.0f}, CYAN},
            // Left face (-X)
            {{-H, -H, 0.0f}, YELLOW},
            {{-H, -H, S},    YELLOW},
            {{-H,  H, S},    YELLOW},
            {{-H,  H, 0.0f}, YELLOW},
            // Right face (+X)
            {{ H, -H, 0.0f}, BLUE},
            {{ H,  H, 0.0f}, BLUE},
            {{ H,  H, S},    BLUE},
            {{ H, -H, S},    BLUE},
            // Bottom face (-Y)
            {{-H, -H, 0.0f}, MAGENTA},
            {{ H, -H, 0.0f}, MAGENTA},
            {{ H, -H, S},    MAGENTA},
            {{-H, -H, S},    MAGENTA},
            // Top face (+Y)
            {{-H,  H, 0.0f}, GREEN},
            {{-H,  H, S},    GREEN},
            {{ H,  H, S},    GREEN},
            {{ H,  H, 0.0f}, GREEN},
        };

        // 36 indices (6 indices per face = 2 triangles)
        static const uint16_t CUBE_INDICES[36] = {
             0,  1,  2,   0,  2,  3,  // Front
             4,  5,  6,   4,  6,  7,  // Back
             8,  9, 10,   8, 10, 11,  // Left
            12, 13, 14,  12, 14, 15,  // Right
            16, 17, 18,  16, 18, 19,  // Bottom
            20, 21, 22,  20, 22, 23   // Top
        };

        cubeVb = VertexBuffer::Builder()
                     .vertexCount(24)
                     .bufferCount(1)
                     .attribute(VertexAttribute::POSITION, 0,
                                VertexBuffer::AttributeType::FLOAT3,
                                0, sizeof(CubeVertex))
                     .attribute(VertexAttribute::COLOR, 0,
                                VertexBuffer::AttributeType::FLOAT4,
                                sizeof(math::float3), sizeof(CubeVertex))
                     .build(*engine);
        cubeVb->setBufferAt(*engine, 0,
                            VertexBuffer::BufferDescriptor(CUBE_VERTICES, sizeof(CUBE_VERTICES)));

        cubeIb = IndexBuffer::Builder()
                     .indexCount(36)
                     .bufferType(IndexBuffer::IndexType::USHORT)
                     .build(*engine);
        cubeIb->setBuffer(*engine,
                          IndexBuffer::BufferDescriptor(CUBE_INDICES, sizeof(CUBE_INDICES)));

        cubeEntity = utils::EntityManager::get().create();
        RenderableManager::Builder(1)
            .boundingBox({{-H, -H, 0.0f}, {H, H, S}})
            .material(0, cubeMaterialInst)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, cubeVb, cubeIb)
            .culling(false)
            .receiveShadows(false)
            .castShadows(false)
            .build(*engine, cubeEntity);

        scene->addEntity(cubeEntity);

        // Initialize transform component (hidden initially until marker is acquired)
        auto &tcm = engine->getTransformManager();
        tcm.create(cubeEntity);
        auto inst = tcm.getInstance(cubeEntity);
        tcm.setTransform(inst, math::mat4f::scaling(math::float3{0.0f}));

        printf("AR 3D Cube added to scene successfully!\n");
    }

    // Update logic: attach the 3D Cube to the marker pose
    void update(float deltaTime) override {
        auto &tcm = engine->getTransformManager();
        auto inst = tcm.getInstance(cubeEntity);
        if (!inst.isValid()) return;

        if (isTracking) {
            // Convert column-major modelViewMatrix to Filament mat4f
            math::mat4f filamentPose;
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    filamentPose[c][r] = modelViewMatrix[c * 4 + r];
                }
            }

            // Pinball marker is 893x1117 @ 120 DPI (~189.02mm x 236.43mm)
            // Center is (94.51mm, 118.22mm)
            const float markerCenterX = (893.0f / 120.0f) * 25.4f * 0.5f;
            const float markerCenterY = (1117.0f / 120.0f) * 25.4f * 0.5f;

            // Slowly spin the cube around its Z axis for dynamic AR feedback
            static float spinAngle = 0.0f;
            spinAngle += deltaTime * 1.5f;

            math::mat4f centerTrans = math::mat4f::translation(math::float3{markerCenterX, markerCenterY, 0.0f});
            math::mat4f spinRot = math::mat4f::rotation(spinAngle, math::float3{0.0f, 0.0f, 1.0f});
            math::mat4f localTransform = centerTrans * spinRot;

            math::mat4f finalTransform = filamentPose * localTransform;
            tcm.setTransform(inst, finalTransform);

            static int trackLogCount = 0;
            if (trackLogCount++ % 60 == 0) {
                printf("[WebARApp] Tracking active! Pose Translation: [%.2f, %.2f, %.2f]\n",
                       modelViewMatrix[12], modelViewMatrix[13], modelViewMatrix[14]);
            }
        } else {
            // Hide the cube when tracking is not active
            tcm.setTransform(inst, math::mat4f::scaling(math::float3{0.0f}));
        }
    }

private:
    utils::Entity cubeEntity;
    VertexBuffer* cubeVb = nullptr;
    IndexBuffer* cubeIb = nullptr;
    Material* cubeMaterial = nullptr;
    MaterialInstance* cubeMaterialInst = nullptr;
};

// 2. Entry point of the WASM application
int main(int argc, char** argv) {
    int width = 640;
    int height = 480;

    std::string markerPath = "/assets/markers/pinball";

    static WebARApp app;

    std::cout << "Initializing WebAR-Canvas with NFT..." << std::endl;

    if (!app.init("canvas", width, height, markerPath)) {
        std::cerr << "Error during ARBaseEngine initialization" << std::endl;
        return 1;
    }

    app.run();

    return 0;
}