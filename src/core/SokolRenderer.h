#ifndef SOKOL_RENDERER_H
#define SOKOL_RENDERER_H

#include "core/IARRenderer.h"

#include "sokol_gfx.h"

#include <cstdint>
#include <vector>

/**
 * IARRenderer on sokol_gfx (WebGL 2, the GLES3 backend; #13): no host tools, no material
 * compiler. The WebGL context is created here (as in FilamentRenderer); sokol draws into the
 * canvas's default framebuffer. The camera video is a WebGL texture (ARVideoTexture) injected
 * into sokol and drawn on a full-screen quad, as in FilamentRenderer. The meshes are drawn
 * after it with one unlit vertex-colour pipeline (projection * transform), as ar_cube.mat.
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

  int addMesh(const ARMesh &mesh) override;
  void setMeshTransform(int id, const float transform[16]) override;
  void setMeshVisible(int id, bool visible) override;
  void removeMesh(int id) override;

private:
  bool createVideoBackground();
  bool createMeshPipeline();

  struct Mesh {
    bool alive = false;
    bool visible = true;
    bool zero = false; // an all-zero transform: nothing to draw
    sg_buffer vertices = {};
    sg_buffer indices = {};
    int indexCount = 0;
    float transform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  };
  Mesh *findMesh(int id);

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

  // Meshes (backend-neutral API)
  sg_shader meshShader = {};
  sg_pipeline meshPipeline = {};
  std::vector<Mesh> meshes; // by id
};

#endif // SOKOL_RENDERER_H
