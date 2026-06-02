#include "game/camera.h"

#include <algorithm>
#include <cmath>

namespace voxel {

void Camera::update(const CameraInput& input) {
  yaw += input.look_delta_x * look_sensitivity;
  pitch -= input.look_delta_y * look_sensitivity;
  pitch = std::max(-1.45f, std::min(1.45f, pitch));

  const Vec3 planar_forward = {std::cos(yaw), 0.0f, std::sin(yaw)};
  const Vec3 planar_right = {-std::sin(yaw), 0.0f, std::cos(yaw)};

  Vec3 move;
  if (input.forward) {
    move += planar_forward;
  }
  if (input.back) {
    move -= planar_forward;
  }
  if (input.right) {
    move += planar_right;
  }
  if (input.left) {
    move -= planar_right;
  }
  if (input.up) {
    move.y += 1.0f;
  }
  if (input.down) {
    move.y -= 1.0f;
  }

  if (length(move) > 0.00001f) {
    position += normalize(move) * (move_speed * input.delta_time);
  }
}

Vec3 Camera::forward() const {
  const float cp = std::cos(pitch);
  return normalize({cp * std::cos(yaw), std::sin(pitch), cp * std::sin(yaw)});
}

Vec3 Camera::right_vector() const {
  return normalize(cross(forward(), {0.0f, 1.0f, 0.0f}));
}

Mat4 Camera::view_matrix() const {
  return Mat4::look_at(position, position + forward(), {0.0f, 1.0f, 0.0f});
}

Mat4 Camera::projection_matrix() const {
  return Mat4::perspective(fov_y, aspect, near_z, far_z);
}

}  // namespace voxel
