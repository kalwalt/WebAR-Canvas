#include "core/SokolRenderer.h"

#include "sokol_gfx.h"
#include "sokol_log.h"

#include <emscripten/html5.h>

#include <cstdio>

SokolRenderer::~SokolRenderer() {
  if (ready) sg_shutdown();
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
  return true;
}

void SokolRenderer::setProjection(const double p[16], double /*nearPlane*/, double /*farPlane*/) {
  for (int i = 0; i < 16; ++i) projection[i] = static_cast<float>(p[i]);
}

void SokolRenderer::updateVideoTexture() {}

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
  sg_end_pass();
  sg_commit();

  // The same progress line as FilamentRenderer (the measurement page counts it).
  static int frameCount = 0;
  if (frameCount++ % 120 == 0) printf("Rendered frame %d (sokol)\n", frameCount);
}
