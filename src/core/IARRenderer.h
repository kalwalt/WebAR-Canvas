#ifndef IAR_RENDERER_H
#define IAR_RENDERER_H

/**
 * The rendering backend of WebAR-Canvas (#13): the engine draws the camera video and the
 * scene only through this interface. FilamentRenderer is the default backend.
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
};

#endif // IAR_RENDERER_H
