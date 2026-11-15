#ifndef AR_PRIMITIVES_H
#define AR_PRIMITIVES_H

#include <cmath>
#include <cstdint>
#include <vector>

/**
 * Geometry for quick prototyping (#2): boxes, planes and spheres as vertex-coloured triangle
 * meshes, renderer-independent. Units are millimetres, like marker poses. ARBaseApp's
 * createBox / createCube / createPlane / createSphere turn them into Filament entities.
 */
struct ARVertex {
  float position[3];
  float color[4];
};

struct ARMesh {
  std::vector<ARVertex> vertices;
  std::vector<uint16_t> indices; // triangles, counter-clockwise from outside
};

/**
 * A box centred in x and y, standing on z = 0 (the marker plane), sizeZ high. Faces, in this
 * order and with these colours: +Z (top, z = sizeZ), -Z (base, z = 0), -X, +X, -Y, +Y.
 */
inline ARMesh makeBox(float sizeX, float sizeY, float sizeZ, const float faceColors[6][4]) {
  const float hx = sizeX * 0.5f, hy = sizeY * 0.5f, z = sizeZ;
  const float faces[6][4][3] = {
      {{-hx, -hy, z}, {hx, -hy, z}, {hx, hy, z}, {-hx, hy, z}},       // +Z
      {{-hx, -hy, 0}, {-hx, hy, 0}, {hx, hy, 0}, {hx, -hy, 0}},       // -Z
      {{-hx, -hy, 0}, {-hx, -hy, z}, {-hx, hy, z}, {-hx, hy, 0}},     // -X
      {{hx, -hy, 0}, {hx, hy, 0}, {hx, hy, z}, {hx, -hy, z}},         // +X
      {{-hx, -hy, 0}, {hx, -hy, 0}, {hx, -hy, z}, {-hx, -hy, z}},     // -Y
      {{-hx, hy, 0}, {-hx, hy, z}, {hx, hy, z}, {hx, hy, 0}},         // +Y
  };
  ARMesh mesh;
  for (int f = 0; f < 6; ++f) {
    const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
    for (int v = 0; v < 4; ++v) {
      ARVertex vertex{};
      for (int i = 0; i < 3; ++i) vertex.position[i] = faces[f][v][i];
      for (int i = 0; i < 4; ++i) vertex.color[i] = faceColors[f][i];
      mesh.vertices.push_back(vertex);
    }
    const uint16_t quad[6] = {0, 1, 2, 0, 2, 3};
    for (uint16_t q : quad) mesh.indices.push_back(static_cast<uint16_t>(base + q));
  }
  return mesh;
}

/** A rectangle centred on the origin in the z = 0 plane, facing +z. */
inline ARMesh makePlane(float sizeX, float sizeY, const float color[4]) {
  const float hx = sizeX * 0.5f, hy = sizeY * 0.5f;
  const float corners[4][2] = {{-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}};
  ARMesh mesh;
  for (const auto &c : corners) {
    ARVertex vertex{{c[0], c[1], 0.0f}, {color[0], color[1], color[2], color[3]}};
    mesh.vertices.push_back(vertex);
  }
  mesh.indices = {0, 1, 2, 0, 2, 3};
  return mesh;
}

/**
 * A UV sphere centred on the origin: segments around z, rings from pole to pole (at least
 * 3 x 2). Vertices: (rings + 1) * (segments + 1); indices: rings * segments * 6.
 */
inline ARMesh makeSphere(float radius, int segments, int rings, const float color[4]) {
  segments = segments < 3 ? 3 : segments;
  rings = rings < 2 ? 2 : rings;
  const float pi = 3.14159265358979f;
  ARMesh mesh;
  for (int r = 0; r <= rings; ++r) {
    const float phi = pi * r / rings; // 0 at +z, pi at -z
    for (int s = 0; s <= segments; ++s) {
      const float theta = 2.0f * pi * s / segments;
      ARVertex vertex{{radius * std::sin(phi) * std::cos(theta),
                       radius * std::sin(phi) * std::sin(theta), radius * std::cos(phi)},
                      {color[0], color[1], color[2], color[3]}};
      mesh.vertices.push_back(vertex);
    }
  }
  const int row = segments + 1;
  for (int r = 0; r < rings; ++r) {
    for (int s = 0; s < segments; ++s) {
      const uint16_t a = static_cast<uint16_t>(r * row + s), b = static_cast<uint16_t>(a + row);
      const uint16_t quad[6] = {a, b, static_cast<uint16_t>(a + 1),
                                static_cast<uint16_t>(a + 1), b, static_cast<uint16_t>(b + 1)};
      for (uint16_t q : quad) mesh.indices.push_back(q);
    }
  }
  return mesh;
}

#endif // AR_PRIMITIVES_H
