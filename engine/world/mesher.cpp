#include "mesher.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "art_scale.h"
#include "generator.h"

namespace voxel {

namespace {

struct Face {
  int dx;
  int dy;
  int dz;
  Vec3 normal;
  std::array<Vec3, 4> corners;
};

struct Box {
  Vec3 min;
  Vec3 max;
  PackedColor color;
};

constexpr std::array<Face, 6> kFaces = {{
  {1, 0, 0, {1.0f, 0.0f, 0.0f}, {{{1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 1.0f}}}},
  {-1, 0, 0, {-1.0f, 0.0f, 0.0f}, {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}}}},
  {0, 1, 0, {0.0f, 1.0f, 0.0f}, {{{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}}}},
  {0, -1, 0, {0.0f, -1.0f, 0.0f}, {{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}}},
  {0, 0, 1, {0.0f, 0.0f, 1.0f}, {{{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f}}}},
  {0, 0, -1, {0.0f, 0.0f, -1.0f}, {{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}}},
}};

constexpr float kMoonClearingX = 42.0f;
constexpr float kMoonClearingZ = -104.0f;
constexpr float kMoonClearingRadius = 16.0f;
constexpr float kOwlLandmarkX = 0.0f;
constexpr float kOwlLandmarkZ = -7.0f;
constexpr Vec3 kOwlPerchOffset = {-3.0f, 0.0f, 1.5f};
constexpr float kOwlPerchHeight = 1.78f;
constexpr int kWorldDressingStep = 6;
constexpr int kMushroomCandidateStep = 3;
constexpr float kMushroomSpawnChance = 0.13f;
constexpr float kMushroomClusterChance = 0.24f;
constexpr float kMushroomClusterRadius = 2.35f;
constexpr float kMushroomMinObstacleDistance = 1.65f;
constexpr float kMushroomMinTreeDistance = 1.75f;
constexpr float kMushroomMinSpacing = 0.95f;
constexpr float kClusterMushroomMinSpacing = 0.55f;
constexpr int kMushroomMaxLocalSlope = 1;
constexpr int kMushroomHeadroomVoxels = 3;

constexpr int kNearVisualDetailLevel = 2;

struct MushroomSpot {
  float x;
  float z;
};

std::uint32_t hash2(int x, int z, std::uint32_t seed = 0x6d2b79f5u) {
  std::uint32_t h = seed;
  h ^= static_cast<std::uint32_t>(x) * 0x8da6b343u;
  h ^= static_cast<std::uint32_t>(z) * 0xd8163841u;
  h ^= h >> 13;
  h *= 0x85ebca6bu;
  h ^= h >> 16;
  return h;
}

float hash01(int x, int z, std::uint32_t seed = 0x6d2b79f5u) {
  return static_cast<float>(hash2(x, z, seed) & 0xffffu) / 65535.0f;
}

std::uint32_t hash3(int x, int y, int z, std::uint32_t seed = 0x6d2b79f5u) {
  std::uint32_t h = hash2(x, z, seed);
  h ^= static_cast<std::uint32_t>(y) * 0xcb1ab31fu;
  h ^= h >> 15;
  h *= 0x9e3779b1u;
  h ^= h >> 16;
  return h;
}

float hash01_3(int x, int y, int z, std::uint32_t seed = 0x6d2b79f5u) {
  return static_cast<float>(hash3(x, y, z, seed) & 0xffffu) / 65535.0f;
}

float distance_sq(float ax, float az, float bx, float bz) {
  const float dx = ax - bx;
  const float dz = az - bz;
  return dx * dx + dz * dz;
}

float moon_clearing_influence(float world_x, float world_z) {
  const float dx = world_x - kMoonClearingX;
  const float dz = world_z - kMoonClearingZ;
  const float distance = std::sqrt(dx * dx + dz * dz);
  return std::max(0.0f, std::min(1.0f, (kMoonClearingRadius - distance) / 6.0f));
}

float smoothstep(float value) {
  value = std::max(0.0f, std::min(1.0f, value));
  return value * value * (3.0f - 2.0f * value);
}

float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

float sample_terrain_height(const TerrainGenerator& generator, float x, float z) {
  const int x0 = static_cast<int>(std::floor(x));
  const int z0 = static_cast<int>(std::floor(z));
  const float tx = smoothstep(x - static_cast<float>(x0));
  const float tz = smoothstep(z - static_cast<float>(z0));

  const float h00 = static_cast<float>(generator.terrain_height(x0, z0));
  const float h10 = static_cast<float>(generator.terrain_height(x0 + 1, z0));
  const float h01 = static_cast<float>(generator.terrain_height(x0, z0 + 1));
  const float h11 = static_cast<float>(generator.terrain_height(x0 + 1, z0 + 1));
  return lerp(lerp(h00, h10, tx), lerp(h01, h11, tx), tz);
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

Vec3 rotate_x(Vec3 v, float angle) {
  const float s = std::sin(angle);
  const float c = std::cos(angle);
  return {
    v.x,
    v.y * c - v.z * s,
    v.y * s + v.z * c,
  };
}

Vec3 rotate_z(Vec3 v, float angle) {
  const float s = std::sin(angle);
  const float c = std::cos(angle);
  return {
    v.x * c - v.y * s,
    v.x * s + v.y * c,
    v.z,
  };
}

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

PackedColor shaded(std::uint8_t r, std::uint8_t g, std::uint8_t b, float shade, std::uint8_t a = 255) {
  return pack_rgba(shade_channel(r, shade), shade_channel(g, shade), shade_channel(b, shade), a);
}

PackedColor mix_rgb(PackedColor a, PackedColor b, float t) {
  t = std::max(0.0f, std::min(1.0f, t));
  const auto ar = static_cast<int>((a >> 24) & 0xffu);
  const auto ag = static_cast<int>((a >> 16) & 0xffu);
  const auto ab = static_cast<int>((a >> 8) & 0xffu);
  const auto br = static_cast<int>((b >> 24) & 0xffu);
  const auto bg = static_cast<int>((b >> 16) & 0xffu);
  const auto bb = static_cast<int>((b >> 8) & 0xffu);
  const auto r = static_cast<std::uint8_t>(ar + static_cast<int>((br - ar) * t));
  const auto g = static_cast<std::uint8_t>(ag + static_cast<int>((bg - ag) * t));
  const auto blue = static_cast<std::uint8_t>(ab + static_cast<int>((bb - ab) * t));
  return pack_rgba(r, g, blue);
}

float face_shade(Vec3 normal) {
  if (normal.y > 0.5f) {
    return 1.0f;
  }
  if (normal.y < -0.5f) {
    return 0.46f;
  }
  if (normal.x > 0.5f || normal.z > 0.5f) {
    return 0.78f;
  }
  return 0.64f;
}

PackedColor color_for(VoxelType type, Vec3 normal, int world_x, int world_z) {
  const float patch = 0.88f + hash01(world_x / 4, world_z / 4, 0xbad5eedu) * 0.24f;
  const float shade = face_shade(normal) * patch;
  const float clearing = moon_clearing_influence(static_cast<float>(world_x), static_cast<float>(world_z));
  PackedColor color = pack_rgba(255, 0, 255);
  switch (type) {
    case VoxelType::Grass:
      if (normal.y > 0.5f) {
        color = hash01(world_x / 8, world_z / 8, 0x395638u) > 0.58f
            ? pack_rgba(49, 67, 59)
            : pack_rgba(57, 86, 56);
        color = mix_rgb(color, pack_rgba(96, 117, 109), clearing * 0.45f);
        break;
      }
      color = pack_rgba(63, 80, 48);
      break;
    case VoxelType::Dirt:
      color = pack_rgba(90, 74, 51);
      break;
    case VoxelType::Stone:
      color = pack_rgba(91, 102, 92);
      break;
    case VoxelType::Bark:
      color = pack_rgba(138, 93, 60);
      break;
    case VoxelType::Leaves:
      color = pack_rgba(52, 122, 73);
      break;
    case VoxelType::Air:
      return pack_rgba(0, 0, 0, 0);
  }
  return shaded(static_cast<std::uint8_t>((color >> 24) & 0xffu),
                static_cast<std::uint8_t>((color >> 16) & 0xffu),
                static_cast<std::uint8_t>((color >> 8) & 0xffu),
                shade);
}

bool is_tree_type(VoxelType type) {
  return type == VoxelType::Bark || type == VoxelType::Leaves;
}

VoxelType terrain_type_at_height(int y, int terrain_height) {
  if (y == terrain_height) {
    return VoxelType::Grass;
  }
  if (y > terrain_height - 3) {
    return VoxelType::Dirt;
  }
  return VoxelType::Stone;
}

int detail_cap(int base_cap, int visual_detail_level) {
  return visual_detail_level >= kNearVisualDetailLevel ? base_cap : std::max(1, base_cap / 3);
}

float detail_density_scale(int visual_detail_level) {
  return visual_detail_level >= kNearVisualDetailLevel ? 1.0f : 0.38f;
}

struct DetailBudget {
  int visual_detail_level = kNearVisualDetailLevel;
  int terrain_voxels = 0;
  int tree_voxels = 0;
};

bool allow_terrain_detail(DetailBudget& budget, int count = 1) {
  const int cap = detail_cap(MAX_TERRAIN_DETAIL_VOXELS_PER_CHUNK, budget.visual_detail_level);
  if (budget.terrain_voxels + count > cap) {
    return false;
  }
  budget.terrain_voxels += count;
  return true;
}

bool allow_tree_detail(DetailBudget& budget, int count = 1) {
  const int cap = detail_cap(MAX_TREE_DETAIL_VOXELS_PER_CHUNK, budget.visual_detail_level);
  if (budget.tree_voxels + count > cap) {
    return false;
  }
  budget.tree_voxels += count;
  return true;
}

PackedColor varied_bark_color(int world_x, int y, int world_z) {
  const float variant = hash01_3(world_x, y, world_z, 0x6261726bu);
  PackedColor base = variant > 0.68f ? pack_rgba(118, 78, 49) : pack_rgba(138, 93, 60);
  if (variant < 0.18f) {
    base = pack_rgba(91, 59, 39);
  }
  return base;
}

PackedColor varied_leaf_color(int world_x, int y, int world_z, int variant) {
  const float roll = hash01_3(world_x + variant * 17, y - variant * 7, world_z + variant * 13, 0x1ea7d15cu);
  PackedColor base = roll > 0.66f ? pack_rgba(45, 107, 65) : pack_rgba(52, 122, 73);
  if (roll < 0.22f) {
    base = pack_rgba(68, 137, 77);
  }
  if (roll > 0.90f) {
    base = pack_rgba(40, 89, 61);
  }
  return base;
}

void emit_top_tile(Mesh& mesh, float min_x, float y, float min_z, float max_x, float max_z, PackedColor color) {
  const Index start = static_cast<Index>(mesh.vertices.size());
  const std::array<Vec3, 4> vertices = {{
      {min_x, y, min_z},
      {min_x, y, max_z},
      {max_x, y, max_z},
      {max_x, y, min_z},
  }};
  const std::array<Vec3, 4> micro = {{
      {-0.5f, 0.5f, -0.5f},
      {-0.5f, 0.5f, 0.5f},
      {0.5f, 0.5f, 0.5f},
      {0.5f, 0.5f, -0.5f},
  }};
  const PackedColor shaded_color = shaded(static_cast<std::uint8_t>((color >> 24) & 0xffu),
                                          static_cast<std::uint8_t>((color >> 16) & 0xffu),
                                          static_cast<std::uint8_t>((color >> 8) & 0xffu),
                                          face_shade({0.0f, 1.0f, 0.0f}),
                                          static_cast<std::uint8_t>(color & 0xffu));
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    mesh.vertices.push_back(vertices[i]);
    mesh.normals.push_back({0.0f, 1.0f, 0.0f});
    mesh.colors.push_back(shaded_color);
    mesh.micro_positions.push_back(micro[i]);
  }
  mesh.indices.push_back(start + 0);
  mesh.indices.push_back(start + 1);
  mesh.indices.push_back(start + 2);
  mesh.indices.push_back(start + 0);
  mesh.indices.push_back(start + 2);
  mesh.indices.push_back(start + 3);
}

