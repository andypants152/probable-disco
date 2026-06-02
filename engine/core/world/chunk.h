#pragma once

#include <array>

#include "world/voxel.h"

namespace voxel {

constexpr int kChunkSize = 32;
constexpr int kChunkVolume = kChunkSize * kChunkSize * kChunkSize;

class Chunk {
 public:
  Chunk();

  void clear();
  bool in_bounds(int x, int y, int z) const;
  Voxel get(int x, int y, int z) const;
  void set(int x, int y, int z, VoxelType type);

 private:
  static int index(int x, int y, int z);

  std::array<Voxel, kChunkVolume> voxels_;
};

}  // namespace voxel
