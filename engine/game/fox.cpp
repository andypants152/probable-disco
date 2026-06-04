#include "fox.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace voxel {

namespace {

struct Box {
  Vec3 min;
  Vec3 max;
  PackedColor color;
};

struct Face {
  Vec3 normal;
  std::array<Vec3, 4> corners;
};

constexpr std::array<Face, 6> kBoxFaces = {{
  {{1.0f, 0.0f, 0.0f}, {{{1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 1.0f}}}},
  {{-1.0f, 0.0f, 0.0f}, {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}}}},
  {{0.0f, 1.0f, 0.0f}, {{{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}}}},
  {{0.0f, -1.0f, 0.0f}, {{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}}},
  {{0.0f, 0.0f, 1.0f}, {{{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f}}}},
  {{0.0f, 0.0f, -1.0f}, {{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}}},
}};

std::uint8_t shade_channel(std::uint8_t channel, float shade) {
  const int value = static_cast<int>(static_cast<float>(channel) * shade);
  if (value < 0) {
    return 0;
  }
  if (value > 255) {
    return 255;
  }
  return static_cast<std::uint8_t>(value);
}

PackedColor shaded(PackedColor color, Vec3 normal) {
  float shade = 0.72f;
  if (normal.y > 0.5f) {
    shade = 1.0f;
  } else if (normal.y < -0.5f) {
    shade = 0.48f;
  } else if (normal.x > 0.5f || normal.z > 0.5f) {
    shade = 0.84f;
  }

  const auto r = static_cast<std::uint8_t>((color >> 24) & 0xffu);
  const auto g = static_cast<std::uint8_t>((color >> 16) & 0xffu);
  const auto b = static_cast<std::uint8_t>((color >> 8) & 0xffu);
  const auto a = static_cast<std::uint8_t>(color & 0xffu);
  return pack_rgba(shade_channel(r, shade), shade_channel(g, shade), shade_channel(b, shade), a);
}

Vec3 rotate_y(Vec3 v, float heading) {
  const float s = std::sin(heading);
  const float c = std::cos(heading);
  return {
    v.x * c + v.z * s,
    v.y,
    -v.x * s + v.z * c,
  };
}

void append_box(Mesh& mesh, const Box& box, Vec3 origin, float heading) {
  for (const Face& face : kBoxFaces) {
    const Index start = static_cast<Index>(mesh.vertices.size());
    const Vec3 normal = rotate_y(face.normal, heading);
    for (const Vec3& corner : face.corners) {
      const Vec3 local = {
        box.min.x + (box.max.x - box.min.x) * corner.x,
        box.min.y + (box.max.y - box.min.y) * corner.y,
        box.min.z + (box.max.z - box.min.z) * corner.z,
      };
      const Vec3 rotated = rotate_y(local, heading);
      mesh.vertices.push_back(origin + rotated);
      mesh.normals.push_back(normal);
      mesh.colors.push_back(shaded(box.color, normal));
      mesh.micro_positions.push_back({corner.x - 0.5f, corner.y - 0.5f, corner.z - 0.5f});
    }

    mesh.indices.push_back(start + 0);
    mesh.indices.push_back(start + 1);
    mesh.indices.push_back(start + 2);
    mesh.indices.push_back(start + 0);
    mesh.indices.push_back(start + 2);
    mesh.indices.push_back(start + 3);
  }
}

}  // namespace

void append_fox_mesh(Mesh& mesh, Vec3 ground_center, float heading_radians) {
  constexpr PackedColor orange = pack_rgba(210, 92, 34);
  constexpr PackedColor dark_orange = pack_rgba(166, 66, 28);
  constexpr PackedColor white = pack_rgba(236, 231, 211);
  constexpr PackedColor black = pack_rgba(22, 19, 16);

  const std::array<Box, 17> boxes = {{
    // Body and chest.
    {{-1.25f, 0.55f, -1.05f}, {1.25f, 1.35f, 1.10f}, orange},
    {{-0.72f, 0.62f, 0.92f}, {0.72f, 1.30f, 1.22f}, white},

    // Head, snout, ears, and eyes. The fox faces +Z toward the starting camera.
    {{-0.76f, 1.10f, 0.85f}, {0.76f, 1.95f, 1.72f}, orange},
    {{-0.46f, 1.22f, 1.66f}, {0.46f, 1.56f, 2.25f}, white},
    {{-0.14f, 1.36f, 2.21f}, {0.14f, 1.54f, 2.38f}, black},
    {{-0.48f, 1.64f, 1.70f}, {-0.25f, 1.82f, 1.88f}, black},
    {{0.25f, 1.64f, 1.70f}, {0.48f, 1.82f, 1.88f}, black},
    {{-0.68f, 1.88f, 1.02f}, {-0.28f, 2.45f, 1.42f}, dark_orange},
    {{0.28f, 1.88f, 1.02f}, {0.68f, 2.45f, 1.42f}, dark_orange},

    // Legs and paws.
    {{-0.98f, 0.00f, -0.78f}, {-0.58f, 0.62f, -0.38f}, dark_orange},
    {{0.58f, 0.00f, -0.78f}, {0.98f, 0.62f, -0.38f}, dark_orange},
    {{-0.98f, 0.00f, 0.45f}, {-0.58f, 0.62f, 0.85f}, dark_orange},
    {{0.58f, 0.00f, 0.45f}, {0.98f, 0.62f, 0.85f}, dark_orange},

    // Raised tail with white tip.
    {{-0.48f, 0.96f, -2.15f}, {0.48f, 1.58f, -0.95f}, dark_orange},
    {{-0.40f, 1.22f, -2.92f}, {0.40f, 1.82f, -2.08f}, orange},
    {{-0.34f, 1.32f, -3.35f}, {0.34f, 1.76f, -2.86f}, white},

    // Small shadow block to ground the model visually.
    {{-1.28f, 0.01f, -1.05f}, {1.28f, 0.04f, 1.08f}, pack_rgba(42, 55, 32)},
  }};

  for (const Box& box : boxes) {
    append_box(mesh, box, ground_center, heading_radians);
  }
}

}  // namespace voxel
