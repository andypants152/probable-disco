#pragma once

#include <cstdint>

#include "world/chunk.h"

namespace voxel {

class TerrainGenerator {
 public:
  explicit TerrainGenerator(std::uint32_t seed = 1337);

  void generate(Chunk& chunk, int origin_x = 0, int origin_z = 0) const;
  Voxel voxel_at(int world_x, int y, int world_z) const;
  int terrain_height(int world_x, int world_z) const;

 private:
  Voxel tree_voxel_at(int world_x, int y, int world_z) const;
  bool tree_origin_for_cell(int cell_x, int cell_z, int& origin_x, int& origin_z) const;

  std::uint32_t seed_;
};

}  // namespace voxel
