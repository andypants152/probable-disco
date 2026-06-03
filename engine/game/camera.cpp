#include "game/camera.h"

#include <algorithm>
#include <cmath>

namespace voxel {

namespace {

constexpr float kNormalizedLookPixelsPerSecond = 720.0f;

float clamped_axis(float value) {
  return std::max(-1.0f, std::min(1.0f, value));
}

}  // namespace

void Camera::update(const CameraInput& input) {
  const float look_delta_x = input.look_delta_x +
      clamped_axis(input.look_x) * kNormalizedLookPixelsPerSecond * input.delta_time;
  const float look_delta_y = input.look_delta_y +
      clamped_axis(input.look_y) * kNormalizedLookPixelsPerSecond * input.delta_time;
  yaw += look_delta_x * look_sensitivity;
  pitch -= look_delta_y * look_sensitivity;
  pitch = std::max(-1.45f, std::min(1.45f, pitch));

  const Vec3 planar_forward = {std::cos(yaw), 0.0f, std::sin(yaw)};
  const Vec3 planar_right = {-std::sin(yaw), 0.0f, std::cos(yaw)};

  float move_x = clamped_axis(input.move_x);
  float move_y = clamped_axis(input.move_y);
  move_y += input.forward ? -1.0f : 0.0f;
  move_y += input.back ? 1.0f : 0.0f;
  move_x += input.left ? -1.0f : 0.0f;
  move_x += input.right ? 1.0f : 0.0f;
  move_x = clamped_axis(move_x);
  move_y = clamped_axis(move_y);

  Vec3 move = planar_right * move_x - planar_forward * move_y;
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
