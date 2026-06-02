#include "app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "audio.h"
#include "forest_audio.h"
#include "game/fox.h"
#include "world/mesher.h"

namespace voxel {

namespace {

constexpr int kWorldRenderRadiusChunks = 2;
constexpr float kFoxMoveSpeed = 8.5f;
constexpr float kCameraDistance = 13.0f;
constexpr float kCameraTargetHeight = 1.35f;
constexpr float kFoxHalfWidth = 0.82f;
constexpr float kFoxFrontFootZ = 0.72f;
constexpr float kFoxRearFootZ = -0.68f;
constexpr float kOwlEncounterRadius = 18.0f;

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

int chunk_center_step(int current_chunk, int center_chunk) {
  const int delta = current_chunk - center_chunk;
  if (delta > 1) {
    return center_chunk + 1;
  }
  if (delta < -1) {
    return center_chunk - 1;
  }
  return center_chunk;
}

float smoothstep(float value) {
  value = std::max(0.0f, std::min(1.0f, value));
  return value * value * (3.0f - 2.0f * value);
}

float lerp(float a, float b, float t) {
  return a + (b - a) * t;
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

Vec3 fox_forward(float heading) {
  return {std::sin(heading), 0.0f, std::cos(heading)};
}

float horizontal_distance(Vec3 a, Vec3 b) {
  const float dx = b.x - a.x;
  const float dz = b.z - a.z;
  return std::sqrt(dx * dx + dz * dz);
}

void append_mesh(Mesh& destination, const Mesh& source) {
  const Index index_offset = static_cast<Index>(destination.vertices.size());
  destination.vertices.insert(destination.vertices.end(), source.vertices.begin(), source.vertices.end());
  destination.normals.insert(destination.normals.end(), source.normals.begin(), source.normals.end());
  destination.colors.insert(destination.colors.end(), source.colors.begin(), source.colors.end());
  destination.micro_positions.insert(destination.micro_positions.end(),
                                     source.micro_positions.begin(),
                                     source.micro_positions.end());
  for (Index index : source.indices) {
    destination.indices.push_back(index + index_offset);
  }
}

}  // namespace

bool App::init(Renderer& renderer) {
  fox_position_.x = 0.0f;
  fox_position_.z = 0.0f;
  fox_heading_ = 0.0f;
  fox_position_.y = fox_support_height(generator_, fox_position_, fox_heading_) + 1.0f;
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

  if (!audio_init()) {
    std::printf("Audio unavailable; continuing without sound.\n");
  } else {
    audio_set_forest_hum(0.0f, 1.0f);
  }
  forest_audio_init();

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
  const Vec3 owl_position = owl_perch_position(generator_);
  const ForestAudioPlayerState player_audio = {
    fox_position_,
    fox_forward(fox_heading_),
    fox_movement_speed_,
    true,
  };
  const ForestAudioWorldState world_audio = {
    heart_position(generator_),
    owl_position,
    horizontal_distance(fox_position_, owl_position) <= kOwlEncounterRadius,
  };
  forest_audio_update(input.delta_time, &player_audio, &world_audio);
  audio_update(input.delta_time);
  frame_stats_.forest_audio = forest_audio_debug_status();
  frame_stats_.update_ns = elapsed_ns(update_start, Clock::now());
  frame_stats_.fox_moved = fox_moved;

  const int current_chunk_x = floor_div(floor_to_int(fox_position_.x), kChunkSize);
  const int current_chunk_z = floor_div(floor_to_int(fox_position_.z), kChunkSize);
  const int next_center_chunk_x = chunk_center_step(current_chunk_x, previous_chunk_x);
  const int next_center_chunk_z = chunk_center_step(current_chunk_z, previous_chunk_z);
  const bool chunk_changed = next_center_chunk_x != previous_chunk_x || next_center_chunk_z != previous_chunk_z;
  frame_stats_.chunk_changed = chunk_changed;
  if (chunk_changed) {
    world_center_chunk_x_ = next_center_chunk_x;
    world_center_chunk_z_ = next_center_chunk_z;
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
  forest_audio_shutdown();
  audio_shutdown();
  renderer.shutdown();
  initialized_ = false;
}

void App::rebuild_world_mesh() {
  const int span_chunks = kWorldRenderRadiusChunks * 2 + 1;
  const int min_chunk_x = world_center_chunk_x_ - kWorldRenderRadiusChunks;
  const int min_chunk_z = world_center_chunk_z_ - kWorldRenderRadiusChunks;
  const int max_chunk_x = world_center_chunk_x_ + kWorldRenderRadiusChunks;
  const int max_chunk_z = world_center_chunk_z_ + kWorldRenderRadiusChunks;

  for (int chunk_z = min_chunk_z; chunk_z <= max_chunk_z; ++chunk_z) {
    for (int chunk_x = min_chunk_x; chunk_x <= max_chunk_x; ++chunk_x) {
      const auto cached = std::find_if(terrain_chunk_cache_.begin(),
                                       terrain_chunk_cache_.end(),
                                       [chunk_x, chunk_z](const CachedTerrainChunk& chunk) {
                                         return chunk.chunk_x == chunk_x && chunk.chunk_z == chunk_z;
                                       });
      if (cached != terrain_chunk_cache_.end()) {
        continue;
      }

      CachedTerrainChunk chunk;
      chunk.chunk_x = chunk_x;
      chunk.chunk_z = chunk_z;
      chunk.mesh = build_world_mesh(generator_,
                                    chunk_x * kChunkSize,
                                    chunk_z * kChunkSize,
                                    kChunkSize,
                                    kChunkSize);
      terrain_chunk_cache_.push_back(std::move(chunk));
    }
  }

  std::size_t vertex_count = 0;
  std::size_t index_count = 0;
  for (const CachedTerrainChunk& chunk : terrain_chunk_cache_) {
    if (chunk.chunk_x < min_chunk_x || chunk.chunk_x > max_chunk_x ||
        chunk.chunk_z < min_chunk_z || chunk.chunk_z > max_chunk_z) {
      continue;
    }
    vertex_count += chunk.mesh.vertices.size();
    index_count += chunk.mesh.indices.size();
  }

  terrain_mesh_.clear();
  terrain_mesh_.vertices.reserve(vertex_count);
  terrain_mesh_.normals.reserve(vertex_count);
  terrain_mesh_.colors.reserve(vertex_count);
  terrain_mesh_.micro_positions.reserve(vertex_count);
  terrain_mesh_.indices.reserve(index_count);

  for (int chunk_z = min_chunk_z; chunk_z <= max_chunk_z; ++chunk_z) {
    for (int chunk_x = min_chunk_x; chunk_x <= max_chunk_x; ++chunk_x) {
      const auto cached = std::find_if(terrain_chunk_cache_.begin(),
                                       terrain_chunk_cache_.end(),
                                       [chunk_x, chunk_z](const CachedTerrainChunk& chunk) {
                                         return chunk.chunk_x == chunk_x && chunk.chunk_z == chunk_z;
                                       });
      if (cached != terrain_chunk_cache_.end()) {
        append_mesh(terrain_mesh_, cached->mesh);
      }
    }
  }

  std::vector<CachedTerrainChunk> visible_cache;
  visible_cache.reserve(static_cast<std::size_t>(span_chunks * span_chunks));
  for (CachedTerrainChunk& chunk : terrain_chunk_cache_) {
    if (chunk.chunk_x < min_chunk_x || chunk.chunk_x > max_chunk_x ||
        chunk.chunk_z < min_chunk_z || chunk.chunk_z > max_chunk_z) {
      continue;
    }
    visible_cache.push_back(std::move(chunk));
  }
  terrain_chunk_cache_ = std::move(visible_cache);
}

void App::rebuild_fox_mesh() {
  fox_mesh_.clear();
  append_fox_mesh(fox_mesh_, fox_position_, fox_heading_);
}

void App::rebuild_scene_mesh() {
  mesh_ = terrain_mesh_;
  append_mesh(mesh_, fox_mesh_);
}

bool App::update_fox(const CameraInput& input) {
  fox_movement_speed_ = 0.0f;
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

  bool horizontal_moved = false;
  if (length(move) > 0.00001f) {
    move = normalize(move);
    fox_position_ += move * (kFoxMoveSpeed * input.delta_time);
    fox_heading_ = std::atan2(move.x, move.z);
    fox_movement_speed_ = kFoxMoveSpeed;
    horizontal_moved = true;
  }

  const float previous_y = fox_position_.y;
  fox_position_.y = fox_support_height(generator_, fox_position_, fox_heading_) + 1.0f;
  const bool vertical_moved = std::fabs(fox_position_.y - previous_y) > 0.0005f;
  return horizontal_moved || vertical_moved;
}

void App::update_camera(const CameraInput& input) {
  camera_.yaw += input.look_delta_x * camera_.look_sensitivity;
  camera_.pitch -= input.look_delta_y * camera_.look_sensitivity;
  camera_.pitch = std::max(-1.20f, std::min(-0.12f, camera_.pitch));

  const Vec3 target = fox_position_ + Vec3{0.0f, kCameraTargetHeight, 0.0f};
  camera_.position = target - camera_.forward() * kCameraDistance;
}

}  // namespace voxel
