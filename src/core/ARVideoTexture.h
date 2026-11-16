#ifndef AR_VIDEO_TEXTURE_H
#define AR_VIDEO_TEXTURE_H

#include <cstdint>

/**
 * The camera video as a WebGL texture, shared by the rendering backends (#13). The backend
 * creates the texture once in the current WebGL context and imports it (Filament:
 * Texture::Builder().import(); sokol: sg_image_desc.gl_texture). Every new camera frame is then
 * copied from <video id="ar-video"> straight into it with texImage2D, on the GPU side.
 *
 * The upload binds and unbinds the texture through WebGL, behind the backend's back: a backend
 * that caches GL state must reset its cache afterwards (sokol: sg_reset_state_cache()).
 */

// Creates an RGB8 texture of width x height (linear filtering, clamped) in the current context
// and returns its GL name.
uint32_t createVideoTexture(int width, int height);

// Copies the current frame of <video id="ar-video"> into the texture, if the video has data.
void uploadVideoFrame(uint32_t textureId);

#endif // AR_VIDEO_TEXTURE_H
