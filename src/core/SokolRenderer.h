#ifndef SOKOL_RENDERER_H
#define SOKOL_RENDERER_H

#include "core/IARRenderer.h"

/**
 * IARRenderer on sokol_gfx (WebGL 2, the GLES3 backend; #13): no host tools, no material
 * compiler. The WebGL context is created here (as in FilamentRenderer); sokol draws into the
 * canvas's default framebuffer.
 */
class SokolRenderer : public IARRenderer {
public:
  SokolRenderer() = default;
  ~SokolRenderer() override;

  bool init(const char *canvasSelector, int width, int height) override;
  void setProjection(const double projection[16], double nearPlane, double farPlane) override;
  void updateVideoTexture() override;
  void render() override;

private:
  bool ready = false; // sg_setup() done
  int width = 0;
  int height = 0;
  float projection[16] = {}; // column-major, from ARNFTTracker::getCameraLens()
};

#endif // SOKOL_RENDERER_H
