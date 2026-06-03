#pragma once

#include "math/vec3.h"
#include "render/mesh.h"
#include "world/chunk.h"

namespace voxel {

class TerrainGenerator;

Mesh build_chunk_mesh(const Chunk& chunk);
Mesh build_world_mesh(const TerrainGenerator& generator, int min_x, int min_z, int size_x, int size_z);
Vec3 owl_perch_position(const TerrainGenerator& generator);
void append_owl_perch_mesh(Mesh& mesh, Vec3 owl_position, float heading_radians);
void append_owl_mesh(Mesh& mesh, Vec3 owl_position, float heading_radians, float wing_pose);
void append_firefly_mesh(Mesh& mesh, Vec3 position, float glow_intensity, bool carried);
void append_lantern_mesh(Mesh& mesh,
                         Vec3 position,
                         int deposited_fireflies,
                         int required_fireflies,
                         bool lit,
                         float glow_intensity);

}  // namespace voxel
