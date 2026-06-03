#pragma once

#include "math/mat4.h"
#include "math/vec3.h"

namespace voxel {

struct CameraInput {
  bool forward = false;
  bool back = false;
  bool left = false;
  bool right = false;
  bool up = false;
  bool down = false;
  bool interact = false;
  float look_delta_x = 0.0f;
  float look_delta_y = 0.0f;
  float delta_time = 1.0f / 60.0f;
};

class Camera {
 public:
  Vec3 position = {16.0f, 19.0f, 48.0f};
  float yaw = -1.57079632679f;
  float pitch = -0.35f;
  float aspect = 16.0f / 9.0f;
  float fov_y = 1.0471975512f;
  float near_z = 0.1f;
  float far_z = 250.0f;
  float move_speed = 14.0f;
  float look_sensitivity = 0.0025f;

  void update(const CameraInput& input);
  Vec3 forward() const;
  Vec3 right_vector() const;
  Mat4 view_matrix() const;
  Mat4 projection_matrix() const;
};

}  // namespace voxel
