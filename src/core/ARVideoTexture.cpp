#include "core/ARVideoTexture.h"

#include <GLES3/gl3.h>
#include <emscripten.h>

// Helper JavaScript to update the WebGL texture from the <video> tag
EM_JS(void, update_webgl_texture, (int textureId), {
  const video = document.getElementById('ar-video');

  // Try to get the GL context from multiple sources
  var gl = (GL.currentContext && GL.currentContext.GLctx) || Module.ctx;
  if (!gl) {
    const canvas = document.getElementById('canvas');
    gl = canvas.getContext('webgl2') || canvas.getContext('webgl');
  }

  if (!gl) {
    console.error(
        "Critical: Could not find WebGL context in update_webgl_texture");
    return;
  }

  if (video && video.readyState >= video.HAVE_CURRENT_DATA) {
    const glTexture = GL.textures[textureId];
    if (!glTexture) {
      console.error("Critical: GL.textures[" + textureId +
                    "] is undefined. Native texture handle not found in " +
                    "Emscripten GL object.");
      return;
    }

    gl.bindTexture(gl.TEXTURE_2D, glTexture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGB, gl.RGB, gl.UNSIGNED_BYTE, video);
    gl.bindTexture(gl.TEXTURE_2D, null);
    // Optional: verify error
    const err = gl.getError();
    if (err != 0) {
      console.error("WebGL Error in update_webgl_texture:", err);
    }
  } else {
    // console.warn("Video not ready yet");
  }
});

uint32_t createVideoTexture(int width, int height) {
  GLuint id = 0;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
  glBindTexture(GL_TEXTURE_2D, 0);
  return id;
}

void uploadVideoFrame(uint32_t textureId) { update_webgl_texture(static_cast<int>(textureId)); }
