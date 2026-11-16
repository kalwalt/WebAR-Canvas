#include "core/SokolRenderer.h"
#include "core/ARVideoTexture.h"

#include "sokol_log.h"

#include <GLES3/gl3.h>
#include <emscripten/html5.h>

#include <cstddef>
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

// The meshes: unlit, vertex colour, as ar_cube.mat (Filament's unlit model with no
// post-processing writes the vertex colour unchanged).
const char *kMeshVertexShader = R"(#version 300 es
uniform mat4 mvp;
in vec3 position;
in vec4 color0;
out vec4 color;
void main() {
  gl_Position = mvp * vec4(position, 1.0);
  color = color0;
}
)";

const char *kMeshFragmentShader = R"(#version 300 es
precision mediump float;
in vec4 color;
out vec4 frag_color;
void main() {
  frag_color = color;
}
)";

// out = a * b, column-major.
void multiply(const float a[16], const float b[16], float out[16]) {
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) sum += a[k * 4 + r] * b[c * 4 + k];
      out[c * 4 + r] = sum;
    }
  }
}

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
  desc.buffer_pool_size = 1024; // 2 buffers per mesh (sokol's default pool holds 128)
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
  return createVideoBackground() && createMeshPipeline();
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

bool SokolRenderer::createMeshPipeline() {
  sg_shader_desc shader = {};
  shader.vertex_func.source = kMeshVertexShader;
  shader.fragment_func.source = kMeshFragmentShader;
  shader.attrs[0].base_type = SG_SHADERATTRBASETYPE_FLOAT;
  shader.attrs[0].glsl_name = "position";
  shader.attrs[1].base_type = SG_SHADERATTRBASETYPE_FLOAT;
  shader.attrs[1].glsl_name = "color0";
  shader.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
  shader.uniform_blocks[0].size = 16 * sizeof(float);
  shader.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
  shader.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
  shader.label = "mesh-shader";
  meshShader = sg_make_shader(&shader);

  sg_pipeline_desc pipeline = {};
  pipeline.shader = meshShader;
  pipeline.layout.buffers[0].stride = sizeof(ARVertex);
  pipeline.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
  pipeline.layout.attrs[0].offset = offsetof(ARVertex, position);
  pipeline.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
  pipeline.layout.attrs[1].offset = offsetof(ARVertex, color);
  pipeline.index_type = SG_INDEXTYPE_UINT16;
  pipeline.cull_mode = SG_CULLMODE_NONE; // as ar_cube.mat (culling: none)
  pipeline.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
  pipeline.depth.write_enabled = true;
  pipeline.label = "mesh-pipeline";
  meshPipeline = sg_make_pipeline(&pipeline);

  const bool ok = sg_query_pipeline_state(meshPipeline) == SG_RESOURCESTATE_VALID;
  if (!ok) printf("ERROR: the sokol mesh pipeline could not be created\n");
  return ok;
}

SokolRenderer::Mesh *SokolRenderer::findMesh(int id) {
  return id >= 0 && id < static_cast<int>(meshes.size()) && meshes[id].alive ? &meshes[id]
                                                                             : nullptr;
}

int SokolRenderer::addMesh(const ARMesh &mesh) {
  if (!ready || mesh.vertices.empty() || mesh.indices.empty()) return -1;
  Mesh m;
  sg_buffer_desc vertices = {};
  vertices.usage.vertex_buffer = true;
  vertices.data = {mesh.vertices.data(), mesh.vertices.size() * sizeof(ARVertex)};
  vertices.label = "mesh-vertices";
  m.vertices = sg_make_buffer(&vertices);
  sg_buffer_desc indices = {};
  indices.usage.index_buffer = true;
  indices.data = {mesh.indices.data(), mesh.indices.size() * sizeof(uint16_t)};
  indices.label = "mesh-indices";
  m.indices = sg_make_buffer(&indices);
  m.indexCount = static_cast<int>(mesh.indices.size());
  if (sg_query_buffer_state(m.vertices) != SG_RESOURCESTATE_VALID ||
      sg_query_buffer_state(m.indices) != SG_RESOURCESTATE_VALID) {
    // e.g. sokol's buffer pool is full (sg_desc.buffer_pool_size, 2 buffers per mesh)
    sg_destroy_buffer(m.vertices);
    sg_destroy_buffer(m.indices);
    printf("ERROR: sokol could not create the mesh buffers\n");
    return -1;
  }
  m.alive = true;
  meshes.push_back(m);
  return static_cast<int>(meshes.size()) - 1;
}

void SokolRenderer::setMeshTransform(int id, const float transform[16]) {
  Mesh *m = findMesh(id);
  if (!m) return;
  bool zero = true;
  for (int i = 0; i < 16; ++i) {
    m->transform[i] = transform[i];
    zero = zero && transform[i] == 0.0f;
  }
  m->zero = zero;
}

void SokolRenderer::setMeshVisible(int id, bool visible) {
  if (Mesh *m = findMesh(id)) m->visible = visible;
}

void SokolRenderer::removeMesh(int id) {
  Mesh *m = findMesh(id);
  if (!m) return;
  sg_destroy_buffer(m->vertices);
  sg_destroy_buffer(m->indices);
  *m = Mesh(); // not alive; the id is not reused
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

  // The meshes, over the video, with depth testing.
  bool pipelineApplied = false;
  for (const Mesh &m : meshes) {
    if (!m.alive || !m.visible || m.zero) continue;
    if (!pipelineApplied) {
      sg_apply_pipeline(meshPipeline);
      pipelineApplied = true;
    }
    sg_bindings bindings = {};
    bindings.vertex_buffers[0] = m.vertices;
    bindings.index_buffer = m.indices;
    sg_apply_bindings(&bindings);
    float mvp[16];
    multiply(projection, m.transform, mvp);
    const sg_range uniforms = {mvp, sizeof(mvp)};
    sg_apply_uniforms(0, &uniforms);
    sg_draw(0, m.indexCount, 1);
  }

  sg_end_pass();
  sg_commit();

  // The same progress line as FilamentRenderer (the measurement page counts it).
  static int frameCount = 0;
  if (frameCount++ % 120 == 0) printf("Rendered frame %d (sokol)\n", frameCount);
}
