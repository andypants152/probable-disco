#include "app.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "audio.h"
#include "forest_audio.h"
#include "game/fox.h"
#include "subtitles.h"
#include "world/mesher.h"

namespace voxel {

namespace {

constexpr int kWorldRenderRadiusChunks = 2;
constexpr float kFoxMoveSpeed = 8.5f;
constexpr float kNormalizedLookPixelsPerSecond = 1800.0f;
constexpr float kCameraDistance = 13.0f;
constexpr float kCameraTargetHeight = 1.35f;
constexpr float kFoxHalfWidth = 0.82f;
constexpr float kFoxFrontFootZ = 0.72f;
constexpr float kFoxRearFootZ = -0.68f;
constexpr float kOwlEncounterRadius = 18.0f;
constexpr float kOwlDefaultHeading = 3.14159265358979323846f;
constexpr float kOwlFlyAwayHeading = 0.0f;
constexpr float kOwlTalkSeconds = 3.6f;
constexpr float kOwlFlySeconds = 2.6f;
constexpr float kOwlSecondLineTime = 1.95f;
constexpr float FIREFLY_BOB_HEIGHT = 0.52f;
constexpr float FIREFLY_GLOW_MIN = 0.64f;
constexpr float FIREFLY_GLOW_MAX = 1.0f;
constexpr float LANTERN_SPAWN_CLEAR_RADIUS = 8.0f;
constexpr float LANTERN_LIGHT_RADIUS = 18.0f;
constexpr float LANTERN_LIGHT_INTENSITY = 2.0f;
constexpr float LANTERN_GROUND_GLOW_RADIUS = 9.0f;
constexpr float LANTERN_LIGHT_PULSE_DURATION = 2.4f;
constexpr float LANTERN_LIGHT_PULSE_RADIUS = 22.0f;
constexpr float kGameplayLightCullPadding = kCameraDistance + 4.0f;
constexpr float kFireflyLightRadius = 4.4f;
constexpr float kCarriedFireflyLightRadius = 4.0f;
constexpr float kUnlitLanternLightRadius = 7.0f;
constexpr int kMaxSubmittedGameplayLights = 4;
constexpr int kMaxUncollectedFireflyLights = 3;
constexpr float kFireflyLightCullRadius = 11.0f;
constexpr float kFireflyCollectRadius = 1.85f;
constexpr float kFireflyDepositRadius = 3.15f;
constexpr float kFireflyClusterRadius = LANTERN_SPAWN_CLEAR_RADIUS;
constexpr float kFireflyHeight = 2.65f;
constexpr float kDepositInterval = 0.16f;
constexpr float kLanternLightPulseSeconds = LANTERN_LIGHT_PULSE_DURATION;
constexpr std::array<Vec3, 4> kStarterLanternAnchors = {{
    {7.0f, 0.0f, -14.0f},
    {30.0f, 0.0f, -38.0f},
    {54.0f, 0.0f, -70.0f},
    {22.0f, 0.0f, -103.0f},
}};
constexpr std::array<Vec3, 12> kFireflyOffsets = {{
    {0.0f, 0.0f, -5.6f},
    {4.8f, 0.0f, -2.7f},
    {4.9f, 0.0f, 2.8f},
    {0.0f, 0.0f, 5.8f},
    {-4.7f, 0.0f, 2.6f},
    {-4.8f, 0.0f, -2.8f},
    {2.3f, 0.0f, -7.1f},
    {6.8f, 0.0f, 0.0f},
    {2.4f, 0.0f, 7.1f},
    {-2.4f, 0.0f, 7.1f},
    {-6.8f, 0.0f, 0.0f},
    {-2.3f, 0.0f, -7.1f},
}};

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

Vec3 fox_forward(float heading) {
  return {std::sin(heading), 0.0f, std::cos(heading)};
}

float horizontal_distance(Vec3 a, Vec3 b) {
  const float dx = b.x - a.x;
  const float dz = b.z - a.z;
  return std::sqrt(dx * dx + dz * dz);
}

Vec3 terrain_position(const TerrainGenerator& generator, Vec3 anchor, float height_offset) {
  return {
    anchor.x,
    interpolated_terrain_height(generator, anchor.x, anchor.z) + height_offset,
    anchor.z,
  };
}

Vec3 lantern_anchor_for_sequence(int sequence) {
  if (sequence < static_cast<int>(kStarterLanternAnchors.size())) {
    return kStarterLanternAnchors[static_cast<std::size_t>(sequence)];
  }

  const float step = static_cast<float>(sequence);
  const float forward = -102.0f - static_cast<float>(sequence - 3) * 27.0f;
  const float weave = std::sin(step * 1.31f) * 38.0f + std::sin(step * 0.47f) * 14.0f;
  return {weave, 0.0f, forward};
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
  owl_position_ = owl_perch_position(generator_);
  owl_heading_ = kOwlDefaultHeading;
  init_lantern_loop();
  rebuild_dynamic_mesh();

  if (!renderer.init()) {
    return false;
  }

  if (renderer.supports_separate_meshes()) {
    renderer.upload_static_mesh(terrain_mesh_);
    renderer.upload_dynamic_mesh(dynamic_mesh_);
  } else {
    rebuild_scene_mesh();
    renderer.upload_mesh(mesh_);
  }

  if (!audio_init()) {
    std::printf("Audio unavailable; continuing without sound.\n");
  }
  forest_audio_init();
  if (!subtitles_init()) {
    std::printf("Subtitles unavailable; continuing without subtitle overlay.\n");
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
  const bool owl_changed = update_owl(input);
  const bool gameplay_changed = update_fireflies(input.delta_time);
  rebuild_gameplay_lights();
  update_camera(input);
  const bool owl_exists = owl_state_ != OwlState::Gone;
  const bool owl_encounter_active = owl_exists && owl_state_ != OwlState::Waiting;
  const ForestAudioPlayerState player_audio = {
    fox_position_,
    fox_forward(fox_heading_),
    fox_movement_speed_,
    true,
  };
  const ForestAudioWorldState world_audio = {
    gameplay_objective_position(),
    owl_position_,
    owl_encounter_active,
  };
  forest_audio_update(input.delta_time, &player_audio, &world_audio);
  audio_update(input.delta_time);
  subtitles_update(input.delta_time);
  update_lantern_hud();
  frame_stats_.carried_fireflies = carried_fireflies_;
  frame_stats_.active_lantern_index = lantern_sequence_;
  frame_stats_.deposited_fireflies = lanterns_[active_lantern_index_].deposited_fireflies;
  frame_stats_.required_fireflies = lanterns_[active_lantern_index_].required_fireflies;
  frame_stats_.active_fireflies = active_firefly_count();
  frame_stats_.active_gameplay_lights = gameplay_light_count_;
  frame_stats_.firefly_glow_intensity = FIREFLY_GLOW_MAX;
  frame_stats_.lantern_light_intensity = LANTERN_LIGHT_INTENSITY;
  frame_stats_.lantern_light_radius = LANTERN_LIGHT_RADIUS;
  frame_stats_.distance_to_objective = horizontal_distance(fox_position_, gameplay_objective_position());
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
    if (fox_moved || owl_changed || gameplay_changed || chunk_changed) {
      const auto fox_rebuild_start = Clock::now();
      if (fox_moved || chunk_changed) {
        rebuild_fox_mesh();
      }
      rebuild_dynamic_mesh();
      frame_stats_.fox_rebuild_ns = elapsed_ns(fox_rebuild_start, Clock::now());
      const auto upload_start = Clock::now();
      renderer.upload_dynamic_mesh(dynamic_mesh_);
      frame_stats_.upload_ns += elapsed_ns(upload_start, Clock::now());
    }
  } else if (fox_moved || owl_changed || gameplay_changed || chunk_changed) {
    const auto fox_rebuild_start = Clock::now();
    if (fox_moved || chunk_changed) {
      rebuild_fox_mesh();
    }
    rebuild_dynamic_mesh();
    frame_stats_.fox_rebuild_ns = elapsed_ns(fox_rebuild_start, Clock::now());
    const auto scene_rebuild_start = Clock::now();
    rebuild_scene_mesh();
    frame_stats_.scene_rebuild_ns = elapsed_ns(scene_rebuild_start, Clock::now());
    const auto upload_start = Clock::now();
    renderer.upload_mesh(mesh_);
    frame_stats_.upload_ns = elapsed_ns(upload_start, Clock::now());
  }

  const auto render_start = Clock::now();
  render_frame_commands_.clear();
  render_frame_commands_.camera = camera_;
  render_frame_commands_.light_count = std::min(gameplay_light_count_, kMaxRendererGameplayLights);
  for (int i = 0; i < render_frame_commands_.light_count; ++i) {
    render_frame_commands_.lights[static_cast<std::size_t>(i)] = gameplay_lights_[static_cast<std::size_t>(i)];
  }
  render_frame_commands_.subtitle = &subtitles_frame();
  render_frame_commands_.hud = &subtitles_hud_frame();
  render_frame_commands_.commands.push_back({RenderCommandType::DrawStaticMesh});
  if (renderer.supports_separate_meshes()) {
    render_frame_commands_.commands.push_back({RenderCommandType::DrawDynamicMesh});
  }
  render_frame_commands_.commands.push_back({RenderCommandType::DrawSubtitle});
  renderer.render_frame(render_frame_commands_);
  frame_stats_.render_ns = elapsed_ns(render_start, Clock::now());
  frame_stats_.total_ns = elapsed_ns(frame_start, Clock::now());
}

void App::shutdown(Renderer& renderer) {
  subtitles_shutdown();
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

void App::rebuild_dynamic_mesh() {
  dynamic_mesh_.clear();
  append_mesh(dynamic_mesh_, fox_mesh_);
  for (const Lantern& lantern : lanterns_) {
    if (!lantern.active && !lantern.lit && lantern.deposited_fireflies == 0) {
      continue;
    }
    append_lantern_mesh(dynamic_mesh_,
                        lantern.position,
                        lantern.deposited_fireflies,
                        lantern.required_fireflies,
                        lantern.lit,
                        lantern.glow_intensity);
  }
  for (const Firefly& firefly : fireflies_) {
    if (!firefly.active || firefly.collected) {
      continue;
    }
    append_firefly_mesh(dynamic_mesh_, firefly.position, firefly.glow_intensity, false);
  }
  for (int i = 0; i < carried_fireflies_; ++i) {
    append_firefly_mesh(dynamic_mesh_, carried_firefly_position(i), 1.0f, true);
  }
  const Vec3 perch = owl_perch_position(generator_);
  append_owl_perch_mesh(dynamic_mesh_, perch, kOwlDefaultHeading);
  if (owl_state_ == OwlState::Gone) {
    return;
  }

  append_owl_mesh(dynamic_mesh_, owl_position_, owl_heading_, owl_wing_pose_);
}

void App::rebuild_scene_mesh() {
  mesh_ = terrain_mesh_;
  append_mesh(mesh_, dynamic_mesh_);
}

void App::init_lantern_loop() {
  carried_fireflies_ = 0;
  active_lantern_index_ = 0;
  lantern_sequence_ = 0;
  firefly_chime_cooldown_ = 1.0f;
  deposit_cooldown_ = 0.0f;
  for (Lantern& lantern : lanterns_) {
    lantern = {};
    lantern.required_fireflies = 3;
  }
  for (Firefly& firefly : fireflies_) {
    firefly = {};
  }
  activate_lantern(0);
}

void App::activate_lantern(int sequence) {
  if (sequence < 0) {
    sequence = 0;
  }
  lantern_sequence_ = sequence;
  active_lantern_index_ = sequence % static_cast<int>(lanterns_.size());
  for (Lantern& lantern : lanterns_) {
    lantern.active = false;
  }

  Lantern& lantern = lanterns_[active_lantern_index_];
  lantern = {};
  lantern.position = terrain_position(generator_, lantern_anchor_for_sequence(sequence), 0.10f);
  lantern.active = true;
  lantern.lit = false;
  lantern.deposited_fireflies = 0;
  lantern.required_fireflies = 3;
  lantern.glow_intensity = 0.0f;
  lantern.glow_timer = 0.0f;
  lantern.pulse_timer = 0.0f;
  spawn_fireflies_for_lantern(active_lantern_index_);
}

void App::spawn_fireflies_for_lantern(int index) {
  for (Firefly& firefly : fireflies_) {
    firefly = {};
  }

  const Lantern& lantern = lanterns_[index];
  const int count = std::min<int>(kMaxFireflies, lantern.required_fireflies + 3);
  for (int i = 0; i < count; ++i) {
    Firefly& firefly = fireflies_[i];
    const Vec3 offset = kFireflyOffsets[static_cast<std::size_t>(i)];
    const float offset_distance = std::max(0.001f, horizontal_distance({}, offset));
    const float clamped_radius = std::min(offset_distance, kFireflyClusterRadius);
    const Vec3 readable_offset = {offset.x / offset_distance * clamped_radius,
                                  0.0f,
                                  offset.z / offset_distance * clamped_radius};
    const Vec3 anchor = {lantern.position.x + readable_offset.x, 0.0f, lantern.position.z + readable_offset.z};
    firefly.home = terrain_position(generator_, anchor, kFireflyHeight + 0.14f * static_cast<float>(i % 3));
    firefly.position = firefly.home;
    firefly.velocity = {0.0f, 0.0f, 0.0f};
    firefly.phase = 0.73f * static_cast<float>(i);
    firefly.bob_timer = 1.31f * static_cast<float>(i);
    firefly.glow_intensity = 0.75f;
    firefly.collected = false;
    firefly.active = true;
  }
}

int App::active_firefly_count() const {
  int count = 0;
  for (const Firefly& firefly : fireflies_) {
    if (firefly.active && !firefly.collected) {
      ++count;
    }
  }
  return count;
}

void App::update_lantern_hud() {
  char text[64] = {};
  std::snprintf(text,
                sizeof(text),
                "Lanterns lit %d",
                lantern_sequence_);
  subtitles_set_hud_text(text);
}

Vec3 App::gameplay_objective_position() const {
  const Lantern& lantern = lanterns_[active_lantern_index_];
  if (carried_fireflies_ > 0) {
    return lantern.position;
  }

  float nearest_distance = 1000000.0f;
  Vec3 nearest_position = lantern.position;
  for (const Firefly& firefly : fireflies_) {
    if (!firefly.active || firefly.collected) {
      continue;
    }
    const float distance = horizontal_distance(fox_position_, firefly.position);
    if (distance < nearest_distance) {
      nearest_distance = distance;
      nearest_position = firefly.position;
    }
  }
  return nearest_position;
}

Vec3 App::carried_firefly_position(int index) const {
  const float angle = fox_heading_ + static_cast<float>(index) * 2.09439510239f;
  const float radius = 1.0f + 0.18f * static_cast<float>(index % 2);
  return fox_position_ + Vec3{
    std::sin(angle) * radius,
    1.35f + 0.18f * std::sin(static_cast<float>(index) * 1.7f),
    std::cos(angle) * radius,
  };
}

void App::add_gameplay_light(Vec3 position, Vec3 color, float radius, float intensity) {
  if (gameplay_light_count_ >= kMaxSubmittedGameplayLights ||
      gameplay_light_count_ >= static_cast<int>(gameplay_lights_.size()) ||
      intensity <= 0.0f ||
      radius <= 0.0f) {
    return;
  }
  if (horizontal_distance(fox_position_, position) > radius + kGameplayLightCullPadding) {
    return;
  }
  GameplayLight& light = gameplay_lights_[static_cast<std::size_t>(gameplay_light_count_++)];
  light.position = position;
  light.color = color;
  light.radius = radius;
  light.intensity = intensity;
  light.active = true;
}

void App::rebuild_gameplay_lights() {
  for (GameplayLight& light : gameplay_lights_) {
    light = {};
  }
  gameplay_light_count_ = 0;

  const Vec3 firefly_color = {1.0f, 0.78f, 0.22f};
  const Vec3 mote_color = {0.86f, 1.0f, 0.46f};
  const Vec3 lantern_color = {1.0f, 0.52f, 0.18f};

  const Lantern& lantern = lanterns_[active_lantern_index_];
  if (lantern.active && !lantern.lit) {
    const Vec3 lantern_light_position = lantern.position + Vec3{0.0f, 1.85f, 0.0f};
    add_gameplay_light(lantern_light_position,
                       lantern_color,
                       kUnlitLanternLightRadius,
                       0.36f + lantern.glow_intensity * 0.70f);
  }

  for (int i = 0; i < carried_fireflies_; ++i) {
    add_gameplay_light(carried_firefly_position(i),
                       mote_color,
                       kCarriedFireflyLightRadius,
                       0.48f);
  }

  int uncollected_firefly_lights = 0;
  for (const Firefly& firefly : fireflies_) {
    if (!firefly.active || firefly.collected) {
      continue;
    }
    if (uncollected_firefly_lights >= kMaxUncollectedFireflyLights) {
      break;
    }
    if (horizontal_distance(fox_position_, firefly.position) > kFireflyLightCullRadius) {
      continue;
    }
    add_gameplay_light(firefly.position,
                       firefly_color,
                       kFireflyLightRadius,
                       0.30f + firefly.glow_intensity * 0.44f);
    ++uncollected_firefly_lights;
  }

  for (const Lantern& lit_lantern : lanterns_) {
    if (!lit_lantern.lit) {
      continue;
    }
    const Vec3 lantern_light_position = lit_lantern.position + Vec3{0.0f, 1.85f, 0.0f};
    const float pulse_t = lit_lantern.pulse_timer > 0.0f
        ? 1.0f - lit_lantern.pulse_timer / LANTERN_LIGHT_PULSE_DURATION
        : 1.0f;
    const float pulse = lit_lantern.pulse_timer > 0.0f ? (1.0f - pulse_t) * 0.8f : 0.0f;
    add_gameplay_light(lantern_light_position,
                       lantern_color,
                       LANTERN_LIGHT_RADIUS + pulse * LANTERN_LIGHT_PULSE_RADIUS,
                       LANTERN_LIGHT_INTENSITY + pulse);
    add_gameplay_light(lit_lantern.position + Vec3{0.0f, 0.26f, 0.0f},
                       lantern_color,
                       LANTERN_GROUND_GLOW_RADIUS,
                       0.42f);
  }
}

bool App::update_fireflies(float dt) {
  bool changed = false;
  dt = std::max(0.0f, std::min(dt, 0.10f));
  firefly_chime_cooldown_ = std::max(0.0f, firefly_chime_cooldown_ - dt);
  deposit_cooldown_ = std::max(0.0f, deposit_cooldown_ - dt);

  for (std::size_t i = 0; i < lanterns_.size(); ++i) {
    if (static_cast<int>(i) == active_lantern_index_ || !lanterns_[i].lit) {
      continue;
    }
    Lantern& lit_lantern = lanterns_[i];
    lit_lantern.glow_timer += dt;
    const float pulse_before = lit_lantern.pulse_timer;
    if (lit_lantern.pulse_timer > 0.0f) {
      lit_lantern.pulse_timer = std::max(0.0f, lit_lantern.pulse_timer - dt);
    }
    const float pulse = lit_lantern.pulse_timer > 0.0f
        ? std::sin((kLanternLightPulseSeconds - lit_lantern.pulse_timer) * 8.0f) * 0.18f + 0.18f
        : 0.0f;
    lit_lantern.glow_intensity = std::min(1.0f, 0.82f + pulse);
    changed = changed || pulse_before != lit_lantern.pulse_timer;
  }

  Lantern& lantern = lanterns_[active_lantern_index_];
  lantern.glow_timer += dt;
  changed = true;
  const float lantern_pulse_before = lantern.pulse_timer;
  if (lantern.pulse_timer > 0.0f) {
    lantern.pulse_timer = std::max(0.0f, lantern.pulse_timer - dt);
    changed = true;
  }
  const float pulse = lantern.pulse_timer > 0.0f
      ? std::sin((kLanternLightPulseSeconds - lantern.pulse_timer) * 8.0f) * 0.18f + 0.18f
      : 0.0f;
  const float active_marker = lantern.active
      ? 0.18f + 0.12f * (std::sin(lantern.glow_timer * 3.6f + lantern.position.x) * 0.5f + 0.5f)
      : 0.0f;
  const float deposit_fill = lantern.required_fireflies > 0
      ? static_cast<float>(lantern.deposited_fireflies) / static_cast<float>(lantern.required_fireflies)
      : 1.0f;
  const float deposit_proximity = horizontal_distance(fox_position_, lantern.position) <= kFireflyDepositRadius + 1.0f
      ? 0.18f
      : 0.0f;
  lantern.glow_intensity = lantern.lit ? std::min(1.0f, 0.78f + pulse) :
      std::min(1.0f, active_marker + deposit_fill * 0.44f + deposit_proximity);
  if (lantern_pulse_before != lantern.pulse_timer) {
    changed = true;
  }

  bool near_uncollected_firefly = false;
  for (Firefly& firefly : fireflies_) {
    if (!firefly.active || firefly.collected) {
      continue;
    }

    firefly.phase += dt * (0.65f + 0.12f * std::sin(firefly.phase));
    firefly.bob_timer += dt;
    const float drift_x = std::sin(firefly.phase * 1.37f) * 0.72f +
                          std::cos(firefly.phase * 0.71f) * 0.34f;
    const float drift_z = std::cos(firefly.phase * 1.19f) * 0.72f +
                          std::sin(firefly.phase * 0.83f) * 0.34f;
    const float bob = std::sin(firefly.bob_timer * 2.4f) * FIREFLY_BOB_HEIGHT;
    firefly.position = firefly.home + Vec3{drift_x, bob, drift_z};
    const float distance = horizontal_distance(fox_position_, firefly.position);
    const float proximity_boost = distance <= 4.8f ? 0.22f : 0.0f;
    const float pulse_speed = distance <= kFireflyCollectRadius + 1.0f ? 7.2f : 3.8f;
    const float pulse_t = std::sin(firefly.bob_timer * pulse_speed) * 0.5f + 0.5f;
    firefly.glow_intensity = std::min(FIREFLY_GLOW_MAX,
                                      FIREFLY_GLOW_MIN +
                                          (FIREFLY_GLOW_MAX - FIREFLY_GLOW_MIN) * pulse_t +
                                          proximity_boost);
    changed = true;

    if (distance < 9.5f) {
      near_uncollected_firefly = true;
    }
    if (distance <= kFireflyCollectRadius) {
      firefly.collected = true;
      firefly.active = false;
      ++carried_fireflies_;
      changed = true;
      if (audio_ready_for_gameplay_sound()) {
        audio_play_mote_chime(0.72f);
      }
    }
  }

  if (near_uncollected_firefly && firefly_chime_cooldown_ <= 0.0f && audio_ready_for_gameplay_sound()) {
    audio_play_mote_chime(0.24f);
    firefly_chime_cooldown_ = 2.1f;
  }

  if (carried_fireflies_ > 0 &&
      !lantern.lit &&
      deposit_cooldown_ <= 0.0f &&
      horizontal_distance(fox_position_, lantern.position) <= kFireflyDepositRadius) {
    --carried_fireflies_;
    ++lantern.deposited_fireflies;
    deposit_cooldown_ = kDepositInterval;
    changed = true;
    if (audio_ready_for_gameplay_sound()) {
      audio_play_mote_chime(0.88f);
    }

    if (lantern.deposited_fireflies >= lantern.required_fireflies) {
      lantern.lit = true;
      lantern.active = false;
      lantern.glow_intensity = 1.0f;
      lantern.pulse_timer = kLanternLightPulseSeconds;
      if (audio_ready_for_gameplay_sound()) {
        audio_play_owl_appear();
        audio_play_mote_chime(1.0f);
      }
      activate_lantern(lantern_sequence_ + 1);
    }
  }

  return changed;
}

bool App::update_fox(const CameraInput& input) {
  fox_movement_speed_ = 0.0f;
  const Vec3 camera_forward = {std::cos(camera_.yaw), 0.0f, std::sin(camera_.yaw)};
  const Vec3 camera_right = {-std::sin(camera_.yaw), 0.0f, std::cos(camera_.yaw)};

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
    fox_position_ += move * (kFoxMoveSpeed * move_scale * input.delta_time);
    fox_heading_ = std::atan2(move.x, move.z);
    fox_movement_speed_ = kFoxMoveSpeed * move_scale;
    horizontal_moved = true;
  }

  const float previous_y = fox_position_.y;
  fox_position_.y = fox_support_height(generator_, fox_position_, fox_heading_) + 1.0f;
  const bool vertical_moved = std::fabs(fox_position_.y - previous_y) > 0.0005f;
  return horizontal_moved || vertical_moved;
}

bool App::update_owl(const CameraInput& input) {
  const OwlState previous_state = owl_state_;
  const Vec3 previous_position = owl_position_;
  const float previous_heading = owl_heading_;
  const float previous_wing_pose = owl_wing_pose_;

  const Vec3 perch = owl_perch_position(generator_);
  float dt = input.delta_time;
  dt = std::max(0.0f, std::min(dt, 0.10f));

  if (owl_state_ == OwlState::Waiting) {
    owl_position_ = perch;
    owl_heading_ = kOwlDefaultHeading;
    owl_wing_pose_ = 0.0f;
    owl_timer_ = 0.0f;
    owl_dialogue_line_ = 0;
    const bool near_owl = horizontal_distance(fox_position_, owl_position_) <= kOwlEncounterRadius;
    owl_prompt_visible_ = near_owl;
    if (near_owl && !subtitles_visible()) {
      subtitles_show("Press A to talk", 0.45f);
    }
    if ((input.interact || input.action_pressed) && audio_ready_for_gameplay_sound() &&
        near_owl) {
      owl_state_ = OwlState::Talking;
      owl_prompt_visible_ = false;
      owl_dialogue_line_ = 1;
      subtitles_show("Oh good, you're awake.", 2.35f);
    }
  } else if (owl_state_ == OwlState::Talking) {
    owl_position_ = perch;
    owl_heading_ = kOwlDefaultHeading;
    owl_wing_pose_ = 0.0f;
    owl_timer_ += dt;
    if (owl_dialogue_line_ == 1 && owl_timer_ >= kOwlSecondLineTime) {
      owl_dialogue_line_ = 2;
      subtitles_show("The little lights are scattered. Bring them home.", 3.35f);
    }
    if (owl_timer_ >= kOwlTalkSeconds) {
      owl_state_ = OwlState::Flying;
      owl_timer_ = 0.0f;
    }
  } else if (owl_state_ == OwlState::Flying) {
    owl_timer_ += dt;
    const float raw_t = std::max(0.0f, std::min(1.0f, owl_timer_ / kOwlFlySeconds));
    const float t = smoothstep(raw_t);
    const float turn_t = smoothstep(std::max(0.0f, std::min(1.0f, raw_t / 0.35f)));
    owl_position_ = perch + Vec3{-2.0f * t, 1.35f * t + 4.8f * t * t, -13.0f * t};
    owl_heading_ = lerp(kOwlDefaultHeading, kOwlFlyAwayHeading, turn_t);
    owl_wing_pose_ = 0.25f + 0.75f * std::fabs(std::sin(owl_timer_ * 18.0f));
    if (owl_timer_ >= kOwlFlySeconds) {
      owl_state_ = OwlState::Gone;
      owl_wing_pose_ = 0.0f;
    }
  }

  return previous_state != owl_state_ ||
      length(previous_position - owl_position_) > 0.0005f ||
      std::fabs(previous_heading - owl_heading_) > 0.0005f ||
      std::fabs(previous_wing_pose - owl_wing_pose_) > 0.0005f;
}

void App::update_camera(const CameraInput& input) {
  const float look_delta_x = input.look_delta_x +
      clamped_axis(input.look_x) * kNormalizedLookPixelsPerSecond * input.delta_time;
  const float look_delta_y = input.look_delta_y +
      clamped_axis(input.look_y) * kNormalizedLookPixelsPerSecond * input.delta_time;
  camera_.yaw += look_delta_x * camera_.look_sensitivity;
  camera_.pitch -= look_delta_y * camera_.look_sensitivity;
  camera_.pitch = std::max(-1.20f, std::min(-0.12f, camera_.pitch));

  const Vec3 target = fox_position_ + Vec3{0.0f, kCameraTargetHeight, 0.0f};
  camera_.position = target - camera_.forward() * kCameraDistance;
}

}  // namespace voxel
