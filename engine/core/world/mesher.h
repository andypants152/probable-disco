#pragma once

#include "math/vec3.h"
#include "render/mesh.h"
#include "world/chunk.h"

namespace voxel {

class TerrainGenerator;

Mesh build_chunk_mesh(const Chunk& chunk);
Mesh build_world_mesh(const TerrainGenerator& generator, int min_x, int min_z, int size_x, int size_z);
Vec3 heart_position(const TerrainGenerator& generator);
Vec3 owl_perch_position(const TerrainGenerator& generator);

}  // namespace voxel
