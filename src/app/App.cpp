#include "app/App.h"

#include <cmath>
#include <cstdio>
#include <iostream>

void App::setup() {
    std::cout << "Setup the AR scene with a 3D cube..." << std::endl;

    // An 80 mm cube standing on the marker, one colour per face.
    const float faceColors[6][4] = {
        {1.0f, 0.2f, 0.2f, 1.0f}, // +Z (top): red
        {0.2f, 1.0f, 1.0f, 1.0f}, // -Z (base): cyan
        {1.0f, 1.0f, 0.2f, 1.0f}, // -X: yellow
        {0.2f, 0.4f, 1.0f, 1.0f}, // +X: blue
        {1.0f, 0.2f, 1.0f, 1.0f}, // -Y: magenta
        {0.2f, 1.0f, 0.2f, 1.0f}, // +Y: green
    };
    cube = addBox(80.0f, 80.0f, 80.0f, faceColors);
    if (cube < 0) {
        printf("ERROR: the AR cube could not be created\n");
        return;
    }
    // Centred on marker 0, hidden while it is lost; update() spins it about the marker's z.
    attachMeshToMarker(cube, 0);
    printf("AR 3D Cube added to scene successfully!\n");

    // A glTF model on the same marker, 80 mm, hidden until a tap or click (see toggleModel).
    // Filament only: with sokol, loadModel() returns AR_MODEL_UNSUPPORTED (-3).
    modelHandle = loadModel("/assets/models/AnimatedMorphCube.glb");
#ifdef WEBAR_RENDERER_FILAMENT
    if (modelHandle >= 0) {
        placeModelOnMarker(modelHandle, 0, 80.0f);
        setModelVisible(modelHandle, false);
    }
#endif
    if (modelHandle < 0) printf("No glTF model (loadModel returned %d)\n", modelHandle);
}

void App::toggleModel() {
    if (modelHandle < 0) return;
    showModel = !showModel;
#ifdef WEBAR_RENDERER_FILAMENT
    setModelVisible(modelHandle, showModel);
#endif
    setMeshVisible(cube, !showModel);
    printf("showing the %s\n", showModel ? "glTF model" : "cube");
}

void App::update(float deltaTime) {
    if (cube < 0 || !isTracking) return;

    // Slowly spin the cube around the marker's z axis for dynamic AR feedback (the anchor
    // places it): a local transform, applied after the marker's pose.
    spinAngle += deltaTime * 1.5f;
    const float c = std::cos(spinAngle), s = std::sin(spinAngle);
    const float spin[16] = {c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    setMeshLocalTransform(cube, spin);

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
