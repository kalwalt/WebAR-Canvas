#ifndef SOKOL_RENDERER_H
#define SOKOL_RENDERER_H

#include "core/IARRenderer.h"

#include "sokol_gfx.h"

#include <cstdint>

/**
 * IARRenderer on sokol_gfx (WebGL 2, the GLES3 backend; #13): no host tools, no material
 * compiler. The WebGL context is created here (as in FilamentRenderer); sokol draws into the
 * canvas's default framebuffer. The camera video is a WebGL texture (ARVideoTexture) injected
 * into sokol and drawn on a full-screen quad, as in FilamentRenderer.
 */
class SokolRenderer final : public IARRenderer {
public:
  SokolRenderer() = default;
  ~SokolRenderer() override;

  SokolRenderer(const SokolRenderer &) = delete;
  SokolRenderer &operator=(const SokolRenderer &) = delete;

  bool init(const char *canvasSelector, int width, int height) override;
  void setProjection(const double projection[16], double nearPlane, double farPlane) override;
  void updateVideoTexture() override;
  void render() override;

private:
  bool createVideoBackground();

  bool ready = false; // sg_setup() done
  int width = 0;
  int height = 0;
  float projection[16] = {}; // column-major, from ARNFTTracker::getCameraLens()

  // Video background
  uint32_t videoTextureId = 0; // GL name, filled by ARVideoTexture
  sg_image videoImage = {};    // the injected texture
  sg_view videoView = {};      // the texture view the shader samples
  sg_sampler videoSampler = {};
  sg_buffer videoQuad = {};
  sg_shader videoShader = {};
  sg_pipeline videoPipeline = {};
};

#endif // SOKOL_RENDERER_H
