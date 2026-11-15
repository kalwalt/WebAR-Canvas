#ifndef AR_ANCHOR_H
#define AR_ANCHOR_H

#include <cmath>

/**
 * Anchoring an object to an NFT marker (#2 phase 3), renderer-independent: everything is a
 * column-major OpenGL 4x4 float matrix (translation in [12], [13], [14]), in millimetres.
 *
 * Per camera frame, update() takes the marker's model-view matrix and gives the object's matrix:
 * the pose (optionally smoothed) times a translation to the marker's centre. ARBaseApp applies it
 * to Filament entities with attachToMarker(); other backends can use the matrix directly.
 */
struct ARAnchorOptions {
  bool centerOnMarker = true; // put the origin at the marker's centre (from width, height, dpi)
  float smoothing = 0.0f;     // 0 = none; otherwise the weight of the previous pose, in [0, 1)
};

/** Millimetres from the marker's corner (its pose origin) to its centre. */
inline void markerCenterMm(int widthPx, int heightPx, int dpi, float &x, float &y) {
  x = dpi > 0 ? widthPx / static_cast<float>(dpi) * 25.4f * 0.5f : 0.0f;
  y = dpi > 0 ? heightPx / static_cast<float>(dpi) * 25.4f * 0.5f : 0.0f;
}

class ARAnchor {
public:
  explicit ARAnchor(ARAnchorOptions options = {}) : options(options) {}

  // tracked: the marker's state on this frame; pose: its model-view matrix; centreX/Y: its
  // centre in mm (markerCenterMm). out: the anchored matrix (unchanged while not tracked).
  void update(bool tracked, const float pose[16], float centerX, float centerY, float out[16]) {
    isVisible = tracked;
    if (!tracked) {
      hasPrevious = false; // no blending across a loss of tracking
      return;
    }
    float current[16];
    for (int i = 0; i < 16; ++i) current[i] = pose[i];
    if (hasPrevious && options.smoothing > 0.0f) {
      blend(previous, current, clampSmoothing(options.smoothing), current);
    }
    for (int i = 0; i < 16; ++i) previous[i] = current[i];
    hasPrevious = true;

    const float tx = options.centerOnMarker ? centerX : 0.0f;
    const float ty = options.centerOnMarker ? centerY : 0.0f;
    for (int i = 0; i < 16; ++i) out[i] = current[i];
    // out = current * translation(tx, ty, 0)
    for (int r = 0; r < 4; ++r) {
      out[12 + r] = current[0 + r] * tx + current[4 + r] * ty + current[12 + r];
    }
  }

  bool visible() const { return isVisible; }

  ARAnchorOptions options;

private:
  static float clampSmoothing(float s) { return s < 0.0f ? 0.0f : (s > 0.99f ? 0.99f : s); }

  // out = previous * s + current * (1 - s): translation linearly, rotation by quaternion slerp.
  static void blend(const float previous[16], const float current[16], float s, float out[16]) {
    float qa[4], qb[4], q[4];
    toQuaternion(previous, qa);
    toQuaternion(current, qb);
    slerp(qa, qb, 1.0f - s, q);
    fromQuaternion(q, out);
    for (int r = 0; r < 3; ++r) out[12 + r] = previous[12 + r] * s + current[12 + r] * (1.0f - s);
    out[3] = out[7] = out[11] = 0.0f;
    out[15] = 1.0f;
  }

  // Quaternion (x, y, z, w) of the rotation part of a column-major matrix (no scale assumed).
  static void toQuaternion(const float m[16], float q[4]) {
    const float m00 = m[0], m11 = m[5], m22 = m[10];
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
      const float t = std::sqrt(trace + 1.0f) * 2.0f;
      q[3] = 0.25f * t;
      q[0] = (m[6] - m[9]) / t;
      q[1] = (m[8] - m[2]) / t;
      q[2] = (m[1] - m[4]) / t;
    } else if (m00 > m11 && m00 > m22) {
      const float t = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
      q[3] = (m[6] - m[9]) / t;
      q[0] = 0.25f * t;
      q[1] = (m[4] + m[1]) / t;
      q[2] = (m[8] + m[2]) / t;
    } else if (m11 > m22) {
      const float t = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
      q[3] = (m[8] - m[2]) / t;
      q[0] = (m[4] + m[1]) / t;
      q[1] = 0.25f * t;
      q[2] = (m[9] + m[6]) / t;
    } else {
      const float t = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
      q[3] = (m[1] - m[4]) / t;
      q[0] = (m[8] + m[2]) / t;
      q[1] = (m[9] + m[6]) / t;
      q[2] = 0.25f * t;
    }
  }

  static void fromQuaternion(const float q[4], float m[16]) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1 - 2 * (y * y + z * z); m[4] = 2 * (x * y - z * w);     m[8] = 2 * (x * z + y * w);
    m[1] = 2 * (x * y + z * w);     m[5] = 1 - 2 * (x * x + z * z); m[9] = 2 * (y * z - x * w);
    m[2] = 2 * (x * z - y * w);     m[6] = 2 * (y * z + x * w);     m[10] = 1 - 2 * (x * x + y * y);
  }

  // Spherical interpolation from a (t = 0) to b (t = 1), along the shorter arc, normalised.
  static void slerp(const float a[4], float b[4], float t, float out[4]) {
    float cosTheta = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float bb[4] = {b[0], b[1], b[2], b[3]};
    if (cosTheta < 0.0f) {
      cosTheta = -cosTheta;
      for (float &v : bb) v = -v;
    }
    float wa, wb;
    if (cosTheta > 0.9995f) { // nearly equal: linear interpolation
      wa = 1.0f - t;
      wb = t;
    } else {
      const float theta = std::acos(cosTheta);
      const float sinTheta = std::sin(theta);
      wa = std::sin((1.0f - t) * theta) / sinTheta;
      wb = std::sin(t * theta) / sinTheta;
    }
    float len = 0.0f;
    for (int i = 0; i < 4; ++i) {
      out[i] = a[i] * wa + bb[i] * wb;
      len += out[i] * out[i];
    }
    len = std::sqrt(len);
    for (int i = 0; i < 4; ++i) out[i] = len > 0.0f ? out[i] / len : (i == 3 ? 1.0f : 0.0f);
  }

  float previous[16] = {0};
  bool hasPrevious = false;
  bool isVisible = false;
};

#endif // AR_ANCHOR_H
