#pragma once

#include "math/vec3.h"
#include "render/mesh.h"
#include "world/chunk.h"

namespace voxel {

class TerrainGenerator;

constexpr int kMaxVisibleTerrainSideDepth = 6;

Mesh build_chunk_mesh(const Chunk& chunk);
Mesh build_world_mesh(const TerrainGenerator& generator,
                      int min_x,
                      int min_z,
                      int size_x,
                      int size_z,
                      int visual_detail_level = 2);
Vec3 owl_perch_position(const TerrainGenerator& generator);
void append_owl_perch_mesh(Mesh& mesh, Vec3 owl_position, float heading_radians);
void append_owl_mesh(Mesh& mesh,
                     Vec3 owl_position,
                     float heading_radians,
                     float wing_pose,
                     float head_yaw,
                     float head_pitch,
                     float head_roll,
                     float body_bob,
                     float blink);
void append_firefly_mesh(Mesh& mesh, Vec3 position, float glow_intensity, bool carried);
void append_squirrel_mesh(Mesh& mesh,
                          Vec3 ground_center,
                          float heading_radians,
                          float tail_pose,
                          float head_pose,
                          float hop_pose,
                          bool happy);
void append_heart_mesh(Mesh& mesh, Vec3 position, float scale, float pulse);
void append_acorn_mesh(Mesh& mesh, Vec3 position, float phase, float readability);
void append_burrow_mesh(Mesh& mesh, Vec3 position, float heading_radians);
void append_rabbit_mesh(Mesh& mesh, Vec3 ground_center, float heading_radians, float pop_progress);
void append_lantern_mesh(Mesh& mesh,
                         Vec3 position,
                         int deposited_fireflies,
                         int required_fireflies,
                         bool lit,
                         float glow_intensity);

}  // namespace voxel
