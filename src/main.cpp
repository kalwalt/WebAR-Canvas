#include "ARBaseEngine.h"
#include <iostream>

// 1. Define the concrete implementation of your AR App
class WebARApp : public ARBaseEngine {
public:
    // Asset loading (3D models, lights, etc.)
    void setup(Engine* engine, Scene* scene) override {
        std::cout << "Setup Filament Scene..." << std::endl;
        
        // Here the developer can add entities to the scene
        // Example: load a .glb file or add a directional light
        
        /* auto light = utils::EntityManager::get().create();
        LightManager::Builder(LightManager::Type::DIRECTIONAL)
            .color(Color::toLinear<ACCURATE>(sRGBColor(0.98f, 0.92f, 0.89f)))
            .intensity(110000.0f)
            .direction({ 0.7f, -1.0f, -0.8f })
            .castShadows(true)
            .build(*engine, light);
        scene->addEntity(light);
        */
    }

    // Update logic (called every frame)
    void update(float deltaTime) override {
        if (isTracking) {
            static int trackLogCount = 0;
            if (trackLogCount++ % 60 == 0) {
                printf("[WebARApp] Tracking active! Pose Translation: [%.2f, %.2f, %.2f]\n",
                       modelViewMatrix[12], modelViewMatrix[13], modelViewMatrix[14]);
            }
        }
    }
};

// 2. Entry point of the WASM application
int main(int argc, char** argv) {
    // Canvas size (can be retrieved dynamically via JS)
    int width = 640;
    int height = 480;
    
    // Path to the marker embedded via CMake
    std::string markerPath = "/assets/markers/pinball";

    // Instantiate the app
    static WebARApp app;

    std::cout << "Initializing WebAR-Canvas with NFT..." << std::endl;

    // Initialize the engine (Canvas ID, Resolution, Marker)
    if (!app.init("canvas", width, height, markerPath)) {
        std::cerr << "Error during ARBaseEngine initialization" << std::endl;
        return 1;
    }

    // Start the Emscripten main loop
    app.run();

    return 0;
}