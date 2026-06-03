#include "world/mesher.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

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

void append_mushrooms(Mesh& mesh, const TerrainGenerator& generator, int world_x, int world_z, float seed) {
  const std::array<PackedColor, 3> cap_colors = {{
      pack_rgba(217, 236, 255),
      pack_rgba(184, 100, 82),
      pack_rgba(143, 255, 242),
  }};
  const int count = 4 + static_cast<int>(hash01(world_x + 61, world_z - 53, 0x6d757368u) * 5.0f);
  for (int i = 0; i < count; ++i) {
    const float ox = (hash01(world_x + i * 17, world_z + 71, 0x6d757368u) - 0.5f) * 2.6f;
    const float oz = (hash01(world_x - 67, world_z + i * 19, 0x6d757368u) - 0.5f) * 2.6f;
    const int bx = world_x + static_cast<int>(ox);
    const int bz = world_z + static_cast<int>(oz);
    const float y = static_cast<float>(generator.terrain_height(bx, bz));
    const float x = static_cast<float>(world_x) + ox;
    const float z = static_cast<float>(world_z) + oz;
    const float scale = 0.85f + hash01(world_x + i * 31, world_z - i * 23, 0x6d757368u) * 0.55f;
    append_box(mesh, {{x - 0.08f * scale, y, z - 0.08f * scale},
                      {x + 0.08f * scale, y + 0.34f * scale, z + 0.08f * scale},
                      pack_rgba(240, 221, 184)});
    append_box(mesh, {{x - 0.22f * scale, y + 0.31f * scale, z - 0.22f * scale},
                      {x + 0.22f * scale, y + 0.53f * scale, z + 0.22f * scale},
                      cap_colors[(static_cast<int>(seed * 100.0f) + i) % cap_colors.size()]});
  }
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
  for (int x = min_x + 4; x < min_x + size_x - 4; x += 6) {
    for (int z = min_z + 4; z < min_z + size_z - 4; z += 6) {
      const float seed = hash01(x, z, 0xdec042u);
      const float ox = (hash01(x + 300, z - 300, 0xdec042u) - 0.5f) * 3.6f;
      const float oz = (hash01(x - 300, z + 300, 0xdec042u) - 0.5f) * 3.6f;
      const int world_x = x + static_cast<int>(std::round(ox));
      const int world_z = z + static_cast<int>(std::round(oz));
      if (moon_clearing_influence(static_cast<float>(world_x), static_cast<float>(world_z)) > 0.18f) {
        continue;
      }

      if (seed > 0.965f) {
        append_rock(mesh, generator, world_x, world_z, seed);
      } else if (seed > 0.94f) {
        append_stump(mesh, generator, world_x, world_z, seed);
      } else if (seed > 0.925f) {
        append_log(mesh, generator, world_x, world_z, seed);
      } else if (seed < 0.075f) {
        append_mushrooms(mesh, generator, world_x, world_z, seed);
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
constexpr float FIREFLY_EMISSIVE_INTENSITY = 0.92f;
constexpr float FIREFLY_NEAR_PULSE_INTENSITY = 0.35f;
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
                                           FIREFLY_NEAR_PULSE_INTENSITY * (glow - 0.64f));
  const float size_scale = carried ? 0.78f : 0.92f;
  const float core = FIREFLY_CORE_SIZE * size_scale;
  const float halo = FIREFLY_HALO_SIZE * (0.58f + glow * 0.12f) * size_scale;
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
