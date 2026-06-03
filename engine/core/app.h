#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "forest_audio.h"
#include "game/camera.h"
#include "platform.h"
#include "render/mesh.h"
#include "world/generator.h"

namespace voxel {

class App {
 public:
  bool init(Renderer& renderer);
  void frame(Renderer& renderer, const CameraInput& input);
  void shutdown(Renderer& renderer);

  const Mesh& mesh() const { return mesh_; }
  const Mesh& terrain_mesh() const { return terrain_mesh_; }
  const Mesh& fox_mesh() const { return fox_mesh_; }
  Camera& camera() { return camera_; }
  const Camera& camera() const { return camera_; }

  struct FrameStats {
    std::uint64_t total_ns = 0;
    std::uint64_t update_ns = 0;
    std::uint64_t world_rebuild_ns = 0;
    std::uint64_t fox_rebuild_ns = 0;
    std::uint64_t scene_rebuild_ns = 0;
    std::uint64_t upload_ns = 0;
    std::uint64_t render_ns = 0;
    ForestAudioDebugStatus forest_audio = {};
    int carried_fireflies = 0;
    int active_lantern_index = 0;
    int deposited_fireflies = 0;
    int required_fireflies = 0;
    int active_fireflies = 0;
    int active_gameplay_lights = 0;
    float firefly_glow_intensity = 0.0f;
    float lantern_light_intensity = 0.0f;
    float lantern_light_radius = 0.0f;
    bool glow_pass_enabled = true;
    bool local_lighting_enabled = true;
    float distance_to_objective = 0.0f;
    bool fox_moved = false;
    bool chunk_changed = false;
  };

  const FrameStats& frame_stats() const { return frame_stats_; }

 private:
  void rebuild_world_mesh();
  void rebuild_fox_mesh();
  void rebuild_dynamic_mesh();
  void rebuild_scene_mesh();
  bool update_fox(const CameraInput& input);
  bool update_owl(const CameraInput& input);
  bool update_fireflies(float dt);
  void init_lantern_loop();
  void activate_lantern(int sequence);
  void spawn_fireflies_for_lantern(int index);
  Vec3 gameplay_objective_position() const;
  Vec3 carried_firefly_position(int index) const;
  void rebuild_gameplay_lights();
  void add_gameplay_light(Vec3 position, Vec3 color, float radius, float intensity);
  int active_firefly_count() const;
  void update_camera(const CameraInput& input);

  static constexpr int kMaxLanterns = 4;
  static constexpr int kMaxFireflies = 12;

  struct Firefly {
    Vec3 home = {};
    Vec3 position = {};
    Vec3 velocity = {};
    float phase = 0.0f;
    float bob_timer = 0.0f;
    float glow_intensity = 1.0f;
    bool collected = false;
    bool active = false;
  };

  struct Lantern {
    Vec3 position = {};
    int required_fireflies = 3;
    int deposited_fireflies = 0;
    bool lit = false;
    bool active = false;
    float glow_intensity = 0.0f;
    float glow_timer = 0.0f;
    float pulse_timer = 0.0f;
  };

  struct CachedTerrainChunk {
    int chunk_x = 0;
    int chunk_z = 0;
    Mesh mesh;
  };

  TerrainGenerator generator_;
  std::vector<CachedTerrainChunk> terrain_chunk_cache_;
  Mesh terrain_mesh_;
  Mesh fox_mesh_;
  Mesh dynamic_mesh_;
  Mesh mesh_;
  Camera camera_;
  Vec3 fox_position_ = {};
  float fox_heading_ = 0.0f;
  float fox_movement_speed_ = 0.0f;
  enum class OwlState {
    Waiting,
    Talking,
    Flying,
    Gone,
  };
  OwlState owl_state_ = OwlState::Waiting;
  Vec3 owl_position_ = {};
  float owl_heading_ = 0.0f;
  float owl_wing_pose_ = 0.0f;
  float owl_timer_ = 0.0f;
  int owl_dialogue_line_ = 0;
  bool owl_prompt_visible_ = false;
  std::array<Lantern, kMaxLanterns> lanterns_ = {};
  std::array<Firefly, kMaxFireflies> fireflies_ = {};
  int active_lantern_index_ = 0;
  int lantern_sequence_ = 0;
  int carried_fireflies_ = 0;
  float firefly_chime_cooldown_ = 1.0f;
  float deposit_cooldown_ = 0.0f;
  std::array<GameplayLight, kMaxGameplayLights> gameplay_lights_ = {};
  int gameplay_light_count_ = 0;
  RenderFrame render_frame_commands_;
  bool intro_dialogue_started_ = false;
  float intro_dialogue_timer_ = 0.0f;
  int intro_dialogue_line_ = 0;
  int world_center_chunk_x_ = 0;
  int world_center_chunk_z_ = 0;
  bool initialized_ = false;
  FrameStats frame_stats_;
};

}  // namespace voxel
