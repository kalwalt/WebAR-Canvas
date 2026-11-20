#ifndef IAR_RENDERER_H
#define IAR_RENDERER_H

#include "core/ARPrimitives.h"

/**
 * The rendering backend of WebAR-Canvas (#13): the engine draws the camera video and the
 * scene only through this interface. FilamentRenderer is the default backend, SokolRenderer
 * the light one (WEBAR_RENDERER in CMakeLists.txt).
 *
 * Call order: init() once, setProjection() once the camera lens is known, then per tick
 * updateVideoTexture() (when a new camera frame arrived) and render().
 */
class IARRenderer {
public:
  virtual ~IARRenderer() = default;

  // Creates the WebGL context on canvasSelector (e.g. "#canvas") at width x height and the
  // backend's resources, including the video background. False on failure.
  virtual bool init(const char *canvasSelector, int width, int height) = 0;

  // Column-major OpenGL projection matrix (ARNFTTracker::getCameraLens()) and its planes.
  virtual void setProjection(const double projection[16], double nearPlane,
                             double farPlane) = 0;

  // Uploads the current frame of <video id="ar-video"> to the video background texture.
  virtual void updateVideoTexture() = 0;

  // Draws the video background and the scene.
  virtual void render() = 0;

  // --- Backend-neutral meshes (ARBaseApp::addBox & co.) ---
  // Only what the demo needs: unlit, vertex-coloured triangles (ARPrimitives.h), drawn over the
  // video with depth testing and no face culling. No materials, textures or lighting.

  // Adds the mesh to the scene, visible, with an identity transform. Returns an id >= 0, or -1
  // before a successful init() or for an empty mesh.
  virtual int addMesh(const ARMesh &mesh) = 0;
  // The mesh's camera-space transform (column-major, millimetres); all zeros draws nothing.
  virtual void setMeshTransform(int id, const float transform[16]) = 0;
  virtual void setMeshVisible(int id, bool visible) = 0;
  // Releases the mesh; its id is not reused. Unknown ids are ignored by all mesh calls.
  virtual void removeMesh(int id) = 0;
};

#endif // IAR_RENDERER_H
