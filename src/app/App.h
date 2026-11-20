#ifndef WEBAR_APP_H
#define WEBAR_APP_H

#include "core/ARBaseApp.h"

// The demo app: a colourful 3D cube on the pinball marker, the same source on every renderer
// (the backend-neutral mesh API); with Filament and glTF, a tap or click switches to a model.
class App : public ARBaseApp {
public:
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

    int cube = -1;        // mesh id, bound to marker 0 and centred on it (ARBaseApp releases it)
    float spinAngle = 0.0f;
    int modelHandle = -1; // AnimatedMorphCube.glb on marker 0 (loadModel), or an error
    bool showModel = false;
};

#endif // WEBAR_APP_H
