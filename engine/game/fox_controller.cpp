#include "fox_controller.h"

#include <algorithm>
#include <cmath>

#include "world/generator.h"

namespace voxel {

namespace {

constexpr float kFoxMoveSpeed = 8.5f;
constexpr float kFoxHalfWidth = 0.82f;
constexpr float kFoxFrontFootZ = 0.72f;
constexpr float kFoxRearFootZ = -0.68f;

int floor_to_int(float value) {
  return static_cast<int>(std::floor(value));
}

float smoothstep(float value) {
  value = std::max(0.0f, std::min(1.0f, value));
  return value * value * (3.0f - 2.0f * value);
}

float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

float clamped_axis(float value) {
  return std::max(-1.0f, std::min(1.0f, value));
}

float interpolated_terrain_height(const TerrainGenerator& generator, float x, float z) {
  const int x0 = floor_to_int(x);
  const int z0 = floor_to_int(z);
  const float tx = smoothstep(x - static_cast<float>(x0));
  const float tz = smoothstep(z - static_cast<float>(z0));

  const float h00 = static_cast<float>(generator.terrain_height(x0, z0));
  const float h10 = static_cast<float>(generator.terrain_height(x0 + 1, z0));
  const float h01 = static_cast<float>(generator.terrain_height(x0, z0 + 1));
  const float h11 = static_cast<float>(generator.terrain_height(x0 + 1, z0 + 1));

  return lerp(lerp(h00, h10, tx), lerp(h01, h11, tx), tz);
}

Vec3 local_to_world_offset(float local_x, float local_z, float heading) {
  const float s = std::sin(heading);
  const float c = std::cos(heading);
  return {
    local_x * c + local_z * s,
    0.0f,
    -local_x * s + local_z * c,
  };
}

float fox_support_height(const TerrainGenerator& generator, Vec3 position, float heading) {
  const Vec3 offsets[] = {
    {0.0f, 0.0f, 0.0f},
    local_to_world_offset(-kFoxHalfWidth, kFoxFrontFootZ, heading),
    local_to_world_offset(kFoxHalfWidth, kFoxFrontFootZ, heading),
    local_to_world_offset(-kFoxHalfWidth, kFoxRearFootZ, heading),
    local_to_world_offset(kFoxHalfWidth, kFoxRearFootZ, heading),
  };

  float height = interpolated_terrain_height(generator, position.x, position.z);
  for (const Vec3& offset : offsets) {
    height = std::max(height, interpolated_terrain_height(generator, position.x + offset.x, position.z + offset.z));
  }
  return height;
}

}  // namespace

void FoxController::init(const TerrainGenerator& generator) {
  position_.x = 0.0f;
  position_.z = 0.0f;
  heading_ = 0.0f;
  movement_speed_ = 0.0f;
  moved_this_frame_ = false;
  position_.y = fox_support_height(generator, position_, heading_) + 1.0f;
}

bool FoxController::update(const CameraInput& input, const TerrainGenerator& generator, const Camera& camera) {
  movement_speed_ = 0.0f;
  const Vec3 camera_forward = {std::cos(camera.yaw), 0.0f, std::sin(camera.yaw)};
  const Vec3 camera_right = {-std::sin(camera.yaw), 0.0f, std::cos(camera.yaw)};

  float move_x = clamped_axis(input.move_x);
  float move_y = clamped_axis(input.move_y);
  move_y += input.forward ? -1.0f : 0.0f;
  move_y += input.back ? 1.0f : 0.0f;
  move_x += input.left ? -1.0f : 0.0f;
  move_x += input.right ? 1.0f : 0.0f;
  move_x = clamped_axis(move_x);
  move_y = clamped_axis(move_y);

  Vec3 move = camera_right * move_x - camera_forward * move_y;
  const float move_scale = std::min(1.0f, std::sqrt(move_x * move_x + move_y * move_y));

  bool horizontal_moved = false;
  if (move_scale > 0.00001f && length(move) > 0.00001f) {
    move = normalize(move);
    position_ += move * (kFoxMoveSpeed * move_scale * input.delta_time);
    heading_ = std::atan2(move.x, move.z);
    movement_speed_ = kFoxMoveSpeed * move_scale;
    horizontal_moved = true;
  }

  const float previous_y = position_.y;
  position_.y = fox_support_height(generator, position_, heading_) + 1.0f;
  const bool vertical_moved = std::fabs(position_.y - previous_y) > 0.0005f;
  moved_this_frame_ = horizontal_moved || vertical_moved;
  return moved_this_frame_;
}

Vec3 FoxController::forward() const {
  return {std::sin(heading_), 0.0f, std::cos(heading_)};
}

}  // namespace voxel
