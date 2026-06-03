#pragma once

#include <cstdint>

namespace voxel {

enum class VoxelType : std::uint8_t {
  Air = 0,
  Grass,
  Dirt,
  Stone,
  Bark,
  Leaves,
};

struct Voxel {
  VoxelType type = VoxelType::Air;
};

inline bool is_solid(VoxelType type) {
  return type != VoxelType::Air;
}

}  // namespace voxel
