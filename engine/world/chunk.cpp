#include "chunk.h"

namespace voxel {

Chunk::Chunk() {
  clear();
}

void Chunk::clear() {
  for (Voxel& voxel : voxels_) {
    voxel.type = VoxelType::Air;
  }
}

bool Chunk::in_bounds(int x, int y, int z) const {
  return x >= 0 && x < kChunkSize &&
         y >= 0 && y < kChunkSize &&
         z >= 0 && z < kChunkSize;
}

Voxel Chunk::get(int x, int y, int z) const {
  if (!in_bounds(x, y, z)) {
    return {};
  }
  return voxels_[index(x, y, z)];
}

void Chunk::set(int x, int y, int z, VoxelType type) {
  if (!in_bounds(x, y, z)) {
    return;
  }
  voxels_[index(x, y, z)].type = type;
}

int Chunk::index(int x, int y, int z) {
  return x + kChunkSize * (z + kChunkSize * y);
}

}  // namespace voxel
