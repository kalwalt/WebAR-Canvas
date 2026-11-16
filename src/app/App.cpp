#include "app/App.h"
#include <filament/Engine.h>
#include <filament/TransformManager.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>
#include <cstdio>
#include <iostream>

using namespace filament;

App::~App() {
    // The cube is released by ARBaseApp (createBox); the anchor entity is the app's.
    Engine *engine = getEngine();
    if (engine) {
        auto &tcm = engine->getTransformManager();
        detachFromMarker(anchorEntity);
        if (tcm.hasComponent(anchorEntity)) {
            tcm.destroy(anchorEntity);
        }
        utils::EntityManager::get().destroy(anchorEntity);
    }
}

void App::setup() {
    std::cout << "Setup Filament Scene with 3D AR Cube..." << std::endl;

    // An 80 mm cube standing on the marker, one colour per face.
    const float faceColors[6][4] = {
        {1.0f, 0.2f, 0.2f, 1.0f}, // +Z (top): red
        {0.2f, 1.0f, 1.0f, 1.0f}, // -Z (base): cyan
        {1.0f, 1.0f, 0.2f, 1.0f}, // -X: yellow
        {0.2f, 0.4f, 1.0f, 1.0f}, // +X: blue
        {1.0f, 0.2f, 1.0f, 1.0f}, // -Y: magenta
        {0.2f, 1.0f, 0.2f, 1.0f}, // +Y: green
    };
    cubeEntity = createBox(80.0f, 80.0f, 80.0f, faceColors);
    if (cubeEntity.isNull()) {
        printf("ERROR: the AR cube could not be created\n");
        return;
    }

    // The cube spins under an anchor bound to marker 0 (centred on it, hidden while lost).
    // create() only if missing, then setParent(): see the parenting note in AGENTS.md.
    auto &tcm = getEngine()->getTransformManager();
    anchorEntity = utils::EntityManager::get().create();
    attachToMarker(anchorEntity, 0);
    if (!tcm.hasComponent(cubeEntity)) tcm.create(cubeEntity);
    tcm.setParent(tcm.getInstance(cubeEntity), tcm.getInstance(anchorEntity));

    printf("AR 3D Cube added to scene successfully!\n");

    // A glTF model on the same marker, 80 mm, hidden until a tap or click (see toggleModel).
    modelHandle = loadModel("/assets/models/AnimatedMorphCube.glb");
    if (modelHandle >= 0) {
        placeModelOnMarker(modelHandle, 0, 80.0f);
        setModelVisible(modelHandle, false);
    } else {
        printf("No glTF model (loadModel returned %d)\n", modelHandle);
    }
}

void App::toggleModel() {
    if (modelHandle < 0) return;
    showModel = !showModel;
    setModelVisible(modelHandle, showModel);
    printf("showing the %s\n", showModel ? "glTF model" : "cube");
}

void App::update(float deltaTime) {
    auto &tcm = getEngine()->getTransformManager();
    auto inst = tcm.getInstance(cubeEntity);
    if (!inst.isValid() || !isTracking) return;

    // Slowly spin the cube around its Z axis for dynamic AR feedback (the anchor places it);
    // scale it to 0 while the glTF model is shown instead.
    static float spinAngle = 0.0f;
    spinAngle += deltaTime * 1.5f;
    tcm.setTransform(inst, showModel ? math::mat4f::scaling(0.0f)
                                     : math::mat4f::rotation(spinAngle, math::float3{0.0f, 0.0f, 1.0f}));

    static int trackLogCount = 0;
    if (trackLogCount++ % 60 == 0) {
        printf("[App] Tracking active! Pose Translation: [%.2f, %.2f, %.2f]\n",
               modelViewMatrix[12], modelViewMatrix[13], modelViewMatrix[14]);
    }
}

void App::onMarkerFound(int markerIndex) {
    printf(">>> NFT Marker %d found <<<\n", markerIndex);
}

void App::onMarkerLost(int markerIndex) {
    printf(">>> NFT Marker %d lost <<<\n", markerIndex);
}

void App::touchDown(float x, float y, int pointerId) {
    printf("tap at (%.1f, %.1f) pointer %d\n", x, y, pointerId);
    toggleModel();
}

void App::mouseDown(float x, float y, int button, int pointerId) {
    printf("click at (%.1f, %.1f) button %d pointer %d\n", x, y, button, pointerId);
    toggleModel();
}

void App::windowResized(int width, int height) {
    printf("window resized to %dx%d\n", width, height);
}
