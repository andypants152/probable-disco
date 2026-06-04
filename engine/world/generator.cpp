#include "generator.h"

#include <algorithm>

#include "art_scale.h"

namespace voxel {

namespace {

constexpr int kSpawnClearRadius = 10;
constexpr float kMoonClearingX = 42.0f;
constexpr float kMoonClearingZ = -104.0f;
constexpr float kMoonClearingRadius = 16.0f;
constexpr float kInnerLeafDensity = 0.85f;
constexpr float kShortOuterLeafDensity = 0.65f;
constexpr float kMediumOuterLeafDensity = 0.58f;
constexpr float kTallOuterLeafDensity = 0.50f;

struct TreeShape {
  int trunk_height;
  int canopy_radius;
  int canopy_layers;
  float inner_leaf_density;
  float outer_leaf_density;
  bool tall;
  bool asymmetric;
  bool broken;
  int lean_x;
  int lean_z;
  int broken_x;
  int broken_z;
};

std::uint32_t hash2(int x, int z, std::uint32_t seed) {
  std::uint32_t h = seed;
  h ^= static_cast<std::uint32_t>(x) * 0x8da6b343u;
  h ^= static_cast<std::uint32_t>(z) * 0xd8163841u;
  h ^= h >> 13;
  h *= 0x85ebca6bu;
  h ^= h >> 16;
  return h;
}

std::uint32_t hash3(int x, int y, int z, std::uint32_t seed) {
  std::uint32_t h = hash2(x, z, seed);
  h ^= static_cast<std::uint32_t>(y) * 0xcb1ab31fu;
  h ^= h >> 15;
  h *= 0x9e3779b1u;
  h ^= h >> 16;
  return h;
}

int signed_unit(std::uint32_t h, int shift) {
  const int value = static_cast<int>((h >> shift) % 3u) - 1;
  return value;
}

int abs_i(int value) {
  return value < 0 ? -value : value;
}

int floor_div(int value, int divisor) {
  int quotient = value / divisor;
  const int remainder = value % divisor;
  if (remainder < 0) {
    --quotient;
  }
  return quotient;
}

bool inside_moon_clearing(int world_x, int world_z) {
  const float dx = static_cast<float>(world_x) - kMoonClearingX;
  const float dz = static_cast<float>(world_z) - kMoonClearingZ;
  return dx * dx + dz * dz < kMoonClearingRadius * kMoonClearingRadius;
}

TreeShape tree_shape_for(int tree_x, int tree_z, std::uint32_t seed) {
  const std::uint32_t h = hash2(tree_x, tree_z, seed ^ 0x7f4a7c15u);
  const int band_roll = static_cast<int>(h % 100u);
  TreeShape shape = {};
  if (band_roll < 18) {
    shape.trunk_height = TREE_SHORT_TRUNK_MIN_CELLS + static_cast<int>((h >> 8) % 3u);
    shape.canopy_radius = TREE_SHORT_LEAF_CLUMP_RADIUS_CELLS;
    shape.canopy_layers = 2;
    shape.inner_leaf_density = kInnerLeafDensity;
    shape.outer_leaf_density = kShortOuterLeafDensity;
  } else if (band_roll < 66) {
    shape.trunk_height = TREE_MEDIUM_TRUNK_MIN_CELLS + static_cast<int>((h >> 8) % 4u);
    shape.canopy_radius = TREE_MEDIUM_LEAF_CLUMP_RADIUS_CELLS;
    shape.canopy_layers = 3;
    shape.inner_leaf_density = kInnerLeafDensity;
    shape.outer_leaf_density = kMediumOuterLeafDensity;
  } else {
    shape.trunk_height = TREE_TALL_TRUNK_MIN_CELLS + static_cast<int>((h >> 8) % 5u);
    shape.canopy_radius = TREE_TALL_LEAF_CLUMP_RADIUS_CELLS;
    shape.canopy_layers = 3;
    shape.inner_leaf_density = 0.78f;
    shape.outer_leaf_density = kTallOuterLeafDensity;
    shape.tall = true;
  }

  shape.asymmetric = ((h >> 14) % 100u) < 36u;
  shape.broken = ((h >> 21) % 100u) < 18u;
  shape.lean_x = signed_unit(h, 16);
  shape.lean_z = signed_unit(h, 18);
  if (shape.lean_x == 0 && shape.lean_z == 0 && shape.asymmetric) {
    shape.lean_x = (h & 1u) == 0u ? -1 : 1;
  }
  shape.broken_x = (h & 1u) == 0u ? -1 : 1;
  shape.broken_z = (h & 2u) == 0u ? -1 : 1;
  return shape;
}

int canopy_radius_for_layer(const TreeShape& shape, int dy) {
  if (dy <= -shape.canopy_layers || dy >= shape.canopy_layers) {
    return shape.tall ? 2 : 1;
  }
  if (dy > 0) {
    return shape.canopy_radius - 1;
  }
  return shape.canopy_radius;
}

float leaf_density_for(const TreeShape& shape, int distance, int radius) {
  if (distance <= std::max(1, radius - 1)) {
    return shape.inner_leaf_density;
  }
  return shape.outer_leaf_density;
}

bool keep_leaf_voxel(const TreeShape& shape,
                     int world_x,
                     int y,
                     int world_z,
                     int local_x,
                     int local_z,
                     int distance,
                     int radius,
                     std::uint32_t seed) {
  if (shape.broken && distance >= radius &&
      local_x * shape.broken_x + local_z * shape.broken_z > radius / 2) {
    return false;
  }

  const float density = leaf_density_for(shape, distance, radius);
  const std::uint32_t threshold = static_cast<std::uint32_t>(density * 65535.0f);
  return (hash3(world_x, y, world_z, seed ^ 0x1ea7d15cu) & 0xffffu) <= threshold;
}

}  // namespace

TerrainGenerator::TerrainGenerator(std::uint32_t seed) : seed_(seed) {}

void TerrainGenerator::generate(Chunk& chunk, int origin_x, int origin_z) const {
  chunk.clear();

  for (int y = 0; y < kChunkSize; ++y) {
    for (int z = 0; z < kChunkSize; ++z) {
      for (int x = 0; x < kChunkSize; ++x) {
        chunk.set(x, y, z, voxel_at(origin_x + x, y, origin_z + z).type);
      }
    }
  }
}

Voxel TerrainGenerator::voxel_at(int world_x, int y, int world_z) const {
  if (y < 0) {
    return {VoxelType::Stone};
  }
  if (y >= kChunkSize) {
    return {};
  }

  const int height = terrain_height(world_x, world_z);
  if (y <= height) {
    if (y == height) {
      return {VoxelType::Grass};
    }
    if (y > height - 3) {
      return {VoxelType::Dirt};
    }
    return {VoxelType::Stone};
  }

  return tree_voxel_at(world_x, y, world_z);
}

int TerrainGenerator::terrain_height(int world_x, int world_z) const {
  const int broad = static_cast<int>(hash2(floor_div(world_x, 16),
                                           floor_div(world_z, 16),
                                           seed_) % 3);
  const int detail = static_cast<int>(hash2(floor_div(world_x, 7),
                                            floor_div(world_z, 7),
                                            seed_ ^ 0x51ed270bu) % 2);
  return 8 + broad + detail;
}

Voxel TerrainGenerator::tree_voxel_at(int world_x, int y, int world_z) const {
  const int base_cell_x = floor_div(world_x, TREE_LAYOUT_CELL_SIZE);
  const int base_cell_z = floor_div(world_z, TREE_LAYOUT_CELL_SIZE);

  for (int cell_z = base_cell_z - 1; cell_z <= base_cell_z + 1; ++cell_z) {
    for (int cell_x = base_cell_x - 1; cell_x <= base_cell_x + 1; ++cell_x) {
      int tree_x = 0;
      int tree_z = 0;
      if (!tree_origin_for_cell(cell_x, cell_z, tree_x, tree_z)) {
        continue;
      }

      const int ground_y = terrain_height(tree_x, tree_z);
      const TreeShape shape = tree_shape_for(tree_x, tree_z, seed_);
      const int trunk_height = shape.trunk_height;
      const int top_y = ground_y + trunk_height;
      const int dx = world_x - tree_x;
      const int dz = world_z - tree_z;

      if (dx == 0 && dz == 0 && y >= ground_y + 1 && y <= top_y) {
        return {VoxelType::Bark};
      }

      for (int dy = -shape.canopy_layers; dy <= shape.canopy_layers; ++dy) {
        if (y != top_y + dy) {
          continue;
        }

        const int offset_x = shape.asymmetric && dy >= 0 ? shape.lean_x : 0;
        const int offset_z = shape.asymmetric && dy >= 0 ? shape.lean_z : 0;
        const int local_x = dx - offset_x;
        const int local_z = dz - offset_z;
        const int radius = canopy_radius_for_layer(shape, dy);
        const int distance = abs_i(local_x) + abs_i(local_z);
        if (distance <= radius &&
            keep_leaf_voxel(shape, world_x, y, world_z, local_x, local_z, distance, radius, seed_)) {
          return {VoxelType::Leaves};
        }
      }
    }
  }

  return {};
}

bool TerrainGenerator::tree_origin_for_cell(int cell_x, int cell_z, int& origin_x, int& origin_z) const {
  const std::uint32_t h = hash2(cell_x, cell_z, seed_ ^ 0xb5297a4du);
  if ((h % 100) > 64) {
    return false;
  }

  origin_x = cell_x * TREE_LAYOUT_CELL_SIZE + 2 + static_cast<int>((h >> 8) % 5);
  origin_z = cell_z * TREE_LAYOUT_CELL_SIZE + 2 + static_cast<int>((h >> 16) % 5);

  const int clear_distance = origin_x * origin_x + origin_z * origin_z;
  if (clear_distance < kSpawnClearRadius * kSpawnClearRadius) {
    return false;
  }
  if (inside_moon_clearing(origin_x, origin_z)) {
    return false;
  }

  return true;
}

}  // namespace voxel