void emit_face(Mesh& mesh, int x, int y, int z, const Face& face, VoxelType type) {
  const Index start = static_cast<Index>(mesh.vertices.size());
  const PackedColor color = color_for(type, face.normal, x, z);

  for (const Vec3& corner : face.corners) {
    mesh.vertices.push_back({static_cast<float>(x) + corner.x,
                             static_cast<float>(y) + corner.y,
                             static_cast<float>(z) + corner.z});
    mesh.normals.push_back(face.normal);
    mesh.colors.push_back(color);
    mesh.micro_positions.push_back({corner.x - 0.5f, corner.y - 0.5f, corner.z - 0.5f});
  }

  mesh.indices.push_back(start + 0);
  mesh.indices.push_back(start + 1);
  mesh.indices.push_back(start + 2);
  mesh.indices.push_back(start + 0);
  mesh.indices.push_back(start + 2);
  mesh.indices.push_back(start + 3);
}

void append_box(Mesh& mesh, const Box& box) {
  for (const Face& face : kFaces) {
    const Index start = static_cast<Index>(mesh.vertices.size());
    const PackedColor color = shaded(static_cast<std::uint8_t>((box.color >> 24) & 0xffu),
                                     static_cast<std::uint8_t>((box.color >> 16) & 0xffu),
                                     static_cast<std::uint8_t>((box.color >> 8) & 0xffu),
                                     face_shade(face.normal),
                                     static_cast<std::uint8_t>(box.color & 0xffu));
    for (const Vec3& corner : face.corners) {
      const Vec3 position = {
        box.min.x + (box.max.x - box.min.x) * corner.x,
        box.min.y + (box.max.y - box.min.y) * corner.y,
        box.min.z + (box.max.z - box.min.z) * corner.z,
      };
      mesh.vertices.push_back(position);
      mesh.normals.push_back(face.normal);
      mesh.colors.push_back(color);
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

void append_oriented_box(Mesh& mesh, const Box& box, Vec3 origin, float heading) {
  for (const Face& face : kFaces) {
    const Index start = static_cast<Index>(mesh.vertices.size());
    const Vec3 normal = rotate_y(face.normal, heading);
    const PackedColor color = shaded(static_cast<std::uint8_t>((box.color >> 24) & 0xffu),
                                     static_cast<std::uint8_t>((box.color >> 16) & 0xffu),
                                     static_cast<std::uint8_t>((box.color >> 8) & 0xffu),
                                     face_shade(normal),
                                     static_cast<std::uint8_t>(box.color & 0xffu));
    for (const Vec3& corner : face.corners) {
      const Vec3 local = {
        box.min.x + (box.max.x - box.min.x) * corner.x,
        box.min.y + (box.max.y - box.min.y) * corner.y,
        box.min.z + (box.max.z - box.min.z) * corner.z,
      };
      const Vec3 position = origin + rotate_y(local, heading);
      mesh.vertices.push_back(position);
      mesh.normals.push_back(normal);
      mesh.colors.push_back(color);
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

void append_local_box(Mesh& mesh, Vec3 origin, float heading, Vec3 min, Vec3 max, PackedColor color) {
  append_oriented_box(mesh, {min, max, color}, origin, heading);
}

void append_local_box_with_transform(Mesh& mesh,
                                     Vec3 origin,
                                     float heading,
                                     Vec3 min,
                                     Vec3 max,
                                     PackedColor color,
                                     Vec3 pivot,
                                     float local_yaw,
                                     float local_pitch,
                                     float local_roll) {
  const Box box = {min, max, color};
  for (const Face& face : kFaces) {
    const Index start = static_cast<Index>(mesh.vertices.size());
    const Vec3 local_normal = rotate_z(rotate_y(rotate_x(face.normal, local_pitch), local_yaw), local_roll);
    const Vec3 normal = rotate_y(local_normal, heading);
    const PackedColor shaded_color = shaded(static_cast<std::uint8_t>((box.color >> 24) & 0xffu),
                                            static_cast<std::uint8_t>((box.color >> 16) & 0xffu),
                                            static_cast<std::uint8_t>((box.color >> 8) & 0xffu),
                                            face_shade(normal),
                                            static_cast<std::uint8_t>(box.color & 0xffu));
    for (const Vec3& corner : face.corners) {
      const Vec3 local = {
        box.min.x + (box.max.x - box.min.x) * corner.x,
        box.min.y + (box.max.y - box.min.y) * corner.y,
        box.min.z + (box.max.z - box.min.z) * corner.z,
      };
      const Vec3 posed = pivot + rotate_z(rotate_y(rotate_x(local - pivot, local_pitch), local_yaw), local_roll);
      const Vec3 position = origin + rotate_y(posed, heading);
      mesh.vertices.push_back(position);
      mesh.normals.push_back(normal);
      mesh.colors.push_back(shaded_color);
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

void append_surface_tiles(Mesh& mesh,
                          const TerrainGenerator& generator,
                          int world_x,
                          int y,
                          int world_z,
                          VoxelType type,
                          int visual_detail_level) {
  const int subdivisions = std::max(1, static_cast<int>(std::round(WORLD_LOGICAL_CELL_SIZE /
                                                                   TERRAIN_VISUAL_VOXEL_SCALE)));
  const float tile = WORLD_LOGICAL_CELL_SIZE / static_cast<float>(subdivisions);
  const float top_y = static_cast<float>(y) + WORLD_LOGICAL_CELL_SIZE;
  const bool near_detail = visual_detail_level >= kNearVisualDetailLevel;
  const float clearing = moon_clearing_influence(static_cast<float>(world_x), static_cast<float>(world_z));

  for (int sz = 0; sz < subdivisions; ++sz) {
    for (int sx = 0; sx < subdivisions; ++sx) {
      const int detail_x = world_x * subdivisions + sx;
      const int detail_z = world_z * subdivisions + sz;
      const float roll = hash01(detail_x, detail_z, 0x715facedu);
      const float height_step = near_detail && roll > 0.74f
          ? (roll > 0.92f ? TERRAIN_VISUAL_VOXEL_SCALE * 0.20f : TERRAIN_VISUAL_VOXEL_SCALE * 0.10f)
          : 0.0f;
      const float min_x = static_cast<float>(world_x) + static_cast<float>(sx) * tile;
      const float min_z = static_cast<float>(world_z) + static_cast<float>(sz) * tile;
      PackedColor color = color_for(type, {0.0f, 1.0f, 0.0f}, detail_x, detail_z);

      if (type == VoxelType::Grass) {
        if (roll < 0.08f) {
          color = mix_rgb(color, pack_rgba(82, 69, 48), 0.58f);
        } else if (roll > 0.86f) {
          color = mix_rgb(color, pack_rgba(75, 106, 67), 0.42f);
        }
        color = mix_rgb(color, pack_rgba(96, 117, 109), clearing * 0.18f);
      }

      if (height_step > 0.001f) {
        append_box(mesh, {{min_x, top_y, min_z},
                          {min_x + tile, top_y + height_step, min_z + tile},
                          color});
      } else {
        emit_top_tile(mesh, min_x, top_y, min_z, min_x + tile, min_z + tile, color);
      }
    }
  }
}

void append_surface_detail(Mesh& mesh,
                           const TerrainGenerator& generator,
                           int world_x,
                           int y,
                           int world_z,
                           DetailBudget& budget) {
  if (budget.visual_detail_level < kNearVisualDetailLevel) {
    return;
  }
  const float density = TERRAIN_SURFACE_DETAIL_DENSITY * detail_density_scale(budget.visual_detail_level);
  const float roll = hash01(world_x, world_z, 0x5a7faceu);
  if (roll > density || !allow_terrain_detail(budget)) {
    return;
  }

  const float top_y = static_cast<float>(y) + WORLD_LOGICAL_CELL_SIZE;
  const float ox = 0.18f + hash01(world_x + 17, world_z - 13, 0x5a7faceu) * 0.62f;
  const float oz = 0.18f + hash01(world_x - 23, world_z + 29, 0x5a7faceu) * 0.62f;
  const float x = static_cast<float>(world_x) + ox;
  const float z = static_cast<float>(world_z) + oz;

  if (roll < density * 0.26f) {
    const float size = 0.16f + hash01(world_x + 5, world_z - 7, 0x73746f6eu) * 0.13f;
    append_box(mesh, {{x - size, top_y + 0.01f, z - size * 0.82f},
                      {x + size, top_y + 0.11f + size * 0.18f, z + size * 0.82f},
                      pack_rgba(91, 102, 92)});
  } else if (roll < density * 0.60f) {
    const bool along_x = hash01(world_x - 11, world_z + 19, 0x726f6f74u) > 0.5f;
    const float length = 0.46f + hash01(world_x + 31, world_z - 37, 0x726f6f74u) * 0.34f;
    const float half_width = 0.055f;
    if (along_x) {
      append_box(mesh, {{x - length * 0.5f, top_y + 0.012f, z - half_width},
                        {x + length * 0.5f, top_y + 0.095f, z + half_width},
                        pack_rgba(99, 64, 39)});
    } else {
      append_box(mesh, {{x - half_width, top_y + 0.012f, z - length * 0.5f},
                        {x + half_width, top_y + 0.095f, z + length * 0.5f},
                        pack_rgba(99, 64, 39)});
    }
  } else {
    const float size = 0.24f + hash01(world_x + 41, world_z - 43, 0x6d6f7373u) * 0.18f;
    append_box(mesh, {{x - size * 0.5f, top_y + 0.006f, z - size * 0.5f},
                      {x + size * 0.5f, top_y + 0.030f, z + size * 0.5f},
                      pack_rgba(54, 92, 57)});
  }

  (void)generator;
}

void append_ledge_breakup(Mesh& mesh,
                          int world_x,
                          int y,
                          int world_z,
                          const Face& face,
                          DetailBudget& budget) {
  if (!LEDGE_BREAKUP_ENABLED || budget.visual_detail_level < kNearVisualDetailLevel) {
    return;
  }
  const float density = TERRAIN_SURFACE_DETAIL_DENSITY * 0.72f;
  if (hash01(world_x + face.dx * 7, world_z + face.dz * 11, 0x1ed9eb0u) > density ||
      !allow_terrain_detail(budget)) {
    return;
  }

  const float top_y = static_cast<float>(y) + 0.58f;
  const float edge_jitter = 0.18f + hash01(world_x - face.dx * 13, world_z - face.dz * 17, 0x1ed9eb0u) * 0.48f;
  Vec3 min = {static_cast<float>(world_x) + 0.35f, top_y, static_cast<float>(world_z) + 0.35f};
  Vec3 max = {static_cast<float>(world_x) + 0.65f, top_y + 0.25f, static_cast<float>(world_z) + 0.65f};

  if (face.dx > 0) {
    min.x = static_cast<float>(world_x) + 0.82f;
    max.x = static_cast<float>(world_x) + 1.04f;
    min.z = static_cast<float>(world_z) + edge_jitter;
    max.z = min.z + 0.24f;
  } else if (face.dx < 0) {
    min.x = static_cast<float>(world_x) - 0.04f;
    max.x = static_cast<float>(world_x) + 0.18f;
    min.z = static_cast<float>(world_z) + edge_jitter;
    max.z = min.z + 0.24f;
  } else if (face.dz > 0) {
    min.z = static_cast<float>(world_z) + 0.82f;
    max.z = static_cast<float>(world_z) + 1.04f;
    min.x = static_cast<float>(world_x) + edge_jitter;
    max.x = min.x + 0.24f;
  } else {
    min.z = static_cast<float>(world_z) - 0.04f;
    max.z = static_cast<float>(world_z) + 0.18f;
    min.x = static_cast<float>(world_x) + edge_jitter;
    max.x = min.x + 0.24f;
  }

  append_box(mesh, {min, max, pack_rgba(82, 70, 48)});
}

void append_tree_box(Mesh& mesh, DetailBudget& budget, Vec3 min, Vec3 max, PackedColor color) {
  if (!allow_tree_detail(budget)) {
    return;
  }
  append_box(mesh, {min, max, color});
}

void append_root_detail(Mesh& mesh,
                        const TerrainGenerator& generator,
                        int world_x,
                        int y,
                        int world_z,
                        DetailBudget& budget) {
  if (!ROOT_DETAIL_ENABLED || budget.visual_detail_level < kNearVisualDetailLevel) {
    return;
  }

  const std::array<Vec3, 4> directions = {{
      {1.0f, 0.0f, 0.0f},
      {-1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, -1.0f},
  }};
  for (std::size_t i = 0; i < directions.size(); ++i) {
    const Vec3 dir = directions[i];
    const float roll = hash01_3(world_x + static_cast<int>(i) * 19,
                                y,
                                world_z - static_cast<int>(i) * 23,
                                0x726f6f74u);
    if (roll > 0.42f * TREE_TRUNK_DETAIL_DENSITY || !allow_tree_detail(budget)) {
      continue;
    }
    const int sample_x = world_x + static_cast<int>(dir.x);
    const int sample_z = world_z + static_cast<int>(dir.z);
    const float root_y = static_cast<float>(generator.terrain_height(sample_x, sample_z)) + WORLD_LOGICAL_CELL_SIZE + 0.03f;
    const float length = 0.54f + roll * 0.52f;
    const float width = 0.08f + roll * 0.04f;
    const float center_x = static_cast<float>(world_x) + 0.5f;
    const float center_z = static_cast<float>(world_z) + 0.5f;
    if (dir.x != 0.0f) {
      const float start_x = center_x + dir.x * 0.20f;
      const float end_x = center_x + dir.x * length;
      append_box(mesh, {{std::min(start_x, end_x), root_y, center_z - width},
                        {std::max(start_x, end_x), root_y + 0.14f, center_z + width},
                        pack_rgba(105, 68, 42)});
    } else {
      const float start_z = center_z + dir.z * 0.20f;
      const float end_z = center_z + dir.z * length;
      append_box(mesh, {{center_x - width, root_y, std::min(start_z, end_z)},
                        {center_x + width, root_y + 0.14f, std::max(start_z, end_z)},
                        pack_rgba(105, 68, 42)});
    }
  }
}

void append_bark_visual(Mesh& mesh,
                        const TerrainGenerator& generator,
                        int world_x,
                        int y,
                        int world_z,
                        bool is_base,
                        DetailBudget& budget) {
  const float center_x = static_cast<float>(world_x) + 0.5f;
  const float center_z = static_cast<float>(world_z) + 0.5f;
  const float jitter_x = (hash01_3(world_x, y, world_z, 0x7472756eu) - 0.5f) * 0.035f;
  const float jitter_z = (hash01_3(world_x, y, world_z, 0x74727a65u) - 0.5f) * 0.035f;
  const float radius_x = 0.30f + hash01_3(world_x, y, world_z, 0x62726b31u) * 0.035f;
  const float radius_z = 0.30f + hash01_3(world_x, y, world_z, 0x62726b32u) * 0.035f;
  append_tree_box(mesh,
                  budget,
                  {center_x + jitter_x - radius_x, static_cast<float>(y), center_z + jitter_z - radius_z},
                  {center_x + jitter_x + radius_x, static_cast<float>(y) + WORLD_LOGICAL_CELL_SIZE, center_z + jitter_z + radius_z},
                  varied_bark_color(world_x, y, world_z));

  if (is_base) {
    append_root_detail(mesh, generator, world_x, y, world_z, budget);
  }
}

void append_leaf_visual(Mesh& mesh,
                        int world_x,
                        int y,
                        int world_z,
                        bool exposed,
                        DetailBudget& budget) {
  const float hole_roll = hash01_3(world_x, y, world_z, 0x67617073u);
  if (!exposed || hole_roll > 0.985f || !allow_tree_detail(budget)) {
    return;
  }

  const float inset = 0.08f + hash01_3(world_x, y, world_z, 0x1eafc1u) * 0.04f;
  append_box(mesh, {{static_cast<float>(world_x) + inset,
                    static_cast<float>(y) + inset,
                    static_cast<float>(world_z) + inset},
                    {static_cast<float>(world_x) + 1.0f - inset,
                    static_cast<float>(y) + 1.0f - inset,
                    static_cast<float>(world_z) + 1.0f - inset},
                    varied_leaf_color(world_x, y, world_z, 0)});
}

void append_rock(Mesh& mesh, const TerrainGenerator& generator, int world_x, int world_z, float seed) {
  const int blocks = 1 + static_cast<int>(seed * 4.0f);
  const std::array<PackedColor, 3> colors = {{
      pack_rgba(115, 123, 112),
      pack_rgba(91, 102, 92),
      pack_rgba(113, 130, 104),
  }};

  for (int block = 0; block < blocks; ++block) {
    const float ox = (hash01(world_x + block * 11, world_z + 3, 0x726f636bu) - 0.5f) * 0.9f;
    const float oz = (hash01(world_x - 2, world_z + block * 13, 0x726f636bu) - 0.5f) * 0.9f;
    const float size = 0.45f + hash01(world_x + block * 17, world_z - block * 19, 0x726f636bu) * 0.45f;
    const float y = static_cast<float>(generator.terrain_height(world_x + static_cast<int>(ox), world_z + static_cast<int>(oz))) + 0.08f + block * 0.05f;
    const float x = static_cast<float>(world_x) + ox;
    const float z = static_cast<float>(world_z) + oz;
    append_box(mesh, {{x - size * 0.5f, y, z - size * 0.5f},
                      {x + size * 0.5f, y + size * 0.55f, z + size * 0.5f},
                      colors[(static_cast<int>(seed * 10.0f) + block) % colors.size()]});
  }
}

void append_stump(Mesh& mesh, const TerrainGenerator& generator, int world_x, int world_z, float seed) {
  const float height = 0.45f + hash01(world_x + 23, world_z - 17, 0x7374756du) * 0.35f;
  const float y = static_cast<float>(generator.terrain_height(world_x, world_z));
  const float x = static_cast<float>(world_x);
  const float z = static_cast<float>(world_z);
  append_box(mesh, {{x - 0.29f, y, z - 0.29f}, {x + 0.29f, y + height, z + 0.29f}, pack_rgba(138, 93, 60)});
  append_box(mesh, {{x - 0.25f, y + height, z - 0.25f}, {x + 0.25f, y + height + 0.08f, z + 0.25f}, pack_rgba(101, 66, 41)});
  (void)seed;
}

void append_log(Mesh& mesh, const TerrainGenerator& generator, int world_x, int world_z, float seed) {
  const int length = 3 + static_cast<int>(hash01(world_x - 31, world_z + 29, 0x6c6f6775) * 4.0f);
  const bool along_x = seed > 0.5f;
  for (int block = 0; block < length; ++block) {
    const float centered = static_cast<float>(block) - static_cast<float>(length - 1) * 0.5f;
    const int bx = world_x + (along_x ? static_cast<int>(std::round(centered)) : 0);
    const int bz = world_z + (along_x ? 0 : static_cast<int>(std::round(centered)));
    const float y = static_cast<float>(generator.terrain_height(bx, bz)) + 0.16f;
    const float x = static_cast<float>(bx);
    const float z = static_cast<float>(bz);
    append_box(mesh, {{x - 0.36f, y, z - 0.36f}, {x + 0.36f, y + 0.5f, z + 0.36f}, pack_rgba(138, 93, 60)});
  }
}

bool dressing_origin_for_grid(int grid_x, int grid_z, int& world_x, int& world_z, float& seed) {
  seed = hash01(grid_x, grid_z, 0xdec042u);
  const float ox = (hash01(grid_x + 300, grid_z - 300, 0xdec042u) - 0.5f) * 3.6f;
  const float oz = (hash01(grid_x - 300, grid_z + 300, 0xdec042u) - 0.5f) * 3.6f;
  world_x = grid_x + static_cast<int>(std::round(ox));
  world_z = grid_z + static_cast<int>(std::round(oz));
  return moon_clearing_influence(static_cast<float>(world_x), static_cast<float>(world_z)) <= 0.18f;
}

bool near_major_dressing_obstacle(int min_x,
                                  int min_z,
                                  int size_x,
                                  int size_z,
                                  float mushroom_x,
                                  float mushroom_z) {
  for (int x = min_x + 4; x < min_x + size_x - 4; x += kWorldDressingStep) {
    for (int z = min_z + 4; z < min_z + size_z - 4; z += kWorldDressingStep) {
      int world_x = 0;
      int world_z = 0;
      float seed = 0.0f;
      if (!dressing_origin_for_grid(x, z, world_x, world_z, seed) || seed <= 0.925f) {
        continue;
      }

      if (seed > 0.94f) {
        const float radius = seed > 0.965f ? 1.75f : 1.30f;
        if (distance_sq(mushroom_x, mushroom_z, static_cast<float>(world_x), static_cast<float>(world_z)) <
            radius * radius) {
          return true;
        }
        continue;
      }

      const int length = 3 + static_cast<int>(hash01(world_x - 31, world_z + 29, 0x6c6f6775) * 4.0f);
      const float half_length = static_cast<float>(length - 1) * 0.5f + 0.80f;
      const float dx = mushroom_x - static_cast<float>(world_x);
      const float dz = mushroom_z - static_cast<float>(world_z);
      if (seed > 0.5f) {
        if (std::abs(dx) <= half_length && std::abs(dz) <= kMushroomMinObstacleDistance) {
          return true;
        }
      } else if (std::abs(dz) <= half_length && std::abs(dx) <= kMushroomMinObstacleDistance) {
        return true;
      }
    }
  }
  return false;
}

bool already_has_nearby_mushroom(const std::vector<MushroomSpot>& placed,
                                 float x,
                                 float z,
                                 float min_spacing) {
  const float min_distance_sq = min_spacing * min_spacing;
  for (const MushroomSpot& spot : placed) {
    if (distance_sq(x, z, spot.x, spot.z) < min_distance_sq) {
      return true;
    }
  }
  return false;
}

bool can_place_mushroom_at(const TerrainGenerator& generator,
                           int min_x,
                           int min_z,
                           int size_x,
                           int size_z,
                           float mushroom_x,
                           float mushroom_z,
                           float scale,
                           float min_spacing,
                           const std::vector<MushroomSpot>& placed) {
  if (mushroom_x < static_cast<float>(min_x + 2) ||
      mushroom_x > static_cast<float>(min_x + size_x - 2) ||
      mushroom_z < static_cast<float>(min_z + 2) ||
      mushroom_z > static_cast<float>(min_z + size_z - 2)) {
    return false;
  }
  if (moon_clearing_influence(mushroom_x, mushroom_z) > 0.18f) {
    return false;
  }
  if (already_has_nearby_mushroom(placed, mushroom_x, mushroom_z, min_spacing)) {
    return false;
  }
  if (near_major_dressing_obstacle(min_x, min_z, size_x, size_z, mushroom_x, mushroom_z)) {
    return false;
  }

  const int cell_x = static_cast<int>(std::round(mushroom_x));
  const int cell_z = static_cast<int>(std::round(mushroom_z));
  const int ground_y = generator.terrain_height(cell_x, cell_z);
  if (!is_solid(generator.voxel_at(cell_x, ground_y, cell_z).type) ||
      is_solid(generator.voxel_at(cell_x, ground_y + 1, cell_z).type)) {
    return false;
  }

  const int ledge_radius = static_cast<int>(std::ceil(kMushroomMinObstacleDistance));
  for (int dz = -ledge_radius; dz <= ledge_radius; ++dz) {
    for (int dx = -ledge_radius; dx <= ledge_radius; ++dx) {
      const int local_ground_y = generator.terrain_height(cell_x + dx, cell_z + dz);
      if (std::abs(local_ground_y - ground_y) > kMushroomMaxLocalSlope) {
        return false;
      }
    }
  }

  const float footprint_radius = std::max(0.38f, 0.42f * scale);
  const int min_footprint_x = static_cast<int>(std::floor(mushroom_x - footprint_radius));
  const int max_footprint_x = static_cast<int>(std::floor(mushroom_x + footprint_radius));
  const int min_footprint_z = static_cast<int>(std::floor(mushroom_z - footprint_radius));
  const int max_footprint_z = static_cast<int>(std::floor(mushroom_z + footprint_radius));
  for (int z = min_footprint_z; z <= max_footprint_z; ++z) {
    for (int x = min_footprint_x; x <= max_footprint_x; ++x) {
      if (std::abs(generator.terrain_height(x, z) - ground_y) > kMushroomMaxLocalSlope) {
        return false;
      }
      for (int y = ground_y + 1; y <= ground_y + kMushroomHeadroomVoxels; ++y) {
        if (is_solid(generator.voxel_at(x, y, z).type)) {
          return false;
        }
      }
    }
  }

  const int obstacle_radius = static_cast<int>(std::ceil(kMushroomMinObstacleDistance));
  const float obstacle_distance_sq = kMushroomMinObstacleDistance * kMushroomMinObstacleDistance;
  const float tree_distance_sq = kMushroomMinTreeDistance * kMushroomMinTreeDistance;
  for (int dz = -obstacle_radius; dz <= obstacle_radius; ++dz) {
    for (int dx = -obstacle_radius; dx <= obstacle_radius; ++dx) {
      const float distance = static_cast<float>(dx * dx + dz * dz);
      if (distance > tree_distance_sq) {
        continue;
      }
      for (int y = ground_y + 1; y <= ground_y + 4; ++y) {
        const VoxelType type = generator.voxel_at(cell_x + dx, y, cell_z + dz).type;
        if (type == VoxelType::Bark) {
          return false;
        }
        if (distance <= obstacle_distance_sq && type != VoxelType::Air) {
          return false;
        }
      }
    }
  }

  return true;
}

void append_mushroom(Mesh& mesh, const TerrainGenerator& generator, float world_x, float world_z, float scale, float seed) {
  const std::array<PackedColor, 4> cap_colors = {{
      pack_rgba(217, 236, 255),
      pack_rgba(184, 100, 82),
      pack_rgba(143, 255, 242),
      pack_rgba(111, 245, 197),
  }};
  const int cell_x = static_cast<int>(std::round(world_x));
  const int cell_z = static_cast<int>(std::round(world_z));
  const float y = static_cast<float>(generator.terrain_height(cell_x, cell_z)) + 1.0f;
  const PackedColor cap_color = seed > 0.94f
      ? cap_colors[3]
      : cap_colors[static_cast<std::size_t>(static_cast<int>(seed * 37.0f) % 3)];
  append_box(mesh, {{world_x - 0.06f * scale, y, world_z - 0.06f * scale},
                    {world_x + 0.06f * scale, y + 0.28f * scale, world_z + 0.06f * scale},
                    pack_rgba(240, 221, 184)});
  append_box(mesh, {{world_x - 0.20f * scale, y + 0.25f * scale, world_z - 0.20f * scale},
                    {world_x + 0.20f * scale, y + 0.43f * scale, world_z + 0.20f * scale},
                    cap_color});
}

float mushroom_scale_for(int world_x, int world_z, int variant) {
  const float size_seed = hash01(world_x + variant * 31, world_z - variant * 23, 0x6d757368u);
  if (hash01(world_x - variant * 11, world_z + variant * 17, 0x6d756267u) > 0.88f) {
    return 1.80f + size_seed * 0.75f;
  }
  return 1.15f + size_seed * 0.65f;
}

void append_charm(Mesh& mesh, const TerrainGenerator& generator, int world_x, int world_z) {
  const float y = static_cast<float>(generator.terrain_height(world_x, world_z)) + 1.15f;
  const float x = static_cast<float>(world_x);
  const float z = static_cast<float>(world_z);
  constexpr PackedColor cyan = pack_rgba(127, 255, 238);
  append_box(mesh, {{x - 0.12f, y - 0.48f, z - 0.12f}, {x + 0.12f, y + 0.48f, z + 0.12f}, cyan});
  append_box(mesh, {{x - 0.48f, y - 0.12f, z - 0.12f}, {x + 0.48f, y + 0.12f, z + 0.12f}, cyan});
  append_box(mesh, {{x - 0.18f, y - 0.18f, z - 0.18f}, {x + 0.18f, y + 0.18f, z + 0.18f}, pack_rgba(230, 255, 248)});
}

void append_world_dressing(Mesh& mesh, const TerrainGenerator& generator, int min_x, int min_z, int size_x, int size_z) {
  for (int x = min_x + 4; x < min_x + size_x - 4; x += kWorldDressingStep) {
    for (int z = min_z + 4; z < min_z + size_z - 4; z += kWorldDressingStep) {
      int world_x = 0;
      int world_z = 0;
      float seed = 0.0f;
      if (!dressing_origin_for_grid(x, z, world_x, world_z, seed)) {
        continue;
      }

      if (seed > 0.965f) {
        append_rock(mesh, generator, world_x, world_z, seed);
      } else if (seed > 0.94f) {
        append_stump(mesh, generator, world_x, world_z, seed);
      } else if (seed > 0.925f) {
        append_log(mesh, generator, world_x, world_z, seed);
      }
    }
  }

  std::vector<MushroomSpot> placed_mushrooms;
  placed_mushrooms.reserve(static_cast<std::size_t>((size_x / kMushroomCandidateStep + 1) *
                                                    (size_z / kMushroomCandidateStep + 1)));
  for (int x = min_x + 3; x < min_x + size_x - 3; x += kMushroomCandidateStep) {
    for (int z = min_z + 3; z < min_z + size_z - 3; z += kMushroomCandidateStep) {
      const float seed = hash01(x, z, 0x6d757368u);
      if (seed > kMushroomSpawnChance) {
        continue;
      }

      const float base_x = static_cast<float>(x) +
          (hash01(x + 211, z - 137, 0x6d757368u) - 0.5f) * 2.3f;
      const float base_z = static_cast<float>(z) +
          (hash01(x - 149, z + 197, 0x6d757368u) - 0.5f) * 2.3f;
      const bool cluster = hash01(x + 59, z - 83, 0x6d757368u) < kMushroomClusterChance;
      const int count = cluster
          ? 2 + static_cast<int>(hash01(x - 71, z + 43, 0x6d757368u) * 4.0f)
          : 1;

      for (int i = 0; i < count; ++i) {
        float mushroom_x = base_x;
        float mushroom_z = base_z;
        if (cluster) {
          const float angle = hash01(x + i * 17, z - i * 29, 0x6d757368u) * 6.2831853f;
          const float radius = (0.35f + hash01(x - i * 31, z + i * 23, 0x6d757368u) * 0.65f) *
              kMushroomClusterRadius;
          mushroom_x += std::cos(angle) * radius;
          mushroom_z += std::sin(angle) * radius;
        }

        const int cell_x = static_cast<int>(std::round(mushroom_x));
        const int cell_z = static_cast<int>(std::round(mushroom_z));
        const float scale = mushroom_scale_for(cell_x, cell_z, i);
        const float min_spacing = cluster ? kClusterMushroomMinSpacing : kMushroomMinSpacing;
        if (!can_place_mushroom_at(generator,
                                   min_x,
                                   min_z,
                                   size_x,
                                   size_z,
                                   mushroom_x,
                                   mushroom_z,
                                   scale,
                                   min_spacing,
                                   placed_mushrooms)) {
          continue;
        }

        const float color_seed = hash01(cell_x + i * 13, cell_z - i * 19, 0x6d757368u);
        append_mushroom(mesh, generator, mushroom_x, mushroom_z, scale, color_seed);
        placed_mushrooms.push_back({mushroom_x, mushroom_z});
      }
    }
  }

  if (min_x <= static_cast<int>(kMoonClearingX) && static_cast<int>(kMoonClearingX) < min_x + size_x &&
      min_z <= static_cast<int>(kMoonClearingZ) && static_cast<int>(kMoonClearingZ) < min_z + size_z) {
    append_charm(mesh, generator, static_cast<int>(kMoonClearingX), static_cast<int>(kMoonClearingZ));
  }
}

}  // namespace

Mesh build_chunk_mesh(const Chunk& chunk) {
  Mesh mesh;
  mesh.vertices.reserve(8192);
  mesh.normals.reserve(8192);
  mesh.colors.reserve(8192);
  mesh.micro_positions.reserve(8192);
  mesh.indices.reserve(12288);

  for (int y = 0; y < kChunkSize; ++y) {
    for (int z = 0; z < kChunkSize; ++z) {
      for (int x = 0; x < kChunkSize; ++x) {
        const Voxel voxel = chunk.get(x, y, z);
        if (!is_solid(voxel.type)) {
          continue;
        }

        for (const Face& face : kFaces) {
          const Voxel neighbor = chunk.get(x + face.dx, y + face.dy, z + face.dz);
          if (!is_solid(neighbor.type)) {
            emit_face(mesh, x, y, z, face, voxel.type);
          }
        }
      }
    }
  }

  return mesh;
}

class WorldMeshSampleCache {
 public:
  WorldMeshSampleCache(const TerrainGenerator& generator, int min_x, int min_z, int size_x, int size_z)
      : min_x_(min_x),
        min_z_(min_z),
        padded_size_x_(size_x + 2),
        padded_size_z_(size_z + 2),
        heights_(static_cast<std::size_t>(padded_size_x_ * padded_size_z_)) {
    for (int pz = 0; pz < padded_size_z_; ++pz) {
      for (int px = 0; px < padded_size_x_; ++px) {
        const int world_x = min_x_ + px - 1;
        const int world_z = min_z_ + pz - 1;
        heights_[height_index(px, pz)] = generator.terrain_height(world_x, world_z);
      }
    }
  }

  VoxelType render_voxel_type_at(const TerrainGenerator& generator, int world_x, int y, int world_z) const {
    if (y < 0) {
      return VoxelType::Stone;
    }
    if (y >= kChunkSize) {
      return VoxelType::Air;
    }

    const int height = terrain_height(generator, world_x, world_z);
    if (y <= height) {
      return terrain_type_at_height(y, height);
    }

    return generator.voxel_at(world_x, y, world_z).type;
  }

  VoxelType tree_voxel_type_at(const TerrainGenerator& generator, int world_x, int y, int world_z) const {
    if (y < 0 || y >= kChunkSize || y <= terrain_height(generator, world_x, world_z)) {
      return VoxelType::Air;
    }
    return generator.voxel_at(world_x, y, world_z).type;
  }

  int terrain_height(const TerrainGenerator& generator, int world_x, int world_z) const {
    const int px = world_x - min_x_ + 1;
    const int pz = world_z - min_z_ + 1;
    if (px < 0 || px >= padded_size_x_ || pz < 0 || pz >= padded_size_z_) {
      return generator.terrain_height(world_x, world_z);
    }
    return heights_[height_index(px, pz)];
  }

 private:
  std::size_t height_index(int px, int pz) const {
    return static_cast<std::size_t>(px + padded_size_x_ * pz);
  }

  int min_x_ = 0;
  int min_z_ = 0;
  int padded_size_x_ = 0;
  int padded_size_z_ = 0;
  std::vector<int> heights_;
};

Mesh build_world_mesh(const TerrainGenerator& generator,
                      int min_x,
                      int min_z,
                      int size_x,
                      int size_z,
                      int visual_detail_level) {
  Mesh mesh;
  const std::size_t base_reserve = static_cast<std::size_t>(size_x * size_z * 18);
  mesh.vertices.reserve(base_reserve);
  mesh.normals.reserve(base_reserve);
  mesh.colors.reserve(base_reserve);
  mesh.micro_positions.reserve(base_reserve);
  mesh.indices.reserve(static_cast<std::size_t>(size_x * size_z * 28));

  DetailBudget detail_budget = {};
  detail_budget.visual_detail_level = visual_detail_level;
  const WorldMeshSampleCache cache(generator, min_x, min_z, size_x, size_z);

  for (int z = min_z; z < min_z + size_z; ++z) {
    for (int x = min_x; x < min_x + size_x; ++x) {
      const int height = cache.terrain_height(generator, x, z);
      if (height < 0 || height >= kChunkSize) {
        continue;
      }

      append_surface_tiles(mesh, generator, x, height, z, VoxelType::Grass, visual_detail_level);
      const bool terrain_top_under_tree =
          is_tree_type(cache.tree_voxel_type_at(generator, x, height + 1, z));
      if (!terrain_top_under_tree) {
        append_surface_detail(mesh, generator, x, height, z, detail_budget);
      }

      for (const Face& face : kFaces) {
        if (face.dy != 0) {
          continue;
        }

        const int neighbor_height = cache.terrain_height(generator, x + face.dx, z + face.dz);
        if (neighbor_height >= height) {
          continue;
        }

        const int min_side_y = std::max(neighbor_height + 1, height - kMaxVisibleTerrainSideDepth + 1);
        for (int y = height; y >= min_side_y && y >= 0; --y) {
          if (is_solid(cache.render_voxel_type_at(generator, x + face.dx, y, z + face.dz))) {
            continue;
          }

          const VoxelType type = terrain_type_at_height(y, height);
          emit_face(mesh, x, y, z, face, type);
          if (type == VoxelType::Grass) {
            append_ledge_breakup(mesh, x, y, z, face, detail_budget);
          }
        }
      }
    }
  }

  for (int z = min_z; z < min_z + size_z; ++z) {
    for (int x = min_x; x < min_x + size_x; ++x) {
      const int height = cache.terrain_height(generator, x, z);
      const int min_tree_y = std::max(0, height + 1);
      for (int y = min_tree_y; y < kChunkSize; ++y) {
        const VoxelType type = cache.tree_voxel_type_at(generator, x, y, z);
        if (!is_tree_type(type)) {
          continue;
        }

        if (type == VoxelType::Bark) {
          const bool is_base =
              cache.render_voxel_type_at(generator, x, y - 1, z) != VoxelType::Bark;
          append_bark_visual(mesh, generator, x, y, z, is_base, detail_budget);
          continue;
        }

        bool exposed = false;
        for (const Face& face : kFaces) {
          if (!is_tree_type(cache.render_voxel_type_at(generator,
                                                       x + face.dx,
                                                       y + face.dy,
                                                       z + face.dz))) {
            exposed = true;
            break;
          }
        }
        append_leaf_visual(mesh, x, y, z, exposed, detail_budget);
      }
    }
  }

  append_world_dressing(mesh, generator, min_x, min_z, size_x, size_z);
  return mesh;
}

Vec3 owl_perch_position(const TerrainGenerator& generator) {
  const float x = kOwlLandmarkX + kOwlPerchOffset.x;
  const float z = kOwlLandmarkZ + kOwlPerchOffset.z;
  const float y = sample_terrain_height(generator, x, z) + kOwlPerchHeight;
  return {x, y, z};
}

void append_owl_perch_mesh(Mesh& mesh, Vec3 owl_position, float heading_radians) {
  append_local_box(mesh, owl_position, heading_radians,
                   {-0.21f, -kOwlPerchHeight, -0.21f},
                   {0.21f, 0.02f, 0.21f},
                   pack_rgba(138, 93, 60));
  append_local_box(mesh, owl_position, heading_radians,
                   {-0.75f, -0.23f, -0.13f},
                   {0.75f, 0.01f, 0.13f},
                   pack_rgba(138, 93, 60));
}

void append_owl_mesh(Mesh& mesh,
                     Vec3 owl_position,
                     float heading_radians,
                     float wing_pose,
                     float head_yaw,
                     float head_pitch,
                     float head_roll,
                     float body_bob,
                     float blink) {
  const float flap = std::max(0.0f, std::min(1.0f, wing_pose));
  const float left_wing_lift = flap * 0.34f;
  const float right_wing_lift = flap * 0.34f;
  const float eye_open = 1.0f - std::max(0.0f, std::min(1.0f, blink)) * 0.82f;
  const float eye_center_y = 1.225f;
  const float eye_half_height = 0.065f * eye_open;
  const Vec3 posed_position = owl_position + Vec3{0.0f, body_bob, 0.0f};
  const Vec3 head_pivot = {0.0f, 1.03f, -0.03f};

  append_local_box(mesh, posed_position, heading_radians,
                   {-0.41f, 0.0f, -0.31f},
                   {0.41f, 1.02f, 0.31f},
                   pack_rgba(95, 75, 58));
  append_local_box(mesh, posed_position, heading_radians,
                   {-0.72f, 0.04f + left_wing_lift, -0.27f},
                   {-0.42f, 0.86f + left_wing_lift, 0.27f},
                   pack_rgba(63, 56, 54));
  append_local_box(mesh, posed_position, heading_radians,
                   {0.42f, 0.04f + right_wing_lift, -0.27f},
                   {0.72f, 0.86f + right_wing_lift, 0.27f},
                   pack_rgba(63, 56, 54));
  append_local_box_with_transform(mesh, posed_position, heading_radians,
                                  {-0.43f, 0.84f, -0.29f},
                                  {0.43f, 1.46f, 0.29f},
                                  pack_rgba(95, 75, 58),
                                  head_pivot,
                                  head_yaw,
                                  head_pitch,
                                  head_roll);
  append_local_box_with_transform(mesh, posed_position, heading_radians,
                                  {-0.34f, 0.98f, -0.37f},
                                  {0.34f, 1.32f, -0.29f},
                                  pack_rgba(216, 206, 178),
                                  head_pivot,
                                  head_yaw,
                                  head_pitch,
                                  head_roll);
  append_local_box_with_transform(mesh, posed_position, heading_radians,
                                  {-0.07f, 0.98f, -0.48f},
                                  {0.07f, 1.12f, -0.36f},
                                  pack_rgba(255, 185, 67),
                                  head_pivot,
                                  head_yaw,
                                  head_pitch,
                                  head_roll);
  append_local_box_with_transform(mesh, posed_position, heading_radians,
                                  {-0.25f, eye_center_y - eye_half_height, -0.43f},
                                  {-0.12f, eye_center_y + eye_half_height, -0.38f},
                                  pack_rgba(143, 255, 242),
                                  head_pivot,
                                  head_yaw,
                                  head_pitch,
                                  head_roll);
  append_local_box_with_transform(mesh, posed_position, heading_radians,
                                  {0.12f, eye_center_y - eye_half_height, -0.43f},
                                  {0.25f, eye_center_y + eye_half_height, -0.38f},
                                  pack_rgba(143, 255, 242),
                                  head_pivot,
                                  head_yaw,
                                  head_pitch,
                                  head_roll);
}

constexpr float FIREFLY_CORE_SIZE = 0.28f;
constexpr float FIREFLY_HALO_SIZE = 0.58f;
constexpr float FIREFLY_EMISSIVE_INTENSITY = 0.34f;
constexpr float FIREFLY_TWINKLE_EMISSIVE_BOOST = 0.66f;
constexpr float FIREFLY_TWINKLE_SCALE_DEPTH = 0.28f;
constexpr float LANTERN_SCALE = 1.38f;
constexpr float LANTERN_GLOW_RADIUS = 1.55f;
constexpr float LANTERN_UNLIT_EMISSIVE = 0.50f;
constexpr float LANTERN_LIT_EMISSIVE = 1.0f;

std::uint8_t emissive_alpha(float intensity) {
  intensity = std::max(0.0f, std::min(1.0f, intensity));
  return static_cast<std::uint8_t>(255.0f * (1.0f - intensity));
}

void append_firefly_mesh(Mesh& mesh, Vec3 position, float glow_intensity, bool carried) {
  const float glow = std::max(0.0f, std::min(1.0f, glow_intensity));
  const float emissive = std::min(1.0f, FIREFLY_EMISSIVE_INTENSITY +
                                           FIREFLY_TWINKLE_EMISSIVE_BOOST * glow);
  const float pulse_scale = 1.0f - FIREFLY_TWINKLE_SCALE_DEPTH + FIREFLY_TWINKLE_SCALE_DEPTH * glow;
  const float size_scale = (carried ? 0.78f : 0.92f) * pulse_scale;
  const float core = FIREFLY_CORE_SIZE * size_scale;
  const float halo = FIREFLY_HALO_SIZE * (0.44f + glow * 0.34f) * size_scale;
  const float wing = 0.16f * size_scale;
  const PackedColor core_color = pack_rgba(255,
                                           static_cast<std::uint8_t>(205 + 38 * glow),
                                           static_cast<std::uint8_t>(58 + 84 * glow),
                                           emissive_alpha(emissive));
  const PackedColor halo_color = pack_rgba(static_cast<std::uint8_t>(150 + 54 * glow),
                                           static_cast<std::uint8_t>(214 + 41 * glow),
                                           static_cast<std::uint8_t>(70 + 96 * glow),
                                           emissive_alpha(emissive * 0.82f));
  const PackedColor wing_color = pack_rgba(static_cast<std::uint8_t>(118 + 42 * glow),
                                           static_cast<std::uint8_t>(215 + 40 * glow),
                                           static_cast<std::uint8_t>(198 + 36 * glow),
                                           emissive_alpha(emissive * 0.58f));

  // Keep the mote readable while limiting per-frame dynamic geometry.
  append_box(mesh, {{position.x - core, position.y - core, position.z - core},
                    {position.x + core, position.y + core, position.z + core},
                    core_color});
  append_box(mesh, {{position.x - halo, position.y - halo * 0.72f, position.z - halo * 0.72f},
                    {position.x + halo, position.y + halo * 0.72f, position.z + halo * 0.72f},
                    halo_color});
  append_box(mesh, {{position.x - core - wing, position.y + 0.06f, position.z - wing},
                    {position.x - core * 0.45f, position.y + 0.22f, position.z + wing},
                    wing_color});
  append_box(mesh, {{position.x + core * 0.45f, position.y + 0.06f, position.z - wing},
                    {position.x + core + wing, position.y + 0.22f, position.z + wing},
                    wing_color});
}

void append_squirrel_mesh(Mesh& mesh,
                          Vec3 ground_center,
                          float heading_radians,
                          float tail_pose,
                          float head_pose,
                          float hop_pose,
                          bool happy) {
  const float hop = std::max(0.0f, std::min(1.0f, hop_pose)) * 0.18f;
  const float tail = std::max(-1.0f, std::min(1.0f, tail_pose));
  const float head = std::max(-1.0f, std::min(1.0f, head_pose));
  const float y = hop;
  const PackedColor fur = pack_rgba(154, 93, 48);
  const PackedColor warm_fur = pack_rgba(183, 112, 58);
  const PackedColor dark_fur = pack_rgba(95, 58, 39);
  const PackedColor cream = pack_rgba(219, 180, 123);
  const PackedColor black = pack_rgba(22, 19, 16);
  (void)happy;

  append_local_box(mesh, ground_center, heading_radians,
                   {-0.36f, y + 0.16f, -0.42f},
                   {0.36f, y + 0.78f, 0.34f},
                   fur);
  append_local_box(mesh, ground_center, heading_radians,
                   {-0.22f, y + 0.22f, 0.24f},
                   {0.22f, y + 0.67f, 0.40f},
                   cream);
  append_local_box(mesh, ground_center, heading_radians + head * 0.22f,
                   {-0.29f, y + 0.70f, 0.08f},
                   {0.29f, y + 1.13f, 0.54f},
                   warm_fur);
  append_local_box(mesh, ground_center, heading_radians + head * 0.22f,
                   {-0.16f, y + 0.78f, 0.48f},
                   {0.16f, y + 0.98f, 0.70f},
                   cream);
  append_local_box(mesh, ground_center, heading_radians + head * 0.22f,
                   {-0.20f, y + 1.07f, 0.16f},
                   {-0.02f, y + 1.34f, 0.34f},
                   dark_fur);
  append_local_box(mesh, ground_center, heading_radians + head * 0.22f,
                   {0.02f, y + 1.07f, 0.16f},
                   {0.20f, y + 1.34f, 0.34f},
                   dark_fur);
  append_local_box(mesh, ground_center, heading_radians + head * 0.22f,
                   {-0.21f, y + 0.91f, 0.50f},
                   {-0.09f, y + 1.03f, 0.60f},
                   black);
  append_local_box(mesh, ground_center, heading_radians + head * 0.22f,
                   {0.09f, y + 0.91f, 0.50f},
                   {0.21f, y + 1.03f, 0.60f},
                   black);

  append_local_box(mesh, ground_center, heading_radians,
                   {-0.34f, y + 0.00f, -0.26f},
                   {-0.10f, y + 0.22f, 0.10f},
                   dark_fur);
  append_local_box(mesh, ground_center, heading_radians,
                   {0.10f, y + 0.00f, -0.26f},
                   {0.34f, y + 0.22f, 0.10f},
                   dark_fur);
  append_local_box(mesh, ground_center, heading_radians,
                   {-0.28f, y + 0.08f, 0.20f},
                   {-0.08f, y + 0.36f, 0.44f},
                   dark_fur);
  append_local_box(mesh, ground_center, heading_radians,
                   {0.08f, y + 0.08f, 0.20f},
                   {0.28f, y + 0.36f, 0.44f},
                   dark_fur);

  append_local_box(mesh, ground_center, heading_radians + tail * 0.15f,
                   {-0.28f, y + 0.42f, -0.92f},
                   {0.28f, y + 1.28f, -0.40f},
                   warm_fur);
  append_local_box(mesh, ground_center, heading_radians + tail * 0.20f,
                   {-0.22f, y + 1.10f, -1.12f},
                   {0.22f, y + 1.70f, -0.70f},
                   warm_fur);
  append_local_box(mesh, ground_center, heading_radians + tail * 0.24f,
                   {-0.18f, y + 1.46f, -0.94f},
                   {0.18f, y + 1.86f, -0.54f},
                   cream);

}

void append_heart_mesh(Mesh& mesh, Vec3 position, float scale, float pulse) {
  const float s = scale * (0.86f + std::max(0.0f, std::min(1.0f, pulse)) * 0.14f);
  const PackedColor deep = pack_rgba(174, 47, 63);
  const PackedColor bright = pack_rgba(222, 75, 91);
  const PackedColor highlight = pack_rgba(245, 130, 136);

  append_box(mesh, {{position.x - 0.30f * s, position.y + 0.18f * s, position.z - 0.08f * s},
                    {position.x - 0.04f * s, position.y + 0.44f * s, position.z + 0.08f * s},
                    bright});
  append_box(mesh, {{position.x + 0.04f * s, position.y + 0.18f * s, position.z - 0.08f * s},
                    {position.x + 0.30f * s, position.y + 0.44f * s, position.z + 0.08f * s},
                    bright});
  append_box(mesh, {{position.x - 0.38f * s, position.y - 0.02f * s, position.z - 0.08f * s},
                    {position.x + 0.38f * s, position.y + 0.24f * s, position.z + 0.08f * s},
                    deep});
  append_box(mesh, {{position.x - 0.25f * s, position.y - 0.25f * s, position.z - 0.08f * s},
                    {position.x + 0.25f * s, position.y + 0.02f * s, position.z + 0.08f * s},
                    deep});
  append_box(mesh, {{position.x - 0.11f * s, position.y - 0.46f * s, position.z - 0.08f * s},
                    {position.x + 0.11f * s, position.y - 0.20f * s, position.z + 0.08f * s},
                    deep});
  append_box(mesh, {{position.x - 0.22f * s, position.y + 0.24f * s, position.z - 0.10f * s},
                    {position.x - 0.12f * s, position.y + 0.34f * s, position.z + 0.10f * s},
                    highlight});
}

void append_acorn_mesh(Mesh& mesh, Vec3 position, float phase, float readability) {
  const float nearby = std::max(0.0f, std::min(1.0f, readability));
  const float scale = 1.12f + nearby * 0.10f;
  const float sway_x = std::sin(phase * 2.7f) * nearby * 0.026f;
  const float sway_z = std::cos(phase * 2.1f) * nearby * 0.020f;
  const float cap_sway_x = std::sin(phase * 2.7f + 0.7f) * nearby * 0.018f;
  const float cap_sway_z = std::cos(phase * 2.1f + 0.5f) * nearby * 0.014f;
  const float readable_pulse = nearby * (0.35f + 0.65f * (std::sin(phase * 4.2f) * 0.5f + 0.5f));
  const PackedColor shell = mix_rgb(pack_rgba(187, 111, 43), pack_rgba(210, 132, 55), readable_pulse * 0.24f);
  const PackedColor shell_shadow = pack_rgba(137, 74, 35);
  const PackedColor cap = pack_rgba(78, 63, 38, 255);
  const PackedColor cap_highlight =
      mix_rgb(pack_rgba(122, 93, 50), pack_rgba(174, 129, 61), readable_pulse * 0.55f);
  const PackedColor stem = pack_rgba(63, 49, 32, 255);

  append_box(mesh, {{position.x - 0.19f * scale + sway_x, position.y + 0.04f * scale, position.z - 0.17f * scale + sway_z},
                    {position.x + 0.19f * scale + sway_x, position.y + 0.34f * scale, position.z + 0.17f * scale + sway_z},
                    shell});
  append_box(mesh, {{position.x - 0.11f * scale + sway_x, position.y - 0.01f * scale, position.z - 0.10f * scale + sway_z},
                    {position.x + 0.11f * scale + sway_x, position.y + 0.08f * scale, position.z + 0.10f * scale + sway_z},
                    shell_shadow});
  append_box(mesh, {{position.x - 0.23f * scale + cap_sway_x, position.y + 0.29f * scale, position.z - 0.20f * scale + cap_sway_z},
                    {position.x + 0.23f * scale + cap_sway_x, position.y + 0.47f * scale, position.z + 0.20f * scale + cap_sway_z},
                    cap});
  append_box(mesh, {{position.x - 0.16f * scale + cap_sway_x, position.y + 0.42f * scale, position.z - 0.22f * scale + cap_sway_z},
                    {position.x + 0.02f * scale + cap_sway_x, position.y + 0.49f * scale, position.z - 0.16f * scale + cap_sway_z},
                    cap_highlight});
  append_box(mesh, {{position.x - 0.05f * scale + cap_sway_x, position.y + 0.45f * scale, position.z - 0.05f * scale + cap_sway_z},
                    {position.x + 0.05f * scale + cap_sway_x, position.y + 0.63f * scale, position.z + 0.05f * scale + cap_sway_z},
                    stem});
}

void append_lantern_mesh(Mesh& mesh,
                         Vec3 position,
                         int deposited_fireflies,
                         int required_fireflies,
                         bool lit,
                         float glow_intensity) {
  const float glow = std::max(0.0f, std::min(1.0f, glow_intensity));
  const float s = LANTERN_SCALE;
  const float fill = required_fireflies > 0
      ? std::max(0.0f, std::min(1.0f, static_cast<float>(deposited_fireflies) /
                                      static_cast<float>(required_fireflies)))
      : 1.0f;
  const PackedColor wood = pack_rgba(112, 70, 42);
  const PackedColor dark_frame = pack_rgba(37, 44, 39);
  const PackedColor dark_glass = pack_rgba(34, 57, 62);
  const PackedColor dim_glass = pack_rgba(58,
                                          static_cast<std::uint8_t>(82 + 72 * glow),
                                          static_cast<std::uint8_t>(92 + 82 * glow),
                                          emissive_alpha(LANTERN_UNLIT_EMISSIVE * 0.45f));
  const PackedColor ember = pack_rgba(static_cast<std::uint8_t>(116 + 139 * fill),
                                      static_cast<std::uint8_t>(92 + 143 * fill),
                                      static_cast<std::uint8_t>(50 + 106 * fill),
                                      emissive_alpha(LANTERN_UNLIT_EMISSIVE + fill * 0.38f));
  const PackedColor lit_core = pack_rgba(255, 184, 72, emissive_alpha(LANTERN_LIT_EMISSIVE));
  const PackedColor glow_shell = pack_rgba(255,
                                           static_cast<std::uint8_t>(132 + 72 * glow),
                                           static_cast<std::uint8_t>(42 + 72 * glow),
                                           emissive_alpha((lit ? 0.82f : 0.35f) + glow * 0.22f));
  const PackedColor lamp_color = lit ? lit_core : ember;
  const float light_shell = (lit ? LANTERN_GLOW_RADIUS : 0.44f + fill * 0.28f + glow * 0.36f) * s;
  const float y = position.y;

  append_box(mesh, {{position.x - 0.48f * s, y, position.z - 0.48f * s},
                    {position.x + 0.48f * s, y + 0.22f * s, position.z + 0.48f * s},
                    wood});
  append_box(mesh, {{position.x - 0.28f * s, y + 0.22f * s, position.z - 0.28f * s},
                    {position.x + 0.28f * s, y + 0.52f * s, position.z + 0.28f * s},
                    wood});

  append_box(mesh, {{position.x - 0.16f * s, y + 0.45f * s, position.z - 0.16f * s},
                    {position.x + 0.16f * s, y + 2.05f * s, position.z + 0.16f * s},
                    wood});
  append_box(mesh, {{position.x - 0.40f * s, y + 1.98f * s, position.z - 0.18f * s},
                    {position.x + 0.40f * s, y + 2.14f * s, position.z + 0.18f * s},
                    wood});

  append_box(mesh, {{position.x - 0.52f * s, y + 0.86f * s, position.z - 0.52f * s},
                    {position.x + 0.52f * s, y + 1.02f * s, position.z + 0.52f * s},
                    dark_frame});
  append_box(mesh, {{position.x - 0.52f * s, y + 1.72f * s, position.z - 0.52f * s},
                    {position.x + 0.52f * s, y + 1.88f * s, position.z + 0.52f * s},
                    dark_frame});
  append_box(mesh, {{position.x - 0.62f * s, y + 1.88f * s, position.z - 0.62f * s},
                    {position.x + 0.62f * s, y + 2.04f * s, position.z + 0.62f * s},
                    wood});
  append_box(mesh, {{position.x - 0.42f * s, y + 2.04f * s, position.z - 0.42f * s},
                    {position.x + 0.42f * s, y + 2.24f * s, position.z + 0.42f * s},
                    dark_frame});
  append_box(mesh, {{position.x - 0.18f * s, y + 2.22f * s, position.z - 0.18f * s},
                    {position.x + 0.18f * s, y + 2.42f * s, position.z + 0.18f * s},
                    wood});

  const float post = 0.11f * s;
  const float min_y = y + 0.98f * s;
  const float max_y = y + 1.82f * s;
  append_box(mesh, {{position.x - 0.55f * s, min_y, position.z - 0.55f * s},
                    {position.x - 0.55f * s + post, max_y, position.z - 0.55f * s + post},
                    dark_frame});
  append_box(mesh, {{position.x + 0.55f * s - post, min_y, position.z - 0.55f * s},
                    {position.x + 0.55f * s, max_y, position.z - 0.55f * s + post},
                    dark_frame});
  append_box(mesh, {{position.x - 0.55f * s, min_y, position.z + 0.55f * s - post},
                    {position.x - 0.55f * s + post, max_y, position.z + 0.55f * s},
                    dark_frame});
  append_box(mesh, {{position.x + 0.55f * s - post, min_y, position.z + 0.55f * s - post},
                    {position.x + 0.55f * s, max_y, position.z + 0.55f * s},
                    dark_frame});

  append_box(mesh, {{position.x - 0.38f * s, y + 1.07f * s, position.z - 0.38f * s},
                    {position.x + 0.38f * s, y + 1.65f * s, position.z + 0.38f * s},
                    dark_glass});
  append_box(mesh, {{position.x - 0.26f * s, y + 1.17f * s, position.z - 0.26f * s},
                    {position.x + 0.26f * s, y + 1.55f * s, position.z + 0.26f * s},
                    lit || fill > 0.0f ? lamp_color : dim_glass});
  append_box(mesh, {{position.x - 0.14f * s, y + 1.25f * s, position.z - 0.14f * s},
                    {position.x + 0.14f * s, y + 1.47f * s, position.z + 0.14f * s},
                    lamp_color});
  if (lit || glow > 0.05f || deposited_fireflies > 0) {
    append_box(mesh, {{position.x - light_shell * 0.46f, y + 1.18f * s, position.z - light_shell * 0.46f},
                      {position.x + light_shell * 0.46f, y + 1.56f * s, position.z + light_shell * 0.46f},
                      glow_shell});
    append_box(mesh, {{position.x - light_shell * 0.34f, y + 1.08f * s, position.z - light_shell * 0.34f},
                      {position.x + light_shell * 0.34f, y + 1.66f * s, position.z + light_shell * 0.34f},
                      glow_shell});
    append_box(mesh, {{position.x - 0.18f * s, y + 0.90f * s, position.z - 0.18f * s},
                      {position.x + 0.18f * s, y + 1.94f * s, position.z + 0.18f * s},
                      glow_shell});
  }
}

}  // namespace voxel
