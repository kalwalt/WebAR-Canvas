#include "core/SokolRenderer.h"
#include "core/ARVideoTexture.h"

#include "sokol_log.h"

#include <GLES3/gl3.h>
#include <emscripten/html5.h>

#include <cstdio>

namespace {

// The video background: a full-screen quad in clip space. V is flipped (v = 1 - v), as Filament
// does by default for its materials (flipUV), so the video shows the same way up on both
// backends: texImage2D from a <video> puts the image's top row at v = 0.
const char *kVideoVertexShader = R"(#version 300 es
in vec2 position;
out vec2 uv;
void main() {
  gl_Position = vec4(position, 1.0, 1.0);
  uv = vec2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
}
)";

const char *kVideoFragmentShader = R"(#version 300 es
precision mediump float;
uniform sampler2D video_tex;
in vec2 uv;
out vec4 frag_color;
void main() {
  frag_color = vec4(texture(video_tex, uv).rgb, 1.0);
}
)";

} // namespace

SokolRenderer::~SokolRenderer() {
  if (!ready) return;
  sg_shutdown(); // releases every sokol resource; the injected GL texture is ours
  if (videoTextureId) {
    GLuint id = videoTextureId;
    glDeleteTextures(1, &id);
  }
}

bool SokolRenderer::init(const char *canvasSelector, int w, int h) {
  width = w;
  height = h;

  // The same WebGL 2 context as FilamentRenderer: depth, no stencil, no MSAA, opaque.
  EmscriptenWebGLContextAttributes attr;
  emscripten_webgl_init_context_attributes(&attr);
  attr.majorVersion = 2;
  attr.minorVersion = 0;
  attr.depth = true;
  attr.stencil = false;
  attr.antialias = false;
  attr.alpha = false;
  attr.preserveDrawingBuffer = false;
  attr.failIfMajorPerformanceCaveat = false;

  emscripten_set_canvas_element_size(canvasSelector, width, height);
  const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context(canvasSelector, &attr);
  if (ctx <= 0) {
    printf("CRITICAL ERROR: Failed to create WebGL 2 context!\n");
    return false;
  }
  if (emscripten_webgl_make_context_current(ctx) != EMSCRIPTEN_RESULT_SUCCESS) {
    printf("CRITICAL ERROR: Failed to make WebGL context current!\n");
    return false;
  }

  sg_desc desc = {};
  desc.logger.func = slog_func;
  desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
  desc.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH;
  desc.environment.defaults.sample_count = 1;
  sg_setup(&desc);
  if (!sg_isvalid()) {
    printf("ERROR: sokol_gfx setup failed\n");
    return false;
  }
  ready = true;
  printf("WebGL 2 Context created and active. sokol_gfx initialized.\n");
  return createVideoBackground();
}

