#include "generator.h"

#include <algorithm>
#include <array>

#include "art_scale.h"

namespace voxel {

namespace {

constexpr int kSpawnClearRadius = 10;
constexpr float kMoonClearingX = 42.0f;
constexpr float kMoonClearingZ = -104.0f;
constexpr float kMoonClearingRadius = 16.0f;
constexpr int kMaxBranchesPerTree = 5;
constexpr int kMaxLeafClumpsPerTree = 6;
constexpr int kLeafFalloffScale = 1024;
constexpr int kMinCanopyDy = -1;
constexpr std::uint32_t kLeafHashMask = 0xffffu;

struct TreeBranch {
  int dir_x;
  int dir_z;
  int start_dy;
  int length;
  int rise;
};

struct LeafClump {
  int center_x;
  int center_dy;
  int center_z;
  int radius_x;
  int radius_y;
  int radius_z;
  std::uint32_t edge_threshold;
};

struct TreeShape {
  int trunk_height;
  int canopy_radius;
  bool tall;
  bool asymmetric;
  bool broken;
  int lean_x;
  int lean_z;
  int broken_x;
  int broken_z;
  int branch_count;
  int clump_count;
  std::array<TreeBranch, kMaxBranchesPerTree> branches;
  std::array<LeafClump, kMaxLeafClumpsPerTree> clumps;
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

int clamp_i(int value, int min_value, int max_value) {
  return std::max(min_value, std::min(value, max_value));
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

void branch_direction(int index, int& dir_x, int& dir_z) {
  switch (index & 3) {
    case 0:
      dir_x = 1;
      dir_z = 0;
      break;
    case 1:
      dir_x = 0;
      dir_z = 1;
      break;
    case 2:
      dir_x = -1;
      dir_z = 0;
      break;
    case 3:
      dir_x = 0;
      dir_z = -1;
      break;
    default:
      dir_x = 1;
      dir_z = 0;
      break;
  }
}

int branch_step_y(const TreeBranch& branch, int step) {
  return branch.start_dy + (branch.rise * step + branch.length / 2) / branch.length;
}

TreeShape tree_shape_for(int tree_x, int tree_z, std::uint32_t seed) {
  const std::uint32_t h = hash2(tree_x, tree_z, seed ^ 0x7f4a7c15u);
  const int band_roll = static_cast<int>(h % 100u);
  TreeShape shape = {};
  if (band_roll < 18) {
    shape.trunk_height = TREE_SHORT_TRUNK_MIN_CELLS + static_cast<int>((h >> 8) % 3u);
    shape.canopy_radius = TREE_SHORT_LEAF_CLUMP_RADIUS_CELLS;
  } else if (band_roll < 66) {
    shape.trunk_height = TREE_MEDIUM_TRUNK_MIN_CELLS + static_cast<int>((h >> 8) % 4u);
    shape.canopy_radius = TREE_MEDIUM_LEAF_CLUMP_RADIUS_CELLS;
  } else {
    shape.trunk_height = TREE_TALL_TRUNK_MIN_CELLS + static_cast<int>((h >> 8) % 5u);
    shape.canopy_radius = TREE_TALL_LEAF_CLUMP_RADIUS_CELLS;
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

  shape.branch_count = (shape.tall ? 4 : 3) + static_cast<int>((h >> 24) % 2u);
  shape.clump_count = 3 + static_cast<int>((h >> 26) % 4u);

  for (int i = 0; i < shape.branch_count; ++i) {
    const std::uint32_t branch_hash = hash2(tree_x + i * 19, tree_z - i * 23, seed ^ 0x4272616eu);
    TreeBranch& branch = shape.branches[static_cast<std::size_t>(i)];
    branch_direction(static_cast<int>((branch_hash >> 3) + i * 2), branch.dir_x, branch.dir_z);
    branch.length = 2 + static_cast<int>((branch_hash >> 8) % (shape.tall ? 3u : 2u));
    branch.start_dy = -1 + static_cast<int>((branch_hash >> 12) % 3u);
    branch.rise = static_cast<int>((branch_hash >> 16) % (shape.tall ? 3u : 2u));
  }

  for (int i = 0; i < shape.clump_count; ++i) {
    const std::uint32_t clump_hash = hash2(tree_x - i * 31, tree_z + i * 37, seed ^ 0x1eafc1u);
    LeafClump& clump = shape.clumps[static_cast<std::size_t>(i)];
    int anchor_x = 0;
    int anchor_z = 0;
    int anchor_dy = 0;
    if (i > 0 && shape.branch_count > 0) {
      const TreeBranch& branch =
          shape.branches[static_cast<std::size_t>((i - 1) % shape.branch_count)];
      anchor_x = branch.dir_x * branch.length;
      anchor_z = branch.dir_z * branch.length;
      anchor_dy = branch_step_y(branch, branch.length);
    }

    const int canopy_bias_x = shape.asymmetric ? shape.lean_x : 0;
    const int canopy_bias_z = shape.asymmetric ? shape.lean_z : 0;
    clump.center_x = anchor_x + canopy_bias_x + signed_unit(clump_hash, 18);
    clump.center_z = anchor_z + canopy_bias_z + signed_unit(clump_hash, 20);
    clump.center_dy = std::max(0, anchor_dy) + static_cast<int>((clump_hash >> 22) % 3u);
    if (i == 0) {
      clump.center_x = canopy_bias_x;
      clump.center_z = canopy_bias_z;
      clump.center_dy = 1 + static_cast<int>((clump_hash >> 22) % 2u);
    }

    const int radius_bonus = i == 0 ? 1 : 0;
    clump.radius_x = clamp_i(shape.canopy_radius + radius_bonus +
                                 static_cast<int>((clump_hash >> 4) % 3u) - 1,
                             2,
                             shape.tall ? 5 : 4);
    clump.radius_z = clamp_i(shape.canopy_radius + radius_bonus +
                                 static_cast<int>((clump_hash >> 7) % 3u) - 1,
                             2,
                             shape.tall ? 5 : 4);
    clump.radius_y = 2 + static_cast<int>((clump_hash >> 10) % (shape.tall ? 3u : 2u));
    clump.edge_threshold = 36000u + ((clump_hash >> 13) & 0x1fffu);
  }

  return shape;
}

bool is_trunk_voxel(int dx, int dz, int y, int ground_y, int top_y) {
  return dx == 0 && dz == 0 && y >= ground_y + 1 && y <= top_y;
}

bool is_branch_voxel(const TreeShape& shape, int dx, int dy, int dz) {
  for (int i = 0; i < shape.branch_count; ++i) {
    const TreeBranch& branch = shape.branches[static_cast<std::size_t>(i)];
    for (int step = 1; step <= branch.length; ++step) {
      if (dx == branch.dir_x * step &&
          dz == branch.dir_z * step &&
          dy == branch_step_y(branch, step)) {
        return true;
      }
    }
  }
  return false;
}

bool is_bark_voxel_for_tree(const TreeShape& shape,
                            int ground_y,
                            int top_y,
                            int dx,
                            int y,
                            int dz) {
  const int dy = y - top_y;
  return is_trunk_voxel(dx, dz, y, ground_y, top_y) ||
         is_branch_voxel(shape, dx, dy, dz);
}

bool raw_leaf_candidate_for_tree(const TreeShape& shape,
                                 int world_x,
                                 int y,
                                 int world_z,
                                 int tree_x,
                                 int tree_z,
                                 int top_y,
                                 std::uint32_t seed) {
  const int dx = world_x - tree_x;
  const int dz = world_z - tree_z;
  const int dy = y - top_y;
  if (dy < kMinCanopyDy) {
    return false;
  }

  for (int i = 0; i < shape.clump_count; ++i) {
    const LeafClump& clump = shape.clumps[static_cast<std::size_t>(i)];
    const int local_x = dx - clump.center_x;
    const int local_y = dy - clump.center_dy;
    const int local_z = dz - clump.center_z;
    if (abs_i(local_x) > clump.radius_x ||
        abs_i(local_y) > clump.radius_y ||
        abs_i(local_z) > clump.radius_z) {
      continue;
    }

    const int scaled_distance =
        (local_x * local_x * kLeafFalloffScale) / (clump.radius_x * clump.radius_x) +
        (local_y * local_y * kLeafFalloffScale) / (clump.radius_y * clump.radius_y) +
        (local_z * local_z * kLeafFalloffScale) / (clump.radius_z * clump.radius_z);
    if (scaled_distance > kLeafFalloffScale) {
      continue;
    }

    if (shape.broken &&
        local_x * shape.broken_x + local_z * shape.broken_z >
            (clump.radius_x + clump.radius_z) / 3 &&
        scaled_distance > kLeafFalloffScale / 2) {
      continue;
    }

    std::uint32_t threshold = clump.edge_threshold;
    threshold += static_cast<std::uint32_t>(
        ((kLeafFalloffScale - scaled_distance) * (65000u - clump.edge_threshold)) /
        kLeafFalloffScale);
    if (scaled_distance < kLeafFalloffScale / 4) {
      threshold = 65535u;
    }

    const std::uint32_t roll =
        hash3(world_x + i * 29, y - i * 11, world_z + i * 17, seed ^ 0x1ea7d15cu) &
        kLeafHashMask;
    if (roll <= threshold) {
      return true;
    }
  }

  return false;
}

bool has_neighboring_bark_for_tree(const TreeShape& shape,
                                   int ground_y,
                                   int top_y,
                                   int dx,
                                   int y,
                                   int dz) {
  constexpr std::array<std::array<int, 3>, 6> kCardinalNeighbors = {{
      {{1, 0, 0}},
      {{-1, 0, 0}},
      {{0, 1, 0}},
      {{0, -1, 0}},
      {{0, 0, 1}},
      {{0, 0, -1}},
  }};

  for (const std::array<int, 3>& offset : kCardinalNeighbors) {
    if (is_bark_voxel_for_tree(shape,
                               ground_y,
                               top_y,
                               dx + offset[0],
                               y + offset[1],
                               dz + offset[2])) {
      return true;
    }
  }

  return false;
}

bool keep_leaf_after_isolation_filter(const TreeShape& shape,
                                      int world_x,
                                      int y,
                                      int world_z,
                                      int tree_x,
                                      int tree_z,
                                      int ground_y,
                                      int top_y,
                                      std::uint32_t seed) {
  if (!raw_leaf_candidate_for_tree(shape, world_x, y, world_z, tree_x, tree_z, top_y, seed)) {
    return false;
  }

  int neighboring_leaves = 0;
  for (int offset_y = -1; offset_y <= 1; ++offset_y) {
    for (int offset_z = -1; offset_z <= 1; ++offset_z) {
      for (int offset_x = -1; offset_x <= 1; ++offset_x) {
        if (offset_x == 0 && offset_y == 0 && offset_z == 0) {
          continue;
        }

        const int neighbor_x = world_x + offset_x;
        const int neighbor_y = y + offset_y;
        const int neighbor_z = world_z + offset_z;
        const int neighbor_dx = neighbor_x - tree_x;
        const int neighbor_dz = neighbor_z - tree_z;
        if (is_bark_voxel_for_tree(shape, ground_y, top_y, neighbor_dx, neighbor_y, neighbor_dz)) {
          continue;
        }
        if (raw_leaf_candidate_for_tree(shape,
                                        neighbor_x,
                                        neighbor_y,
                                        neighbor_z,
                                        tree_x,
                                        tree_z,
                                        top_y,
                                        seed)) {
          ++neighboring_leaves;
          if (neighboring_leaves >= 3) {
            return true;
          }
        }
      }
    }
  }

  const int dx = world_x - tree_x;
  const int dz = world_z - tree_z;
  return neighboring_leaves >= 2 &&
         has_neighboring_bark_for_tree(shape, ground_y, top_y, dx, y, dz);
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
      const int top_y = ground_y + shape.trunk_height;
      const int dx = world_x - tree_x;
      const int dz = world_z - tree_z;
      if (is_bark_voxel_for_tree(shape, ground_y, top_y, dx, y, dz)) {
        return {VoxelType::Bark};
      }
    }
  }

  for (int cell_z = base_cell_z - 1; cell_z <= base_cell_z + 1; ++cell_z) {
    for (int cell_x = base_cell_x - 1; cell_x <= base_cell_x + 1; ++cell_x) {
      int tree_x = 0;
      int tree_z = 0;
      if (!tree_origin_for_cell(cell_x, cell_z, tree_x, tree_z)) {
        continue;
      }

      const int ground_y = terrain_height(tree_x, tree_z);
      const TreeShape shape = tree_shape_for(tree_x, tree_z, seed_);
      const int top_y = ground_y + shape.trunk_height;
      if (keep_leaf_after_isolation_filter(shape,
                                           world_x,
                                           y,
                                           world_z,
                                           tree_x,
                                           tree_z,
                                           ground_y,
                                           top_y,
                                           seed_)) {
        return {VoxelType::Leaves};
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
