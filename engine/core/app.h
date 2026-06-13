#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "forest_audio.h"
#include "game/camera.h"
#include "game/conversation_controller.h"
#include "game/firefly_loop.h"
#include "game/fox_controller.h"
#include "game/owl_encounter.h"
#include "game/squirrel_quest.h"
#include "platform.h"
#include "render/mesh.h"
#include "world/generator.h"
#include "world/terrain_streamer.h"

namespace voxel {

class App {
 public:
  bool init(Renderer& renderer);
  void frame(Renderer& renderer, const CameraInput& input);
  void shutdown(Renderer& renderer);

  const Mesh& mesh() const { return mesh_; }
  const Mesh& terrain_mesh() const { return terrain_streamer_.mesh(); }
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
    int carried_fireflies = 0;
    int active_lantern_index = 0;
    int deposited_fireflies = 0;
    int required_fireflies = 0;
    int active_fireflies = 0;
    int active_gameplay_lights = 0;
    int gameplay_light_limit = 0;
    int terrain_visible_chunks = 0;
    int terrain_rebuilt_chunks = 0;
    std::size_t terrain_vertices = 0;
    std::size_t terrain_triangles = 0;
    std::size_t terrain_largest_chunk_vertices = 0;
    std::size_t terrain_largest_chunk_triangles = 0;
    std::size_t terrain_rebuilt_surface_columns = 0;
    std::size_t terrain_skipped_voxel_samples = 0;
    float fps = 0.0f;
    float firefly_glow_intensity = 0.0f;
    float lantern_light_intensity = 0.0f;
    float lantern_light_radius = 0.0f;
    bool glow_pass_enabled = true;
    bool local_lighting_enabled = true;
    float distance_to_objective = 0.0f;
    bool fox_moved = false;
    bool chunk_changed = false;
    bool squirrel_structural_changed = false;
    bool squirrel_animation_changed = false;
    bool squirrel_lights_changed = false;
    bool squirrel_animation_upload_due = false;
    bool gameplay_structural_changed = false;
    bool gameplay_animation_upload_due = false;
    bool dynamic_mesh_rebuilt = false;
    bool dynamic_mesh_uploaded = false;
  };

  const FrameStats& frame_stats() const { return frame_stats_; }
  int gameplay_light_limit() const { return gameplay_light_limit_; }
  void set_gameplay_light_limit(int limit);
  void dev_collect_active_fireflies();
  void dev_deposit_carried_fireflies();

 private:
  void rebuild_fox_mesh();
  void rebuild_dynamic_mesh();
  void rebuild_scene_mesh();
  void rebuild_gameplay_lights();
  void update_lantern_hud();
  void update_camera(const CameraInput& input);
  bool maybe_schedule_owl_return(Vec3 fox_position,
                                 float dt,
                                 bool fox_moved,
                                 bool squirrel_events_waiting);
  bool begin_owl_dialogue(const OwlEncounter::DialogueEvent& owl_dialogue,
                          Vec3 fox_position,
                          bool& gameplay_changed,
                          bool& gameplay_structural_changed);

  enum class OwlReturnMilestone {
    Locked,
    Pending,
    Seen,
  };

  TerrainGenerator generator_;
  TerrainStreamer terrain_streamer_;
  Mesh fox_mesh_;
  Mesh dynamic_mesh_;
  Mesh mesh_;
  Camera camera_;
  FoxController fox_controller_;
  ConversationController conversation_controller_;
  FireflyLoop firefly_loop_;
  OwlEncounter owl_encounter_;
  SquirrelQuest squirrel_quest_;
  std::vector<SquirrelQuest::ApproachEvent> squirrel_approach_events_;
  std::vector<SquirrelQuest::DialogueEvent> squirrel_dialogue_events_;
  std::vector<SquirrelQuest::CompletionEvent> squirrel_completion_events_;
  std::array<GameplayLight, kMaxGameplayLights> gameplay_lights_ = {};
  int gameplay_light_count_ = 0;
  int gameplay_light_limit_ = kMaxRendererGameplayLights;
  float hud_fps_ = 0.0f;
  float squirrel_animation_upload_timer_ = 0.0f;
  float gameplay_animation_upload_timer_ = 0.0f;
  RenderFrame render_frame_commands_;
  bool initialized_ = false;
  bool previous_interact_down_ = false;
  bool lantern_hud_revealed_ = false;
  bool squirrel_hud_revealed_ = false;
  OwlReturnMilestone owl_return_milestone_ = OwlReturnMilestone::Locked;
  Vec3 owl_return_completed_squirrel_position_ = {};
  float owl_return_quiet_seconds_ = 0.0f;
  FrameStats frame_stats_;
};

}  // namespace voxel
