#ifndef WEBAR_APP_H
#define WEBAR_APP_H

#include "core/ARBaseApp.h"

// The demo app: a colourful 3D cube on the pinball marker.
class App : public ARBaseApp {
public:
    ~App() override;

    // Asset loading: create a colorful 3D Cube sitting on the marker
    void setup() override;

    // Update logic: spin the cube (the anchor keeps it on the marker)
    void update(float deltaTime) override;

    void onMarkerFound(int markerIndex) override;
    void onMarkerLost(int markerIndex) override;

    void touchDown(float x, float y, int pointerId) override;
    void mouseDown(float x, float y, int button, int pointerId) override;
    void windowResized(int width, int height) override;

private:
    void toggleModel(); // a tap or click switches between the cube and the glTF model

#ifdef WEBAR_RENDERER_FILAMENT
    utils::Entity anchorEntity; // bound to marker 0, centred on it
    utils::Entity cubeEntity;   // child of anchorEntity, spinning (owned by ARBaseApp)
#endif
    int modelHandle = -1;       // AnimatedMorphCube.glb on marker 0 (loadModel), or an error
    bool showModel = false;
};

#endif // WEBAR_APP_H
