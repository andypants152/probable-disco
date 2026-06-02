#include "app.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "game/fox.h"
#include "world/mesher.h"

namespace voxel {

namespace {

constexpr int kWorldRenderRadiusChunks = 2;
constexpr float kFoxMoveSpeed = 8.5f;
constexpr float kCameraDistance = 13.0f;
constexpr float kCameraTargetHeight = 1.35f;

using Clock = std::chrono::steady_clock;

std::uint64_t elapsed_ns(Clock::time_point start, Clock::time_point end) {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

int floor_to_int(float value) {
  return static_cast<int>(std::floor(value));
}

int floor_div(int value, int divisor) {
  int quotient = value / divisor;
  const int remainder = value % divisor;
  if (remainder < 0) {
    --quotient;
  }
  return quotient;
}

}  // namespace

bool App::init(Renderer& renderer) {
  fox_position_.x = 0.0f;
  fox_position_.z = 0.0f;
  fox_position_.y = static_cast<float>(generator_.terrain_height(0, 0) + 1);
  fox_heading_ = 0.0f;
  camera_.yaw = -1.57079632679f;
  camera_.pitch = -0.38f;
  update_camera({});

  rebuild_world_mesh();
  rebuild_fox_mesh();

  if (!renderer.init()) {
    return false;
  }

  if (renderer.supports_separate_meshes()) {
    renderer.upload_static_mesh(terrain_mesh_);
    renderer.upload_dynamic_mesh(fox_mesh_);
  } else {
    rebuild_scene_mesh();
    renderer.upload_mesh(mesh_);
  }

  initialized_ = true;
  return true;
}

void App::frame(Renderer& renderer, const CameraInput& input) {
  if (!initialized_) {
    return;
  }

  frame_stats_ = {};
  const auto frame_start = Clock::now();
  const int previous_chunk_x = world_center_chunk_x_;
  const int previous_chunk_z = world_center_chunk_z_;

  const auto update_start = Clock::now();
  const bool fox_moved = update_fox(input);
  update_camera(input);
  frame_stats_.update_ns = elapsed_ns(update_start, Clock::now());
  frame_stats_.fox_moved = fox_moved;

  const int current_chunk_x = floor_div(floor_to_int(fox_position_.x), kChunkSize);
  const int current_chunk_z = floor_div(floor_to_int(fox_position_.z), kChunkSize);
  const bool chunk_changed = current_chunk_x != previous_chunk_x || current_chunk_z != previous_chunk_z;
  frame_stats_.chunk_changed = chunk_changed;
  if (chunk_changed) {
    world_center_chunk_x_ = current_chunk_x;
    world_center_chunk_z_ = current_chunk_z;
    const auto rebuild_start = Clock::now();
    rebuild_world_mesh();
    frame_stats_.world_rebuild_ns = elapsed_ns(rebuild_start, Clock::now());
  }

  if (renderer.supports_separate_meshes()) {
    if (chunk_changed) {
      const auto upload_start = Clock::now();
      renderer.upload_static_mesh(terrain_mesh_);
      frame_stats_.upload_ns += elapsed_ns(upload_start, Clock::now());
    }
    if (fox_moved || chunk_changed) {
      const auto fox_rebuild_start = Clock::now();
      rebuild_fox_mesh();
      frame_stats_.fox_rebuild_ns = elapsed_ns(fox_rebuild_start, Clock::now());
      const auto upload_start = Clock::now();
      renderer.upload_dynamic_mesh(fox_mesh_);
      frame_stats_.upload_ns += elapsed_ns(upload_start, Clock::now());
    }
  } else if (fox_moved || chunk_changed) {
    const auto fox_rebuild_start = Clock::now();
    rebuild_fox_mesh();
    frame_stats_.fox_rebuild_ns = elapsed_ns(fox_rebuild_start, Clock::now());
    const auto scene_rebuild_start = Clock::now();
    rebuild_scene_mesh();
    frame_stats_.scene_rebuild_ns = elapsed_ns(scene_rebuild_start, Clock::now());
    const auto upload_start = Clock::now();
    renderer.upload_mesh(mesh_);
    frame_stats_.upload_ns = elapsed_ns(upload_start, Clock::now());
  }

  const auto render_start = Clock::now();
  renderer.render_frame(camera_);
  frame_stats_.render_ns = elapsed_ns(render_start, Clock::now());
  frame_stats_.total_ns = elapsed_ns(frame_start, Clock::now());
}

void App::shutdown(Renderer& renderer) {
  renderer.shutdown();
  initialized_ = false;
}

void App::rebuild_world_mesh() {
  const int span_chunks = kWorldRenderRadiusChunks * 2 + 1;
  const int min_x = (world_center_chunk_x_ - kWorldRenderRadiusChunks) * kChunkSize;
  const int min_z = (world_center_chunk_z_ - kWorldRenderRadiusChunks) * kChunkSize;
  terrain_mesh_ = build_world_mesh(generator_, min_x, min_z, span_chunks * kChunkSize, span_chunks * kChunkSize);
}

void App::rebuild_fox_mesh() {
  fox_mesh_.clear();
  append_fox_mesh(fox_mesh_, fox_position_, fox_heading_);
}

void App::rebuild_scene_mesh() {
  mesh_ = terrain_mesh_;
  mesh_.vertices.insert(mesh_.vertices.end(), fox_mesh_.vertices.begin(), fox_mesh_.vertices.end());
  mesh_.normals.insert(mesh_.normals.end(), fox_mesh_.normals.begin(), fox_mesh_.normals.end());
  mesh_.colors.insert(mesh_.colors.end(), fox_mesh_.colors.begin(), fox_mesh_.colors.end());
  mesh_.micro_positions.insert(mesh_.micro_positions.end(),
                               fox_mesh_.micro_positions.begin(),
                               fox_mesh_.micro_positions.end());

  const Index index_offset = static_cast<Index>(terrain_mesh_.vertices.size());
  mesh_.indices.reserve(terrain_mesh_.indices.size() + fox_mesh_.indices.size());
  for (Index index : fox_mesh_.indices) {
    mesh_.indices.push_back(index + index_offset);
  }
}

bool App::update_fox(const CameraInput& input) {
  const Vec3 camera_forward = {std::cos(camera_.yaw), 0.0f, std::sin(camera_.yaw)};
  const Vec3 camera_right = {-std::sin(camera_.yaw), 0.0f, std::cos(camera_.yaw)};

  Vec3 move;
  if (input.forward) {
    move += camera_forward;
  }
  if (input.back) {
    move -= camera_forward;
  }
  if (input.right) {
    move += camera_right;
  }
  if (input.left) {
    move -= camera_right;
  }

  if (length(move) <= 0.00001f) {
    return false;
  }

  move = normalize(move);
  fox_position_ += move * (kFoxMoveSpeed * input.delta_time);
  fox_heading_ = std::atan2(move.x, move.z);

  const int terrain_x = floor_to_int(fox_position_.x);
  const int terrain_z = floor_to_int(fox_position_.z);
  fox_position_.y = static_cast<float>(generator_.terrain_height(terrain_x, terrain_z) + 1);
  return true;
}

void App::update_camera(const CameraInput& input) {
  camera_.yaw += input.look_delta_x * camera_.look_sensitivity;
  camera_.pitch -= input.look_delta_y * camera_.look_sensitivity;
  camera_.pitch = std::max(-1.20f, std::min(-0.12f, camera_.pitch));

  const Vec3 target = fox_position_ + Vec3{0.0f, kCameraTargetHeight, 0.0f};
  camera_.position = target - camera_.forward() * kCameraDistance;
}

}  // namespace voxel
