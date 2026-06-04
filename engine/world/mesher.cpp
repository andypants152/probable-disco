#include "world/mesher.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "world/generator.h"

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
constexpr int kMushroomCandidateStep = 4;
constexpr float kMushroomSpawnChance = 0.24f;
constexpr float kMushroomClusterChance = 0.24f;
constexpr float kMushroomClusterRadius = 2.15f;
constexpr float kMushroomMinObstacleDistance = 1.65f;
constexpr float kMushroomMinTreeDistance = 1.75f;
constexpr float kMushroomMinSpacing = 0.95f;
constexpr float kClusterMushroomMinSpacing = 0.55f;
constexpr int kMushroomMaxLocalSlope = 1;
constexpr int kMushroomHeadroomVoxels = 2;

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
  const float y = static_cast<float>(generator.terrain_height(cell_x, cell_z));
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
  if (hash01(world_x - variant * 11, world_z + variant * 17, 0x6d756267u) > 0.94f) {
    return 1.05f + size_seed * 0.25f;
  }
  return 0.58f + size_seed * 0.38f;
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

Mesh build_world_mesh(const TerrainGenerator& generator, int min_x, int min_z, int size_x, int size_z) {
  Mesh mesh;
  mesh.vertices.reserve(static_cast<std::size_t>(size_x * size_z * 8));
  mesh.normals.reserve(static_cast<std::size_t>(size_x * size_z * 8));
  mesh.colors.reserve(static_cast<std::size_t>(size_x * size_z * 8));
  mesh.micro_positions.reserve(static_cast<std::size_t>(size_x * size_z * 8));
  mesh.indices.reserve(static_cast<std::size_t>(size_x * size_z * 12));

  for (int y = 0; y < kChunkSize; ++y) {
    for (int z = min_z; z < min_z + size_z; ++z) {
      for (int x = min_x; x < min_x + size_x; ++x) {
        const Voxel voxel = generator.voxel_at(x, y, z);
        if (!is_solid(voxel.type)) {
          continue;
        }

        for (const Face& face : kFaces) {
          const Voxel neighbor = generator.voxel_at(x + face.dx, y + face.dy, z + face.dz);
          if (!is_solid(neighbor.type)) {
            emit_face(mesh, x, y, z, face, voxel.type);
          }
        }
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

void append_owl_mesh(Mesh& mesh, Vec3 owl_position, float heading_radians, float wing_pose) {
  const float flap = std::max(0.0f, std::min(1.0f, wing_pose));
  const float left_wing_lift = flap * 0.34f;
  const float right_wing_lift = flap * 0.34f;

  append_local_box(mesh, owl_position, heading_radians,
                   {-0.41f, 0.0f, -0.31f},
                   {0.41f, 1.02f, 0.31f},
                   pack_rgba(95, 75, 58));
  append_local_box(mesh, owl_position, heading_radians,
                   {-0.72f, 0.04f + left_wing_lift, -0.27f},
                   {-0.42f, 0.86f + left_wing_lift, 0.27f},
                   pack_rgba(63, 56, 54));
  append_local_box(mesh, owl_position, heading_radians,
                   {0.42f, 0.04f + right_wing_lift, -0.27f},
                   {0.72f, 0.86f + right_wing_lift, 0.27f},
                   pack_rgba(63, 56, 54));
  append_local_box(mesh, owl_position, heading_radians,
                   {-0.43f, 0.84f, -0.29f},
                   {0.43f, 1.46f, 0.29f},
                   pack_rgba(95, 75, 58));
  append_local_box(mesh, owl_position, heading_radians,
                   {-0.34f, 0.98f, -0.37f},
                   {0.34f, 1.32f, -0.29f},
                   pack_rgba(216, 206, 178));
  append_local_box(mesh, owl_position, heading_radians,
                   {-0.07f, 0.98f, -0.48f},
                   {0.07f, 1.12f, -0.36f},
                   pack_rgba(255, 185, 67));
  append_local_box(mesh, owl_position, heading_radians,
                   {-0.25f, 1.16f, -0.43f},
                   {-0.12f, 1.29f, -0.38f},
                   pack_rgba(143, 255, 242));
  append_local_box(mesh, owl_position, heading_radians,
                   {0.12f, 1.16f, -0.43f},
                   {0.25f, 1.29f, -0.38f},
                   pack_rgba(143, 255, 242));
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
