#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "math/vec3.h"
#include "render/lights.h"
#include "render/mesh.h"

namespace voxel {

class TerrainGenerator;
class FireflyLoop;

class SquirrelQuest {
 public:
  struct CompletionEvent {
    Vec3 position = {};
    Vec3 squirrel_position = {};
    std::uint32_t squirrel_id = 0;
    char text[96] = {};
    float seconds = 2.0f;
  };

  struct ConversationRequest {
    Vec3 squirrel_position = {};
    std::uint32_t squirrel_id = 0;
    char text[96] = {};
    float seconds = 2.0f;
  };

  struct DialogueEvent {
    Vec3 squirrel_position = {};
    std::uint32_t squirrel_id = 0;
    char text[96] = {};
    float seconds = 2.0f;
  };

  struct ApproachEvent {
    Vec3 squirrel_position = {};
    std::uint32_t squirrel_id = 0;
    float seconds = 3.0f;
  };

  struct UpdateResult {
    bool structural_changed = false;
    bool animation_changed = false;
    bool lights_changed = false;
  };

  void init(const TerrainGenerator& generator, const FireflyLoop& firefly_loop);
  UpdateResult update(float dt,
                      const TerrainGenerator& generator,
                      const FireflyLoop& firefly_loop,
                      Vec3 fox_position,
                      bool allow_dialogue);
  bool conversation_request(Vec3 fox_position, ConversationRequest& request);
  bool set_talking_squirrel(std::uint32_t squirrel_id, bool talking);

  void append_dynamic_mesh(Mesh& mesh, Vec3 fox_position) const;
  void append_gameplay_lights(std::array<GameplayLight, kMaxGameplayLights>& lights,
                              int& light_count,
                              int light_limit,
                              Vec3 fox_position) const;

  int active_collected_acorns() const;
  int active_required_acorns() const;
  bool active_quest_needs_acorns() const;
  int completed_squirrels() const;
  int carried_acorns() const { return carried_acorns_; }
  bool has_active_quest() const { return active_squirrel_id_ != 0; }
  bool squirrel_position(std::uint32_t squirrel_id, Vec3& position) const;
  void drain_approach_events(std::vector<ApproachEvent>& events);
  void drain_dialogue_events(std::vector<DialogueEvent>& events);
  void drain_completion_events(std::vector<CompletionEvent>& events);

 private:
  static constexpr int kRequiredAcorns = 5;

  struct QuestProgress {
    int required_acorns = kRequiredAcorns;
    int collected_acorns = 0;
    bool completed = false;
    bool approach_started = false;
    bool greeted = false;
  };

  struct Squirrel {
    std::uint32_t id = 0;
    Vec3 home = {};
    Vec3 approach_start = {};
    Vec3 position = {};
    int lantern_index = -1;
    float heading = 0.0f;
    float home_heading = 0.0f;
    float animation_timer = 0.0f;
    float approach_timer = 0.0f;
    float approach_duration = 0.0f;
    float prompt_cooldown = 0.0f;
    float happy_timer = 0.0f;
    float idle_sound_cooldown = 0.0f;
    float scamper_sound_cooldown = 0.0f;
    bool active = false;
  };

  struct Acorn {
    std::uint32_t id = 0;
    Vec3 home = {};
    Vec3 position = {};
    int lantern_index = -1;
    float phase = 0.0f;
    bool active = false;
  };

  void refresh_nearby(const TerrainGenerator& generator, const FireflyLoop& firefly_loop, Vec3 fox_position);
  bool add_squirrel_candidate(const TerrainGenerator& generator, Vec3 lantern_position, int lantern_index);
  bool add_acorn_candidate(const TerrainGenerator& generator,
                           const FireflyLoop& firefly_loop,
                           Vec3 lantern_position,
                           int lantern_index,
                           int slot);
  QuestProgress& progress_for(std::uint32_t squirrel_id);
  const QuestProgress* progress_for(std::uint32_t squirrel_id) const;
  Squirrel* nearest_squirrel(Vec3 fox_position, float max_distance);
  const Squirrel* nearest_squirrel(Vec3 fox_position, float max_distance) const;
  std::uint32_t best_incomplete_squirrel_id(Vec3 fox_position) const;
  std::uint32_t display_squirrel_id() const;
  void remove_surplus_acorns_for_lantern(int lantern_index);

  std::vector<Squirrel> squirrels_;
  std::vector<Acorn> acorns_;
  std::unordered_map<std::uint32_t, QuestProgress> progress_;
  std::unordered_set<std::uint32_t> known_squirrel_ids_;
  std::unordered_set<std::uint32_t> known_acorn_ids_;
  std::unordered_set<std::uint32_t> collected_acorn_ids_;
  std::vector<ApproachEvent> approach_events_;
  std::vector<DialogueEvent> dialogue_events_;
  std::vector<CompletionEvent> completion_events_;
  std::vector<Vec3> lit_lantern_positions_;
  std::uint32_t talking_squirrel_id_ = 0;
  std::uint32_t active_squirrel_id_ = 0;
  int carried_acorns_ = 0;
  float refresh_timer_ = 0.0f;
};

}  // namespace voxel