bool SokolRenderer::createVideoBackground() {
  // The WebGL texture the camera video is copied into (ARVideoTexture, shared with Filament).
  // Created with raw GL calls: sokol's cached GL state is reset before sokol is used again.
  videoTextureId = createVideoTexture(width, height);
  // sokol's GL samplers always use a mipmap min filter (LINEAR_MIPMAP_NEAREST here), and sokol
  // limits the mip levels only of the textures it allocates itself. Without this, the injected
  // one-level texture is incomplete and samples black.
  glBindTexture(GL_TEXTURE_2D, videoTextureId);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  sg_reset_state_cache();

  // Injected into sokol. Its real storage is RGB8 (texImage2D from the video); sokol has no RGB8
  // format, and it neither allocates nor uploads an injected texture, so RGBA8 only describes
  // how the shader samples it.
  sg_image_desc image = {};
  image.type = SG_IMAGETYPE_2D;
  image.width = width;
  image.height = height;
  image.num_mipmaps = 1;
  image.pixel_format = SG_PIXELFORMAT_RGBA8;
  image.gl_texture = videoTextureId;
  image.label = "video-texture";
  videoImage = sg_make_image(&image);

  sg_view_desc view = {};
  view.texture.image = videoImage;
  view.label = "video-view";
  videoView = sg_make_view(&view);

  sg_sampler_desc sampler = {};
  sampler.min_filter = SG_FILTER_LINEAR;
  sampler.mag_filter = SG_FILTER_LINEAR;
  sampler.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
  sampler.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
  sampler.label = "video-sampler";
  videoSampler = sg_make_sampler(&sampler);

  static const float kQuad[8] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
  sg_buffer_desc quad = {};
  quad.usage.vertex_buffer = true;
  quad.data = {kQuad, sizeof(kQuad)};
  quad.label = "video-quad";
  videoQuad = sg_make_buffer(&quad);

  sg_shader_desc shader = {};
  shader.vertex_func.source = kVideoVertexShader;
  shader.fragment_func.source = kVideoFragmentShader;
  shader.attrs[0].base_type = SG_SHADERATTRBASETYPE_FLOAT;
  shader.attrs[0].glsl_name = "position";
  shader.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
  shader.views[0].texture.image_type = SG_IMAGETYPE_2D;
  shader.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
  shader.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
  shader.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
  shader.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
  shader.texture_sampler_pairs[0].view_slot = 0;
  shader.texture_sampler_pairs[0].sampler_slot = 0;
  shader.texture_sampler_pairs[0].glsl_name = "video_tex";
  shader.label = "video-shader";
  videoShader = sg_make_shader(&shader);

  // Drawn first, behind everything: no depth test, no depth write (as video_background.mat).
  sg_pipeline_desc pipeline = {};
  pipeline.shader = videoShader;
  pipeline.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
  pipeline.primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
  pipeline.depth.compare = SG_COMPAREFUNC_ALWAYS;
  pipeline.depth.write_enabled = false;
  pipeline.label = "video-pipeline";
  videoPipeline = sg_make_pipeline(&pipeline);

  const bool ok = sg_query_pipeline_state(videoPipeline) == SG_RESOURCESTATE_VALID &&
                  sg_query_view_state(videoView) == SG_RESOURCESTATE_VALID;
  if (!ok) printf("ERROR: the sokol video background could not be created\n");
  return ok;
}

void SokolRenderer::setProjection(const double p[16], double /*nearPlane*/, double /*farPlane*/) {
  for (int i = 0; i < 16; ++i) projection[i] = static_cast<float>(p[i]);
}

void SokolRenderer::updateVideoTexture() {
  if (!ready || !videoTextureId) return;
  uploadVideoFrame(videoTextureId);
  sg_reset_state_cache(); // the upload bound and unbound the texture behind sokol's back
}

void SokolRenderer::render() {
  if (!ready) return;
  sg_pass pass = {};
  pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
  pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
  pass.action.depth.load_action = SG_LOADACTION_CLEAR;
  pass.action.depth.clear_value = 1.0f;
  pass.swapchain.width = width;
  pass.swapchain.height = height;
  pass.swapchain.sample_count = 1;
  pass.swapchain.color_format = SG_PIXELFORMAT_RGBA8;
  pass.swapchain.depth_format = SG_PIXELFORMAT_DEPTH;
  pass.swapchain.gl.framebuffer = 0; // the canvas
  sg_begin_pass(&pass);

  sg_bindings video = {};
  video.vertex_buffers[0] = videoQuad;
  video.views[0] = videoView;
  video.samplers[0] = videoSampler;
  sg_apply_pipeline(videoPipeline);
  sg_apply_bindings(&video);
  sg_draw(0, 4, 1);

  sg_end_pass();
  sg_commit();

  // The same progress line as FilamentRenderer (the measurement page counts it).
  static int frameCount = 0;
  if (frameCount++ % 120 == 0) printf("Rendered frame %d (sokol)\n", frameCount);
}
