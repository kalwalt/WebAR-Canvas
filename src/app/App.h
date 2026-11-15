#ifndef WEBAR_APP_H
#define WEBAR_APP_H

#include "core/ARBaseApp.h"
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/VertexBuffer.h>
#include <utils/Entity.h>

// The demo app: a colourful 3D cube on the pinball marker.
class App : public ARBaseApp {
public:
    ~App() override;

    // Asset loading: create a colorful 3D Cube sitting on the marker
    void setup() override;

    // Update logic: attach the 3D Cube to the marker pose
    void update(float deltaTime) override;

    void onMarkerFound(int markerIndex) override;
    void onMarkerLost(int markerIndex) override;

private:
    utils::Entity anchorEntity; // bound to marker 0, centred on it
    utils::Entity cubeEntity;   // child of anchorEntity, spinning
    filament::VertexBuffer* cubeVb = nullptr;
    filament::IndexBuffer* cubeIb = nullptr;
    filament::Material* cubeMaterial = nullptr;
    filament::MaterialInstance* cubeMaterialInst = nullptr;
};

#endif // WEBAR_APP_H
