#include "world/generator.h"

namespace voxel {

namespace {

constexpr int kTreeCellSize = 8;
constexpr int kSpawnClearRadius = 10;
constexpr float kMoonClearingX = 42.0f;
constexpr float kMoonClearingZ = -104.0f;
constexpr float kMoonClearingRadius = 16.0f;

std::uint32_t hash2(int x, int z, std::uint32_t seed) {
  std::uint32_t h = seed;
  h ^= static_cast<std::uint32_t>(x) * 0x8da6b343u;
  h ^= static_cast<std::uint32_t>(z) * 0xd8163841u;
  h ^= h >> 13;
  h *= 0x85ebca6bu;
  h ^= h >> 16;
  return h;
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
  const int base_cell_x = floor_div(world_x, kTreeCellSize);
  const int base_cell_z = floor_div(world_z, kTreeCellSize);

  for (int cell_z = base_cell_z - 1; cell_z <= base_cell_z + 1; ++cell_z) {
    for (int cell_x = base_cell_x - 1; cell_x <= base_cell_x + 1; ++cell_x) {
      int tree_x = 0;
      int tree_z = 0;
      if (!tree_origin_for_cell(cell_x, cell_z, tree_x, tree_z)) {
        continue;
      }

      const int ground_y = terrain_height(tree_x, tree_z);
      const int trunk_height = 5 + static_cast<int>(hash2(tree_x, tree_z, seed_) % 3);
      const int top_y = ground_y + trunk_height;
      const int dx = world_x - tree_x;
      const int dz = world_z - tree_z;

      if (dx == 0 && dz == 0 && y >= ground_y + 1 && y <= top_y) {
        return {VoxelType::Bark};
      }

      for (int dy = -2; dy <= 2; ++dy) {
        if (y != top_y + dy) {
          continue;
        }

        const int radius = dy > 0 ? 2 : 3;
        const int distance = abs_i(dx) + abs_i(dz) + (dy > 0 ? dy : 0);
        if (distance <= radius + 1) {
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

  origin_x = cell_x * kTreeCellSize + 2 + static_cast<int>((h >> 8) % 5);
  origin_z = cell_z * kTreeCellSize + 2 + static_cast<int>((h >> 16) % 5);

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
