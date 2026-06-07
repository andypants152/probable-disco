#pragma once

#include "math/vec3.h"
#include "render/mesh.h"

namespace voxel {

struct FoxAnimationPose {
  float walk_blend = 0.0f;
  float walk_cycle = 0.0f;
  float body_bob = 0.0f;
  float body_pitch = 0.0f;
  float head_bob = 0.0f;
  float tail_sway = 0.0f;
  float ear_twitch = 0.0f;
};

void append_fox_mesh(Mesh& mesh, Vec3 ground_center, float heading_radians, const FoxAnimationPose& pose);

}  // namespace voxel
